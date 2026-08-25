class_name SpsController
extends RefCounted

## Sprite Splitter 共享控制器（M5.3 主视图/侧栏拆分）：
## 持有全部状态与业务逻辑（加载/分析/切分/去背景/导入/导出），通过信号驱动
## 主视图（画布）与侧栏（参数/操作）两个独立 UI——EditorPlugin 分别挂到
## 编辑器主屏幕与侧边 dock，跨区域交互由本 controller 桥接。
##
## 编码约定（项目强制）：var 显式类型标注，不用 :=。

signal status_changed(text: String, is_error: bool)
signal image_loaded(texture: Texture2D)
signal rects_changed(rects: Array[Rect2i])
signal count_changed(text: String)
signal analyze_done(stats: Dictionary)
signal exporting_changed(exporting: bool)
signal data_loaded(data: SpriteSplitterData)
signal data_dirty_changed(dirty: bool)
signal data_saved()
signal registry_updated()   # 注册表变化（新增/加载）→ UI 刷新列表

const EXPORT_PNG: int = 0
const EXPORT_META: int = 1
const EXPORT_TRES: int = 2
var data_dir: String = "res://sps_data"   # 项目数据目录（测试可注入隔离）


var splitter: SpriteSplitter = null
var image: Image = null
var image_name: String = ""
var image_res_path: String = ""   # 素材在项目内时转 res://（AtlasTexture 用）
var rects: Array[Rect2i] = []
var selection: Rect2i = Rect2i(-1, -1, 0, 0)   # CROP 裁切框（世界坐标）
var exporting: bool = false
var data: SpriteSplitterData = null   # 关联的项目数据（uid 关联）
var is_dirty: bool = false   # 有未保存的修改（参数/区域/项目名/导出位置）
var data_path: String = ""            # data 落盘路径
var registry: SpriteSplitterRegistry = null   # 项目注册表（默认 res://sps_data/registry.tres）

var _tree: SceneTree = null


func _init(tree: SceneTree, data_dir_override: String = "") -> void:
	_tree = tree
	splitter = SpriteSplitter.new()
	if not data_dir_override.is_empty():
		data_dir = data_dir_override   # 测试注入独立目录，隔离用户数据
	_ensure_registry()


# ---------- 项目注册表（SpriteSplitterRegistry，自动维护） ----------

# 确保默认注册表存在；首次创建后扫描 sps_data/*.tres 补登记既有项目
func _ensure_registry() -> void:
	if ResourceLoader.exists(data_dir + "/registry.tres"):
		registry = load(data_dir + "/registry.tres")
	else:
		registry = SpriteSplitterRegistry.new()
		ResourceSaver.save(registry, data_dir + "/registry.tres")
	_scan_registry()


# 扫描项目数据目录，把既有 .tres（排除注册表自身）补进注册表；
# 同时清理注册表中已不存在的失效条目（自愈）
func _scan_registry() -> void:
	if registry == null:
		return
	var dir: DirAccess = DirAccess.open(data_dir)
	if dir == null:
		registry_updated.emit()
		return
	var changed: bool = false
	dir.list_dir_begin()
	var f: String = dir.get_next()
	while not f.is_empty():
		if f.ends_with(".tres") and f != "registry.tres":
			if registry.register(data_dir + "/" + f):
				changed = true
		f = dir.get_next()
	dir.list_dir_end()
	# 移除失效条目（文件已被删除）
	var kept: Array[String] = []
	for entry: String in registry.entries:
		if ResourceLoader.exists(entry) or FileAccess.file_exists(entry):
			kept.append(entry)
		else:
			changed = true
	if kept.size() != registry.entries.size():
		registry.entries = kept
	if changed:
		ResourceSaver.save(registry, data_dir + "/registry.tres")
	registry_updated.emit()


# 注册一个 data 路径并持久化（保存/新建项目数据时自动调用）
func _register_data(path: String) -> void:
	if registry == null or path.is_empty():
		return
	if registry.register(path):
		ResourceSaver.save(registry, data_dir + "/registry.tres")
	registry_updated.emit()


# 从注册表点击加载项目配置（统一入口 → apply_data）
func load_registry_entry(path: String) -> void:
	if not ResourceLoader.exists(path):
		status_changed.emit("注册表条目不存在: " + path, true)
		return
	var d: Variant = load(path)
	if not (d is SpriteSplitterData):
		status_changed.emit("注册表条目不是 SpriteSplitterData: " + path, true)
		return
	apply_data(d, path)


func _wait_frame() -> void:
	if _tree != null:
		await _tree.process_frame


# ---------- 加载 / 分析 / 切分 ----------

func load_image(path: String) -> bool:
	var img: Image = Image.load_from_file(path)
	if img == null:
		status_changed.emit("加载失败: " + path, true)
		return false
	image = img
	image_name = path.get_file()
	image_res_path = ProjectSettings.localize_path(path)
	if not image_res_path.begins_with("res://"):
		image_res_path = ""   # 项目外素材：AtlasTexture .tres 不可用
	rects = []
	selection = Rect2i(-1, -1, 0, 0)
	image_loaded.emit(ImageTexture.create_from_image(img))
	rects_changed.emit([] as Array[Rect2i])   # 新图：清空画布红框与切片列表
	count_changed.emit("")
	_load_or_create_data()
	status_changed.emit("已加载 " + image_name, false)
	return true


# ---------- 项目数据（uid 关联的 .tres，兼容 meta.json rects） ----------

# 按素材 uid 查找 res://sps_data/<uid>.tres；有则加载恢复，无则初始化（自动填项目名）并落盘
func _load_or_create_data() -> void:
	data = null
	data_path = ""
	if not image_res_path.begins_with("res://"):
		return   # 项目外素材：不关联项目数据
	var uid_id: int = ResourceLoader.get_resource_uid(image_res_path)
	var uid: String = ""
	if uid_id != ResourceUID.INVALID_ID:
		uid = ResourceUID.id_to_text(uid_id)   # int uid → "uid://..."
	var tag: String = image_name.get_basename()
	if not uid.is_empty():
		tag = uid.trim_prefix("uid://")
	data_path = data_dir + "/" + tag + ".tres"
	if ResourceLoader.exists(data_path):
		data = load(data_path)
	else:
		data = SpriteSplitterData.new()
		data.sheet_uid = uid
		data.source_image = image_res_path
		data.project_name = image_name.get_basename()
		data.modified_at = int(Time.get_unix_time_from_system())
		DirAccess.make_dir_recursive_absolute(data_dir)
		ResourceSaver.save(data, data_path)
		_register_data(data_path)   # 新增项目数据自动注册
	data_loaded.emit(data)
	is_dirty = false   # 新素材/恢复数据后无未保存修改
	data_dirty_changed.emit(false)


# 标记有未保存修改（参数/区域/项目名/导出位置变化时由 UI 调用）
func mark_dirty() -> void:
	if is_dirty:
		return
	is_dirty = true
	data_dirty_changed.emit(true)


func _mark_clean() -> void:
	if not is_dirty:
		return
	is_dirty = false
	data_dirty_changed.emit(false)


# 应用外部加载的 SpriteSplitterData 配置（「打开配置」文件对话框 / 拖入 .tres）
# 统一入口：素材 → 项目数据 → 配套区域 → 参数 UI，一条链路
func apply_data(d: SpriteSplitterData, path: String) -> void:
	# 1) 素材：配置带素材且与当前图不同 → 统一走 load_image（素材缺失则保持当前图并提示）
	if not d.source_image.is_empty() and image_res_path != d.source_image:
		if not load_image(d.source_image):
			status_changed.emit("配置素材加载失败，保持当前图片: " + d.source_image, true)
	# 2) 项目数据以所选配置为准（load_image 内部可能按 uid 关联了其他 data，此处强制覆盖）
	data = d
	data_path = path
	# 3) 区域：配置 sprites 与当前图配套（全部落在图像范围内）才加载，否则清空并提示
	if image != null and _sprites_fit_image(d.sprites):
		rects = d.sprites.duplicate()
		rects_changed.emit(rects)
		count_changed.emit("导入 %d 个区域（配置）" % rects.size())
	else:
		rects = []
		rects_changed.emit([] as Array[Rect2i])
		count_changed.emit("")
		if d.sprites.is_empty():
			status_changed.emit("配置无切分区域（保存时未切分）: " + path, false)
	# 4) 恢复侧栏 UI（参数/项目名/导出位置）+ 重置脏数据
	is_dirty = false
	data_dirty_changed.emit(false)
	data_loaded.emit(d)
	if not rects.is_empty():
		status_changed.emit("已加载配置: " + path, false)


# 配置区域是否全部落在当前图像范围内（配套校验，防旧素材区域画到新图上）
func _sprites_fit_image(sprites: Array[Rect2i]) -> bool:
	if image == null or sprites.is_empty():
		return false
	var img_w: int = image.get_width()
	var img_h: int = image.get_height()
	for r: Rect2i in sprites:
		if r.position.x < 0 or r.position.y < 0 or r.end.x > img_w or r.end.y > img_h:
			return false
	return true


# 保存项目：常用参数 + 导出位置 + 项目名 + 当前 rects（meta.json 兼容）到 .tres
func save_project(project_name: String, options: Dictionary, out_dir: String,
		export_mode: int) -> void:
	if data == null or data_path.is_empty():
		status_changed.emit("无关联项目数据（素材需在项目内）", true)
		return
	data.project_name = project_name
	data.mode = String(options.get("mode", data.mode))
	data.min_width = int(options.get("min_width", data.min_width))
	data.min_height = int(options.get("min_height", data.min_height))
	data.grid_cell_size = int(options.get("grid_cell_size", data.grid_cell_size))
	data.merge_distance = int(options.get("merge_distance", data.merge_distance))
	data.alpha_threshold = int(options.get("alpha_threshold", data.alpha_threshold))
	data.background_threshold = int(options.get("background_threshold",
			data.background_threshold))
	data.out_dir = out_dir
	data.export_mode = export_mode
	data.sprites = rects.duplicate()   # 始终同步当前区域（含空）→ 配置与当前状态一致
	data.modified_at = int(Time.get_unix_time_from_system())   # 最后修改时间
	var err: Error = ResourceSaver.save(data, data_path)
	if err == OK:
		status_changed.emit("项目已保存 → " + data_path, false)
		_register_data(data_path)   # 保存自动注册（含已有条目刷新列表）
		_mark_clean()
		data_saved.emit()
		_refresh_filesystem()
	else:
		status_changed.emit("项目保存失败 (err=%d)" % int(err), true)


# 关闭当前图片（Header 关闭按钮）：清空素材/区域/项目数据，回到未加载状态
func close_image() -> void:
	image = null
	image_name = ""
	image_res_path = ""
	rects = []
	selection = Rect2i(-1, -1, 0, 0)
	data = null
	data_path = ""
	is_dirty = false
	image_loaded.emit(null)   # 画布清空（tex=null）
	rects_changed.emit([] as Array[Rect2i])
	count_changed.emit("")
	data_dirty_changed.emit(false)
	status_changed.emit("已关闭素材", false)


func analyze() -> void:
	if image == null:
		status_changed.emit("先打开素材表", true)
		return
	var stats: Dictionary = splitter.analyze(image)
	analyze_done.emit(stats)
	status_changed.emit("分析完成", false)


func split(options: Dictionary) -> void:
	if image == null:
		status_changed.emit("先打开素材表", true)
		return
	var arr: Array = splitter.split(image, options)
	rects = []
	for r: Variant in arr:
		if r is Rect2i:
			rects.append(r)
	if data != null:
		data.sprites = rects.duplicate()   # 内存同步（meta.json 兼容），保存时落盘
		mark_dirty()
	rects_changed.emit(rects)
	count_changed.emit("切分结果: %d 个精灵" % rects.size())
	status_changed.emit("切分完成: %d 个精灵" % rects.size(), rects.is_empty())


# ---------- 去背景（独立操作，替换当前图） ----------

func remove_background(threshold: int) -> void:
	if image == null:
		status_changed.emit("先打开素材表", true)
		return
	status_changed.emit("去背景中...", false)
	await _wait_frame()
	var out: Image = splitter.remove_background(image, {"background_threshold": threshold})
	if out == null:
		status_changed.emit("去背景失败（图像格式不支持）", true)
		return
	image = out
	image_res_path = ""   # 内存处理图：AtlasTexture .tres 不可用
	rects = []
	selection = Rect2i(-1, -1, 0, 0)
	if data != null:
		data.sprites = []   # 图已变，旧 rects 失效
		mark_dirty()
	image_loaded.emit(ImageTexture.create_from_image(out))
	rects_changed.emit([] as Array[Rect2i])   # 图已变：清空画布红框与切片列表
	count_changed.emit("")
	status_changed.emit("已去背景（背景透明）", false)


# ---------- 画布事件转发（主视图 → 侧栏状态） ----------

func set_crop_rect(rect_world: Rect2i) -> void:
	if rect_world.size.x <= 0 or rect_world.size.y <= 0:
		selection = Rect2i(-1, -1, 0, 0)
		status_changed.emit("已清除裁切框", false)
		return
	selection = rect_world
	status_changed.emit("裁切区域 %dx%d @ (%d,%d)" % [selection.size.x, selection.size.y,
			selection.position.x, selection.position.y], false)


func on_canvas_selection(selected: Array[Rect2i]) -> void:
	status_changed.emit("选中 %d 个精灵" % selected.size(), false)


# ---------- 导出 ----------

func export(mode: int, out_dir: String, options: Dictionary) -> void:
	if exporting:
		return
	if image == null:
		status_changed.emit("先打开素材表", true)
		return
	if mode != EXPORT_PNG and rects.is_empty():
		status_changed.emit("切 PNG 可直接导出；仅 meta.json / AtlasTexture 需先切分", true)
		return
	exporting = true
	exporting_changed.emit(true)
	var dir: String = out_dir.strip_edges()
	if dir.is_empty():
		dir = "res://out_sprites/ui"
	DirAccess.make_dir_recursive_absolute(dir)
	match mode:
		EXPORT_PNG:
			await _export_png(dir, options)
		EXPORT_META:
			await _export_meta(dir)
		EXPORT_TRES:
			await _export_tres(dir)
	await _wait_frame()
	exporting = false
	exporting_changed.emit(false)


func export_selected(out_dir: String) -> void:
	if exporting:
		return
	if image == null:
		status_changed.emit("先打开素材表", true)
		return
	if selection.size.x <= 0 or selection.size.y <= 0:
		status_changed.emit("先在预览中用「裁切」工具框选区域", true)
		return
	exporting = true
	exporting_changed.emit(true)
	var dir: String = out_dir.strip_edges()
	if dir.is_empty():
		dir = "res://out_sprites/ui"
	DirAccess.make_dir_recursive_absolute(dir)
	var stem: String = image_name.get_basename()
	var out_path: String = "%s/%s_selected_%d_%d.png" % [dir, stem,
			selection.position.x, selection.position.y]
	var err: Error = splitter.export_sprite(image, selection, out_path)
	await _wait_frame()
	status_changed.emit("导出选中 → %s (err=%d)" % [out_path, int(err)], err != OK)
	_refresh_filesystem()
	exporting = false
	exporting_changed.emit(false)


func _export_png(out_dir: String, options: Dictionary) -> void:
	var files: Array = splitter.split_and_export(image, options, out_dir)
	await _wait_frame()
	var ok: bool = true
	for f: Variant in files:
		if not FileAccess.file_exists(String(f)):
			ok = false
	status_changed.emit("导出 %d 个 PNG → %s" % [files.size(), out_dir], not ok)
	_refresh_filesystem()


func _export_meta(out_dir: String) -> void:
	var meta_path: String = out_dir + "/meta.json"
	var err: Error = splitter.export_metadata(image, rects, image_name, meta_path)
	await _wait_frame()
	status_changed.emit("meta.json → %s (err=%d)" % [meta_path, int(err)], err != OK)
	if err == OK:
		_refresh_filesystem()


func _export_tres(out_dir: String) -> void:
	if image_res_path.is_empty():
		status_changed.emit("AtlasTexture 需要项目内素材（当前文件在项目外）", true)
		return
	var atlas: Texture2D = load(image_res_path)
	if atlas == null:
		status_changed.emit("无法加载导入纹理: " + image_res_path, true)
		return
	var saved: int = 0
	for i: int in range(rects.size()):
		var at: AtlasTexture = AtlasTexture.new()
		at.atlas = atlas
		at.region = rects[i]
		var p: String = "%s/atlas_%02d.tres" % [out_dir, i + 1]
		if ResourceSaver.save(at, p) == OK:
			saved += 1
		if i % 10 == 9:
			await _wait_frame()
	status_changed.emit("AtlasTexture .tres ×%d → %s" % [saved, out_dir], saved != rects.size())
	if saved > 0:
		_refresh_filesystem()


# 编辑器模式下触发资产库（FileSystem dock）重扫；运行模式直接返回
func _refresh_filesystem() -> void:
	if not Engine.is_editor_hint():
		return
	var fs: EditorFileSystem = EditorInterface.get_resource_filesystem()
	fs.scan_sources()


# ---------- 从已有 meta.json 导入区域 ----------

func import_meta(path: String) -> void:
	if image == null:
		status_changed.emit("先打开素材表，再导入区域", true)
		return
	var text: String = FileAccess.get_file_as_string(path)
	if text.is_empty():
		status_changed.emit("读取 meta.json 失败: " + path, true)
		return
	var j: Variant = JSON.parse_string(text)
	if not (j is Dictionary):
		status_changed.emit("meta.json 解析失败: " + path, true)
		return
	var sprites: Variant = j.get("sprites", [])
	if not (sprites is Array):
		status_changed.emit("meta.json 缺少 sprites 数组: " + path, true)
		return
	var imported: Array[Rect2i] = []
	for item: Variant in sprites:
		if not (item is Dictionary):
			continue
		var x: int = int(item.get("x", 0))
		var y: int = int(item.get("y", 0))
		var w: int = int(item.get("width", 0))
		var h: int = int(item.get("height", 0))
		if w <= 0 or h <= 0:
			continue
		imported.append(Rect2i(x, y, w, h))
	if imported.is_empty():
		status_changed.emit("meta.json 中没有有效区域", true)
		return
	rects = imported
	if data != null:
		data.sprites = rects.duplicate()   # 导入 meta.json → 同步项目数据
		mark_dirty()
	rects_changed.emit(rects)
	count_changed.emit("导入 %d 个区域（meta.json）" % rects.size())
	status_changed.emit("已导入区域: " + path, false)
