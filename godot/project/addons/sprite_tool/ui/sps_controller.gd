class_name SpsController
extends RefCounted

## sprite-tool 共享控制器（M5.3 主视图/侧栏拆分）：
## 持有全部状态与业务逻辑（加载/分析/切分/去背景/导入/导出），通过信号驱动
## 主视图（画布）与侧栏（参数/操作）两个独立 UI——EditorPlugin 分别挂到
## 编辑器主屏幕与侧边 dock，跨区域交互由本 controller 桥接。
##
## 编码约定（项目强制）：var 显式类型标注，不用 :=。

signal status_changed(text: String, is_error: bool)
signal image_loaded(texture: Texture2D)
signal rects_changed(rects: Array[Rect2i])
signal sprites_changed(sprites: Array[Dictionary])   # 复杂切片结构（name/uid/xywh/locked/ignored）
signal count_changed(text: String)
signal analyze_done(stats: Dictionary)
signal auto_diag_changed(diag: Dictionary)   # 最近一次 Auto 切分的诊断（mode/confidence/layout/offset；非 auto 为空 {}）
signal exporting_changed(exporting: bool)
signal data_loaded(data: SpriteSplitterData)
signal data_path_changed(path: String)   # 当前项目数据路径变化（打开图片/配置后，UI 据此同步注册表选中项；外部素材为 ""）
signal data_dirty_changed(dirty: bool)
signal data_saved()
signal registry_updated()   # 注册表变化（新增/加载）→ UI 刷新列表
signal edit_sprite_requested(index: int)   # 请求编辑切片（侧栏停靠编辑面板填充）
signal autosave_state_changed(saving: bool)   # 自动/手动保存进行中（UI 禁用保存按钮）
signal autosave_countdown(seconds_left: int)   # 防抖倒计时（3→2→1；0 = 保存完成/取消，UI 清除提示）

const EXPORT_PNG: int = 0
const EXPORT_META: int = 1
const EXPORT_TRES: int = 2
var data_dir: String = "res://sps_data"   # 项目数据目录（测试可注入隔离）

# ---------- 输出根目录（ProjectSettings 配置，所有生成资源统一落此目录下） ----------
const SETTING_OUT_ROOT: String = "sprite_tool/out_root"
const DEFAULT_OUT_ROOT: String = "res://out_sprites"
const OUTPUT_REG_FILE: String = "output_registry.tres"


var splitter: SpriteSplitter = null
var sheet_builder: SpriteSheetBuilder = null   # Sheet 重排（独立新类，对齐 CLI sheet 命令）
var image: Image = null
var image_name: String = ""
var image_res_path: String = ""   # 素材在项目内时转 res://（AtlasTexture 用）
var sprites: Array[Dictionary] = []   # 主数据：复杂切片结构 {uid,name,x,y,width,height,locked,ignored}
var rects: Array[Rect2i] = []         # 兼容视图：由 sprites 派生（_sync_rects 维护），画布/旧调用用
var selection: Rect2i = Rect2i(-1, -1, 0, 0)   # CROP 裁切框（世界坐标）
var exporting: bool = false
var data: SpriteSplitterData = null   # 关联的项目数据（uid 关联）
var is_dirty: bool = false   # 有未保存的修改（参数/区域/项目名/导出位置）
var data_path: String = ""            # data 落盘路径
var registry: SpriteSplitterRegistry = null   # 项目注册表（默认 res://sps_data/registry.tres）
var output_registry: SpriteOutputRegistry = null   # 生成资源注册表（记录全部写盘产物，供清理）
var auto_diag: Dictionary = {}   # 最近一次 Auto 切分的诊断（split_detailed 的 auto_* 字段；非 auto 为空）

# ---------- 自动保存（限流防抖 + 保存锁防竞争） ----------
# mark_dirty → 防抖调度（停止修改 autosave_delay 秒后保存一次）；保存期间
# 再标记脏 → _save_queued 合并为保存完成后补存一次；参数最新值统一放 _save_pending
# （side 通过 sync_save_params 同步，手动/自动保存共用），避免自动保存用旧参数。
var autosave_enabled: bool = true
var autosave_delay: float = 3.0            # 停止修改后延迟（秒，默认 3 → 倒计时 3 2 1）
var _save_pending: Dictionary = {}         # 待保存参数（project_name/options/out_dir/export_mode）
var _autosave_deadline: float = -1.0       # -1 = 无待保存；>0 = 防抖到期时间（秒，Time.get_ticks_msec）
var _autosave_loop_running: bool = false
var _saving: bool = false                  # 保存进行中（防重入锁）
var _save_queued: bool = false             # 保存中又标记脏 → 保存完成后补存
var _autosave_suspended: int = 0           # 挂起计数（去背景等数据迁移流程期间暂停）

var _tree: SceneTree = null


func _init(tree: SceneTree, data_dir_override: String = "") -> void:
	_tree = tree
	splitter = SpriteSplitter.new()
	sheet_builder = SpriteSheetBuilder.new()
	if not data_dir_override.is_empty():
		data_dir = data_dir_override   # 测试注入独立目录，隔离用户数据
	_ensure_out_root_setting()
	_ensure_registry()
	_ensure_output_registry()


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


# 删除注册表条目（右键菜单 → 删除）：删除 .tres 文件，条目由 _scan_registry
# 自动扫描清理（自愈机制：文件不存在 → 移出 entries + 持久化 + 刷新列表）。
# 若删除的是当前项目配置 → 一并关闭图片（画布/切片/项目数据清空，回到未加载态）。
func remove_registry_entry(path: String) -> void:
	if path.is_empty():
		return
	if path == data_path:
		close_image()   # 删除当前资源：关闭图片（内部清 data/data_path + 信号）
		data_path_changed.emit("")   # close_image 不 emit 该信号；补发 → 注册表取消选中
	if ResourceLoader.exists(path) or FileAccess.file_exists(path):
		DirAccess.remove_absolute(path)   # 同时删除 .tres 文件
	_scan_registry()   # 自动扫描：清理失效条目 + 持久化 + 刷新列表
	_refresh_filesystem()   # 通知编辑器文件系统重扫（资产库移除已删 .tres / .uid 缓存）
	status_changed.emit("已删除项目配置: " + path, false)


# ---------- 输出根目录（ProjectSettings 配置：项目设置 → sprite_tool/out_root） ----------
# 所有生成资源（切分 PNG / meta.json / AtlasTexture .tres / 去背景 PNG / sheet）统一落
# 此根目录下；项目数据 out_dir 默认 = <out_root>/<uid 子目录>。编辑器模式改动才写盘
# project.godot（headless/运行模式仅内存生效，测试注入不污染项目设置）。

func _ensure_out_root_setting() -> void:
	if not ProjectSettings.has_setting(SETTING_OUT_ROOT):
		ProjectSettings.set_setting(SETTING_OUT_ROOT, DEFAULT_OUT_ROOT)
	ProjectSettings.add_property_info({
		"name": SETTING_OUT_ROOT,
		"type": TYPE_STRING,
		"hint": PROPERTY_HINT_DIR,
		"hint_string": "",
	})


func get_out_root() -> String:
	var v: Variant = ProjectSettings.get_setting(SETTING_OUT_ROOT, DEFAULT_OUT_ROOT)
	var s: String = String(v).strip_edges()
	return s if not s.is_empty() else DEFAULT_OUT_ROOT


# persist=true 才写盘 project.godot（编辑器模式）；false 仅内存（side 输入过程实时生效，
# 回车/失焦/默认按钮再持久化，避免逐键写盘）
func set_out_root(v: String, persist: bool = false) -> void:
	var s: String = v.strip_edges()
	if s.is_empty():
		s = DEFAULT_OUT_ROOT
	ProjectSettings.set_setting(SETTING_OUT_ROOT, s)
	if persist and Engine.is_editor_hint():
		ProjectSettings.save()


# 项目数据 out_dir 的新建默认值：<out_root>/<tag>
func default_out_dir(tag: String) -> String:
	return get_out_root() + "/" + tag


# ---------- 生成资源注册表（SpriteOutputRegistry：登记全部写盘产物，供清理） ----------

func _ensure_output_registry() -> void:
	var p: String = data_dir + "/" + OUTPUT_REG_FILE
	if ResourceLoader.exists(p):
		output_registry = load(p)
	else:
		output_registry = SpriteOutputRegistry.new()
		DirAccess.make_dir_recursive_absolute(data_dir)
		ResourceSaver.save(output_registry, p)


# 登记一个生成资源（导出/去背景/sheet 写盘成功后调用）→ 自动持久化
func register_output_file(path: String, kind: String) -> void:
	if output_registry == null or path.is_empty():
		return
	output_registry.register(path, kind, _output_project())
	ResourceSaver.save(output_registry, data_dir + "/" + OUTPUT_REG_FILE)


# 生成资源归属项目名（有项目数据用项目名，否则素材名）
func _output_project() -> String:
	if data != null and not String(data.project_name).is_empty():
		return String(data.project_name)
	if not image_name.is_empty():
		return image_name
	return ""


# ---------- 生成资源占用检查 + 清理 ----------

# 一次性扫描全部注册资源的占用状态 → {path: bool}（true = 被占用，不可清理）
func check_outputs_usage() -> Dictionary:
	var refs: Dictionary = _scan_project_refs()
	var out: Dictionary = {}
	for e: Dictionary in output_registry.entries:
		var p: String = String(e.get("path", ""))
		if not p.is_empty():
			out[p] = bool(refs.get(p, false))
	return out


func is_output_occupied(path: String) -> bool:
	var refs: Dictionary = _scan_project_refs()
	return bool(refs.get(path, false))


# 占用判定：① 插件自身（当前图 / 项目数据 source_image 引用）；
# ② 项目文本资源（.tscn/.tres/.gd/.json/.cfg，排除 .godot 缓存与生成资源自身）
# 内容出现该资源路径或 uid。生成文件互相引用（如 sheet_meta.json 记源 PNG）不算占用。
func _scan_project_refs() -> Dictionary:
	var occupied: Dictionary = {}
	# ① 插件自身：当前图 + 项目数据源
	if image_res_path.begins_with("res://"):
		occupied[image_res_path] = true
	if data != null and String(data.source_image).begins_with("res://"):
		occupied[String(data.source_image)] = true
	# ② 项目文本引用：收集每个注册资源的搜索串（路径 + uid）
	var targets: Array[Dictionary] = []
	for e: Dictionary in output_registry.entries:
		var p: String = String(e.get("path", ""))
		if not p.is_empty():
			targets.append({"path": p, "uid": _uid_text_of(p)})
	if targets.is_empty():
		return occupied
	# 引用来源排除集：生成资源自身 + 注册表自身（内容含全部登记路径，互相引用不算占用）
	var self_paths: Dictionary = {}
	for e: Dictionary in output_registry.entries:
		self_paths[String(e.get("path", ""))] = true
	self_paths[data_dir + "/" + OUTPUT_REG_FILE] = true
	var stack: Array[String] = ["res://"]
	while not stack.is_empty():
		var d: String = stack.pop_back()
		var dir: DirAccess = DirAccess.open(d)
		if dir == null:
			continue
		dir.list_dir_begin()
		var f: String = dir.get_next()
		while not f.is_empty():
			if f == "." or f == "..":
				f = dir.get_next()
				continue
			var full: String = d + f
			if dir.current_is_dir():
				if f != ".godot":
					stack.append(full + "/")
			elif _is_scan_file(full) and not self_paths.has(full):
				var text: String = FileAccess.get_file_as_string(full)
				for t: Dictionary in targets:
					if not occupied.has(String(t["path"])) \
							and (text.contains(String(t["path"])) \
							or (not String(t["uid"]).is_empty() \
							and text.contains(String(t["uid"])))):
						occupied[String(t["path"])] = true
			f = dir.get_next()
		dir.list_dir_end()
	return occupied


# 读取资源旁 .uid 文件（Godot 4.4+）或 uid 缓存，返回 uid 文本（无则 ""）
func _uid_text_of(path: String) -> String:
	var uid_file: String = path + ".uid"
	if FileAccess.file_exists(uid_file):
		var text: String = FileAccess.get_file_as_string(uid_file).strip_edges()
		if text.begins_with("uid://"):
			return text
	var id: int = ResourceLoader.get_resource_uid(path)
	if id != ResourceUID.INVALID_ID:
		return ResourceUID.id_to_text(id)
	return ""


# 参与占用扫描的文本文件扩展名（project.godot 排除：out_root 设置值会误报）
func _is_scan_file(path: String) -> bool:
	var ext: String = path.get_extension().to_lower()
	return ext in ["tscn", "tres", "gd", "json", "cfg"]


# 清理生成资源：删除文件 + 移除注册表条目。占用项拒绝（force=true 才删）。
# 返回 {deleted: int, refused: Array[String], missing: int}
func cleanup_outputs(paths: Array, force: bool = false) -> Dictionary:
	var deleted: int = 0
	var refused: Array[String] = []
	var missing: int = 0
	var changed: bool = false
	var usage: Dictionary = check_outputs_usage()
	for p: Variant in paths:
		var path: String = String(p)
		if not output_registry.has(path):
			continue
		if not FileAccess.file_exists(path):
			missing += 1
			output_registry.remove(path)
			changed = true
			continue
		if not force and bool(usage.get(path, false)):
			refused.append(path)
			continue
		if DirAccess.remove_absolute(path) == OK:
			deleted += 1
		output_registry.remove(path)
		changed = true
	if changed:
		ResourceSaver.save(output_registry, data_dir + "/" + OUTPUT_REG_FILE)
		_refresh_filesystem()
	return {"deleted": deleted, "refused": refused, "missing": missing}


func _wait_frame() -> void:
	if _tree != null:
		await _tree.process_frame


# ---------- 切片复杂结构（SpriteItem = Dictionary） ----------
# 每项：{uid, name, x, y, width, height, locked, ignored}
# 兼容：data.sprites 旧格式为 Array[Rect2i]，读取时统一转复杂结构。

func _make_sprite(rect: Rect2i, index: int) -> Dictionary:
	return {
		"uid": "sprite_%d" % (index + 1),
		"name": "精灵 %d" % (index + 1),
		"x": rect.position.x,
		"y": rect.position.y,
		"width": rect.size.x,
		"height": rect.size.y,
		"locked": false,
		"ignored": false,
		"group": "",   # 分组名（空 = 未分组；UI 按分组选择）
		"selected": false,   # 选中状态（UI：列表 emoji + 画布高亮），不参与持久化语义
	}


func _rect_of(s: Dictionary) -> Rect2i:
	return Rect2i(int(s.get("x", 0)), int(s.get("y", 0)),
			int(s.get("width", 0)), int(s.get("height", 0)))


# 任意来源（Array[Rect2i] 或 Array[Dictionary]）→ 复杂结构数组
func _sprites_from_any(src: Array) -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	var index: int = 0
	for item: Variant in src:
		if item is Rect2i:
			out.append(_make_sprite(item, index))
		elif item is Dictionary:
			var s: Dictionary = item.duplicate()
			if not s.has("uid"):
				s["uid"] = "sprite_%d" % (index + 1)
			if not s.has("name"):
				s["name"] = "精灵 %d" % (index + 1)
			if not s.has("locked"):
				s["locked"] = false
			if not s.has("ignored"):
				s["ignored"] = false
			if not s.has("group"):
				s["group"] = ""
			if not s.has("selected"):
				s["selected"] = false
			out.append(s)
		index += 1
	return out


# 数据变更统一出口：sprites → rects 兼容视图 + 双信号
func _sync_rects() -> void:
	rects = []
	for s: Dictionary in sprites:
		rects.append(_rect_of(s))
	rects_changed.emit(rects)
	sprites_changed.emit(sprites)


# 导出用 rects：忽略 ignored 项
func get_export_rects() -> Array[Rect2i]:
	var out: Array[Rect2i] = []
	for s: Dictionary in sprites:
		if bool(s.get("ignored", false)):
			continue
		out.append(_rect_of(s))
	return out


# 画布编辑提交：拖拽移动 / 四角缩放（锁定项拒绝）
func update_sprite_geometry(index: int, rect: Rect2i) -> void:
	if index < 0 or index >= sprites.size():
		return
	var s: Dictionary = sprites[index]
	if bool(s.get("locked", false)):
		return
	if rect.size.x < 1 or rect.size.y < 1:
		return
	s["x"] = rect.position.x
	s["y"] = rect.position.y
	s["width"] = rect.size.x
	s["height"] = rect.size.y
	_sync_rects()
	mark_dirty()


# 编辑窗口批量提交：一次更新切片全部可编辑字段（名称/几何/锁定/忽略）。
# 几何 clamp 到图内；锁定项几何保持原值（与画布一致：锁定不可编辑几何）；
# 名称为空时保持原名（列表有 #N 兜底，但空名无意义）。
func update_sprite_fields(index: int, fields: Dictionary) -> void:
	if index < 0 or index >= sprites.size():
		return
	var s: Dictionary = sprites[index]
	var new_locked: bool = bool(fields.get("locked", s.get("locked", false)))
	var new_ignored: bool = bool(fields.get("ignored", s.get("ignored", false)))
	var want_rect: Rect2i = Rect2i(
			int(fields.get("x", s.get("x", 0))),
			int(fields.get("y", s.get("y", 0))),
			int(fields.get("width", s.get("width", 0))),
			int(fields.get("height", s.get("height", 0))))
	if not new_locked and want_rect.size.x >= 1 and want_rect.size.y >= 1:
		if image != null:
			var img_w: int = image.get_width()
			var img_h: int = image.get_height()
			want_rect.position.x = clampi(want_rect.position.x, 0, maxi(0, img_w - 1))
			want_rect.position.y = clampi(want_rect.position.y, 0, maxi(0, img_h - 1))
			want_rect.size.x = clampi(want_rect.size.x, 1, maxi(1, img_w - want_rect.position.x))
			want_rect.size.y = clampi(want_rect.size.y, 1, maxi(1, img_h - want_rect.position.y))
		s["x"] = want_rect.position.x
		s["y"] = want_rect.position.y
		s["width"] = want_rect.size.x
		s["height"] = want_rect.size.y
	s["locked"] = new_locked
	s["ignored"] = new_ignored
	if fields.has("name"):
		var nm: String = String(fields.get("name", "")).strip_edges()
		if not nm.is_empty():
			s["name"] = nm
	if fields.has("group"):
		s["group"] = String(fields.get("group", "")).strip_edges()   # 空 = 未分组（允许清空）
	_sync_rects()
	mark_dirty()


# 全部分组名（去重、按出现顺序；不含空组名）；UI 重建分组选择下拉用
func get_groups() -> Array[String]:
	var out: Array[String] = []
	for s: Dictionary in sprites:
		var g: String = String(s.get("group", "")).strip_edges()
		if not g.is_empty() and not out.has(g):
			out.append(g)
	return out


# 按分组选择：选中该分组全部切片（替换当前选择）；分组为空字符串 = 未分组项。
# 选中是 UI 状态：不 mark_dirty；sprites_changed 驱动画布/列表/编辑面板同步。
func select_group(group: String) -> void:
	var g: String = group.strip_edges()
	var hit: int = 0
	for i: int in sprites.size():
		var want: bool = String(sprites[i].get("group", "")).strip_edges() == g
		if want:
			hit += 1
		if bool(sprites[i].get("selected", false)) != want:
			sprites[i]["selected"] = want
	if hit == 0:
		status_changed.emit("分组「%s」无切片" % (g if not g.is_empty() else "未分组"), true)
		return
	sprites_changed.emit(sprites)
	status_changed.emit("已选择分组「%s」：%d 个切片" % [g if not g.is_empty() else "未分组", hit], false)


# 选择全部切片（替换当前选择；UI 状态不 mark_dirty）
func select_all() -> void:
	if sprites.is_empty():
		status_changed.emit("无切片可选", true)
		return
	for s: Dictionary in sprites:
		s["selected"] = true
	sprites_changed.emit(sprites)
	status_changed.emit("已选择全部：%d 个切片" % sprites.size(), false)


# 批量删除选中的切片（锁定项不可选自然不参与；ignored 项也删——删除=彻底移除）。
# 数据变更：同步 data.sprites + _sync_rects 双信号 + mark_dirty + count 刷新。
func remove_selected_sprites() -> int:
	var kept: Array[Dictionary] = []
	for s: Dictionary in sprites:
		if not bool(s.get("selected", false)):
			kept.append(s)
	var removed: int = sprites.size() - kept.size()
	if removed == 0:
		status_changed.emit("没有选中的切片可删除", true)
		return 0
	sprites = kept
	if data != null:
		data.sprites = sprites.duplicate()
	_sync_rects()
	mark_dirty()
	count_changed.emit("切分结果: %d 个精灵" % sprites.size())
	status_changed.emit("已删除 %d 个选中切片，剩余 %d 个" % [removed, sprites.size()], false)
	return removed


# 多选批量设置分组：所有 selected 切片 group 设为同一值（空 = 清除分组）。
# 数据变更：_sync_rects 双信号 + mark_dirty（与编辑面板保存语义一致）。
func set_group_for_selected(group: String) -> void:
	var g: String = group.strip_edges()
	var hit: int = 0
	var changed: bool = false
	for i: int in sprites.size():
		if not bool(sprites[i].get("selected", false)):
			continue
		hit += 1
		if String(sprites[i].get("group", "")).strip_edges() != g:
			sprites[i]["group"] = g
			changed = true
	if hit == 0:
		status_changed.emit("先选中切片再批量设置分组", true)
		return
	if changed:
		_sync_rects()
		mark_dirty()
	status_changed.emit("已为 %d 个切片设置分组「%s」" % [hit, g if not g.is_empty() else "未分组"], false)


func rename_sprite(index: int, name: String) -> void:
	if index < 0 or index >= sprites.size():
		return
	sprites[index]["name"] = name
	sprites_changed.emit(sprites)   # 仅列表刷新（几何不变）
	mark_dirty()


func set_sprite_locked(index: int, locked: bool) -> void:
	if index < 0 or index >= sprites.size():
		return
	sprites[index]["locked"] = locked
	sprites_changed.emit(sprites)
	mark_dirty()


func set_sprite_ignored(index: int, ignored: bool) -> void:
	if index < 0 or index >= sprites.size():
		return
	sprites[index]["ignored"] = ignored
	sprites_changed.emit(sprites)
	mark_dirty()


# 编辑请求（列表双击/右键「编辑…」→ 侧栏停靠编辑面板）：
# 多选暂不支持——selected > 1 拒绝并提示单选；否则收敛为单选目标项（写数据 +
# sprites_changed 同步画布/列表高亮），再发 edit_sprite_requested 让侧栏切到编辑页填充。
func request_edit_sprite(index: int) -> void:
	if index < 0 or index >= sprites.size():
		return
	var sel_count: int = 0
	for s: Dictionary in sprites:
		if bool(s.get("selected", false)):
			sel_count += 1
	if sel_count > 1:
		status_changed.emit("多选暂不支持编辑，请先单选一个切片", true)
		return
	var changed: bool = false
	for i: int in sprites.size():
		var want: bool = i == index
		if bool(sprites[i].get("selected", false)) != want:
			sprites[i]["selected"] = want
			changed = true
	if changed:
		sprites_changed.emit(sprites)   # 画布/列表高亮同步（uid 不变，编辑态保留）
	edit_sprite_requested.emit(index)


# 自由裁切确认：把选区（世界坐标）加入切片数据。clamp 到图内，无效选区忽略。
func add_sprite_from_rect(rect: Rect2i) -> void:
	if image == null or rect.size.x < 1 or rect.size.y < 1:
		return
	var r: Rect2i = rect.intersection(Rect2i(Vector2i.ZERO,
			Vector2i(image.get_width(), image.get_height())))
	if r.size.x < 1 or r.size.y < 1:
		return
	sprites.append(_make_sprite(r, sprites.size()))
	if data != null:
		data.sprites = sprites.duplicate()
		mark_dirty()
	_sync_rects()
	count_changed.emit("切分结果: %d 个精灵" % sprites.size())
	status_changed.emit("已添加裁切选区（%d,%d %dx%d）" % [
			r.position.x, r.position.y, r.size.x, r.size.y], false)


# ---------- 加载 / 分析 / 切分 ----------

# 绑定编辑器文件系统信号：源文件重命名/移动会触发重导入流程（resources_reimported /
# filesystem_changed），借此自动修复注册表里 source_image 失效的配置路径。
# 仅编辑器模式可用（EditorInterface）；运行模式（headless 测试）直接跳过。
func connect_fs_signals() -> void:
	if not Engine.is_editor_hint():
		return
	var fs: EditorFileSystem = EditorInterface.get_resource_filesystem()
	fs.resources_reimported.connect(_on_fs_changed)
	fs.filesystem_changed.connect(_on_fs_changed)


func _on_fs_changed(_res: Variant = null) -> void:
	_repair_stale_registry_paths()


# 扫描注册表：source_image 失效（源文件更名/移动）的配置 → 用 sheet_uid 反查
# uid 缓存拿当前路径，修复并持久化；有修复则刷新注册表列表（缩略图重新生成）。
func _repair_stale_registry_paths() -> void:
	if registry == null:
		return
	var repaired: bool = false
	for p: String in registry.entries:
		if not ResourceLoader.exists(p):
			continue   # 死条目（registry 有独立清理）
		var d: Variant = load(p)
		if not (d is SpriteSplitterData):
			continue
		if not String(d.source_image).is_empty() \
				and not ResourceLoader.exists(d.source_image):
			var src: String = _resolve_source_path(d)
			if src.is_empty() or src == d.source_image:
				continue
			d.source_image = src
			d.source_texture = load(src) if src.begins_with("res://") \
					else null
			if ResourceSaver.save(d, p) == OK:
				repaired = true
	if repaired:
		registry_updated.emit()


# 按 uid 加载素材：uid://... 文本 → uid 缓存反查当前路径 → load_image。
# 源文件更名/移动后路径变化也能定位（依赖 Godot uid 缓存，编辑器维护）。
# 失败（uid 无效/缓存无此 uid/文件不存在）返回 false，不改变当前图。
func load_image_by_uid(uid_text: String) -> bool:
	if uid_text.is_empty():
		return false
	var id: int = ResourceUID.text_to_id(uid_text)
	if id == ResourceUID.INVALID_ID:
		return false
	var path: String = ResourceUID.get_id_path(id)
	if not path.begins_with("res://") or not ResourceLoader.exists(path):
		return false
	return load_image(path)


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
	auto_diag = {}
	auto_diag_changed.emit({})
	image_loaded.emit(ImageTexture.create_from_image(img))
	sprites = []
	_sync_rects()   # rects 由 sprites 派生 + 双信号：清空画布红框与切片列表
	count_changed.emit("")
	_load_or_create_data()
	status_changed.emit("已加载 " + image_name, false)
	return true


# ---------- 项目数据（uid 关联的 .tres，兼容 meta.json rects） ----------

# 源路径同步：data.source_image 跟随当前实际加载的素材路径。
# 源文件更名后 uid 保留（Godot 重命名不换 uid），拖入新图会按 uid 匹配到旧配置——
# 此处把配置里的旧路径就地修正为当前路径，下次保存/重启不再失效。
func _sync_source_path() -> void:
	if data == null:
		return
	if image_res_path.begins_with("res://") and data.source_image != image_res_path:
		data.source_image = image_res_path


# 配置素材路径解析：source_image 失效（源文件更名/移动）时，用 sheet_uid 反查
# uid 缓存（ResourceUID.get_id_path）拿当前有效路径；找不到返回 ""。
func _resolve_source_path(d: SpriteSplitterData) -> String:
	if d == null:
		return ""
	var p: String = d.source_image
	if p.begins_with("res://") and ResourceLoader.exists(p):
		return p
	if not d.sheet_uid.is_empty():
		var id: int = ResourceUID.text_to_id(d.sheet_uid)
		if id != ResourceUID.INVALID_ID:
			var p2: String = ResourceUID.get_id_path(id)
			if p2.begins_with("res://") and ResourceLoader.exists(p2):
				return p2
	return ""


# 源纹理同步：按 data.source_image 存导入纹理引用（可序列化进 .tres），
# 注册表缩略图直接用 source_texture，免每次从路径 load+缩放。
# 外部素材 / 内存处理图（remove_background 后）不存——动态 ImageTexture 无法内联 .tres。
func _sync_source_texture() -> void:
	if data == null:
		return
	var src: String = data.source_image
	if src.begins_with("res://") and ResourceLoader.exists(src):
		data.source_texture = load(src)
	else:
		data.source_texture = null


# 按素材 uid 查找 res://sps_data/<uid>.tres；有则加载恢复，无则初始化（自动填项目名）并落盘
func _load_or_create_data() -> void:
	data = null
	data_path = ""
	if not image_res_path.begins_with("res://"):
		data_path_changed.emit("")   # 外部素材：无关联项目数据 → 注册表取消选中
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
		data.out_dir = default_out_dir(tag)   # 默认导出目录 = 输出根目录/<uid 子目录>（根目录可在项目设置配置）
		data.modified_at = int(Time.get_unix_time_from_system())
		DirAccess.make_dir_recursive_absolute(data_dir)
		ResourceSaver.save(data, data_path)
		_register_data(data_path)   # 新增项目数据自动注册
	_sync_source_path()
	_sync_source_texture()
	data_path_changed.emit(data_path)
	data_loaded.emit(data)
	is_dirty = false   # 新素材/恢复数据后无未保存修改
	data_dirty_changed.emit(false)


# 标记有未保存修改（参数/区域/项目名/导出位置变化时由 UI 调用）。
# 同时调度自动保存（防抖：已 dirty 也重置 deadline，连续修改合并为一次保存）。
func mark_dirty() -> void:
	if not is_dirty:
		is_dirty = true
		data_dirty_changed.emit(true)
	_schedule_autosave()


func _mark_clean() -> void:
	if not is_dirty:
		return
	is_dirty = false
	data_dirty_changed.emit(false)


# ---------- 自动保存（防抖限流 + 保存锁） ----------

# side 参数变化同步：参数最新值进 controller（手动/自动保存共用 _save_pending），
# 保证自动保存带上用户最新修改的参数；内部 mark_dirty 触发调度。
func sync_save_params(project_name: String, options: Dictionary, out_dir: String,
		export_mode: int) -> void:
	_save_pending = {
		"project_name": project_name,
		"options": options,
		"out_dir": out_dir,
		"export_mode": export_mode,
	}
	mark_dirty()


# 防抖调度：记录 deadline；循环协程（单例）等到期后保存。
# 挂起期间（数据迁移流程）不调度；保存成功/数据失效会清 deadline。
func _schedule_autosave() -> void:
	if not autosave_enabled or _autosave_suspended > 0:
		return
	if data == null or data_path.is_empty():
		return
	_autosave_deadline = Time.get_ticks_msec() / 1000.0 + autosave_delay
	if _autosave_loop_running:
		return   # 已有循环在等，deadline 更新后循环自然延长等待
	_autosave_loop_running = true
	_run_autosave_loop()


func _run_autosave_loop() -> void:
	var last_sec: int = -1
	while _autosave_deadline > 0.0 and _tree != null:
		var remain: float = _autosave_deadline - Time.get_ticks_msec() / 1000.0
		if remain <= 0.0:
			break
		# 倒计时提示：每秒 emit 剩余秒数（ceil：3 → 2 → 1）
		var sec: int = int(ceil(remain))
		if sec != last_sec:
			last_sec = sec
			autosave_countdown.emit(sec)
		await _tree.create_timer(minf(remain, 1.0)).timeout   # 每秒 tick；末段 <1s 精确等待
	_autosave_loop_running = false
	if _autosave_deadline <= 0.0:
		autosave_countdown.emit(0)   # 保存已发生/被取消 → 清除倒计时提示
		return
	_autosave_deadline = -1.0
	if is_dirty and data != null and not data_path.is_empty():
		_save_now()
	autosave_countdown.emit(0)   # 到期：已保存或 dirty 已清 → 清除倒计时


# 挂起/恢复自动保存（去背景等会迁移 data_path 的流程期间暂停，避免写竞争旧路径）。
# 挂起首层同时取消进行中的倒计时（loop 到期发现 deadline<=0 不保存）。
func suspend_autosave() -> void:
	_autosave_suspended += 1
	if _autosave_suspended == 1 and _autosave_deadline > 0.0:
		_autosave_deadline = -1.0
		autosave_countdown.emit(0)


func resume_autosave() -> void:
	_autosave_suspended = maxi(0, _autosave_suspended - 1)
	if _autosave_suspended == 0 and is_dirty:
		_schedule_autosave()


# 统一保存入口（手动 save_project 与自动保存共用）：保存锁防重入，
# 保存中又标记脏 → _save_queued 保存完成后补存一次（合并重复写入）。
# p_refresh_fs=false：退出兜底场景（flush_on_exit），跳过资产扫描。
func _save_now(p_refresh_fs: bool = true) -> void:
	if _saving:
		_save_queued = true
		return
	if data == null or data_path.is_empty():
		return
	_saving = true
	autosave_state_changed.emit(true)
	_flush_save(p_refresh_fs)
	_saving = false
	autosave_state_changed.emit(false)
	if _save_queued:
		_save_queued = false
		_save_now(p_refresh_fs)   # 保存期间有新修改 → 立即补存最新状态


# 是否有未保存的修改（编辑器退出拦截提示用）。registry 无脏状态——
# 每次注册/保存立即落盘，故只反映项目数据 data 的 dirty。
func has_unsaved_changes() -> bool:
	return is_dirty


# 编辑器退出兜底保存（EditorPlugin._save_external_data 调用）：同步落盘 dirty 数据。
# 退出流程不能 await：自动保存的防抖/挂起均不生效，必须立即同步写；
# 跳过资产扫描（退出时不再触发 _refresh_filesystem）。
func flush_on_exit() -> void:
	if not is_dirty or data == null or data_path.is_empty():
		return
	if _saving:
		return   # 正在同步写盘（ResourceSaver 同步完成，本次保存即最新状态）
	_save_now(false)


# 应用外部加载的 SpriteSplitterData 配置（「打开配置」文件对话框 / 拖入 .tres）
# 统一入口：素材 → 项目数据 → 配套区域 → 参数 UI，一条链路
func apply_data(d: SpriteSplitterData, path: String) -> void:
	# 1) 素材：配置带素材且与当前图不同 → 统一走 load_image（素材缺失则保持当前图并提示）。
	#    source_image 可能因源文件更名失效 → 用 sheet_uid 反查 uid 缓存拿当前有效路径，并就地修复配置
	if not d.source_image.is_empty() and image_res_path != d.source_image:
		var src: String = _resolve_source_path(d)
		if src.is_empty():
			status_changed.emit("配置素材加载失败，保持当前图片: " + d.source_image, true)
		else:
			if load_image(src):
				d.source_image = src   # 修复失效的源路径（更名后 uid 反查）
			else:
				status_changed.emit("配置素材加载失败，保持当前图片: " + src, true)
	# 2) 项目数据以所选配置为准（load_image 内部可能按 uid 关联了其他 data，此处强制覆盖）
	data = d
	data_path = path
	data_path_changed.emit(path)   # 打开配置 → 注册表选中项同步到该路径
	# 3) 区域：配置 sprites 与当前图配套（全部落在图像范围内）才加载，否则清空并提示
	if image != null and _sprites_fit_image(d.sprites):
		sprites = _sprites_from_any(d.sprites)
		_sync_rects()
		count_changed.emit("导入 %d 个区域（配置）" % sprites.size())
	else:
		sprites = []
		_sync_rects()
		count_changed.emit("")
		if d.sprites.is_empty():
			status_changed.emit("配置无切分区域（保存时未切分）: " + path, false)
	# 4) 恢复侧栏 UI（参数/项目名/导出位置）+ 重置脏数据
	is_dirty = false
	data_dirty_changed.emit(false)
	_sync_source_texture()
	data_loaded.emit(d)
	if not rects.is_empty():
		status_changed.emit("已加载配置: " + path, false)


# 配置区域是否全部落在当前图像范围内（配套校验，防旧素材区域画到新图上）
# 兼容 Array[Rect2i]（旧数据）与 Array[Dictionary]（复杂结构）
func _sprites_fit_image(src: Array) -> bool:
	if image == null or src.is_empty():
		return false
	var img_w: int = image.get_width()
	var img_h: int = image.get_height()
	for item: Variant in src:
		var r: Rect2i
		if item is Rect2i:
			r = item
		elif item is Dictionary:
			r = Rect2i(int(item.get("x", 0)), int(item.get("y", 0)),
					int(item.get("width", 0)), int(item.get("height", 0)))
		else:
			continue
		if r.position.x < 0 or r.position.y < 0 or r.end.x > img_w or r.end.y > img_h:
			return false
	return true


# 保存项目（手动入口，side 保存按钮）：参数 + 当前状态统一走 _save_now（锁 + 自动保存状态）。
# 参数同步进 _save_pending，自动保存复用同一参数来源。
func save_project(project_name: String, options: Dictionary, out_dir: String,
		export_mode: int) -> void:
	if data == null or data_path.is_empty():
		status_changed.emit("无关联项目数据（素材需在项目内）", true)
		return
	_save_pending = {
		"project_name": project_name,
		"options": options,
		"out_dir": out_dir,
		"export_mode": export_mode,
	}
	_save_now()


# 实际写盘：常用参数 + 导出位置 + 项目名 + 当前 rects（meta.json 兼容）到 .tres。
# 参数取 _save_pending（side 同步的最新值；未同步过则 fallback data 现值）。
# p_refresh_fs=false：退出兜底（flush_on_exit），不触发资产扫描。
func _flush_save(p_refresh_fs: bool = true) -> void:
	if data == null or data_path.is_empty():
		return
	var p: Dictionary = _save_pending
	var options: Dictionary = p.get("options", {})
	data.project_name = String(p.get("project_name", data.project_name))
	data.mode = String(options.get("mode", data.mode))
	data.min_width = int(options.get("min_width", data.min_width))
	data.min_height = int(options.get("min_height", data.min_height))
	data.grid_cell_size = int(options.get("grid_cell_size", data.grid_cell_size))
	data.grid_cell_w = int(options.get("grid_cell_w", data.grid_cell_w))
	data.grid_cell_h = int(options.get("grid_cell_h", data.grid_cell_h))
	data.merge_distance = int(options.get("merge_distance", data.merge_distance))
	data.alpha_threshold = int(options.get("alpha_threshold", data.alpha_threshold))
	data.slice_policy = String(options.get("slice_policy", data.slice_policy))
	data.padding = int(options.get("padding", data.padding))
	data.background_threshold = int(options.get("background_threshold",
			data.background_threshold))
	data.background_shrink = int(options.get("shrink", data.background_shrink))
	data.background_feather = int(options.get("feather", data.background_feather))
	data.background_backend = String(options.get("background_backend", data.background_backend))
	data.use_bg_color = bool(options.get("use_bg_color", data.use_bg_color))
	data.bg_color = options.get("bg_color", data.bg_color)
	data.bg_url = String(options.get("bg_url", data.bg_url))
	data.out_dir = String(p.get("out_dir", data.out_dir))
	data.export_mode = int(p.get("export_mode", data.export_mode))
	data.sprites = sprites.duplicate()   # 复杂结构（旧 Array[Rect2i] 数据读取时自动转换）→ 配置与当前状态一致
	data.modified_at = int(Time.get_unix_time_from_system())   # 最后修改时间
	_sync_source_texture()   # 保存前同步源纹理（配置来源/内存图场景兜底）
	var err: Error = ResourceSaver.save(data, data_path)
	if err == OK:
		_autosave_deadline = -1.0   # 保存完成 → 取消待保存（防抖循环随之结束）
		status_changed.emit("项目已保存 → " + data_path, false)
		_register_data(data_path)   # 保存自动注册（含已有条目刷新列表）
		_mark_clean()
		data_saved.emit()
		if p_refresh_fs:
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
	sprites = []
	_sync_rects()   # rects 重建（空）+ 双信号
	data = null
	data_path = ""
	is_dirty = false
	auto_diag = {}
	auto_diag_changed.emit({})
	image_loaded.emit(null)   # 画布清空（tex=null）
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
	var diag: Dictionary = splitter.split_detailed(image, options)
	var arr: Variant = diag.get("rects", [])
	var collected: Array[Rect2i] = []
	for r: Variant in arr:
		if r is Rect2i:
			collected.append(r)
	sprites = _sprites_from_any(collected)   # 复杂结构（uid/name 自动生成）
	auto_diag = {}
	for key: String in ["auto_mode", "auto_confidence", "auto_raw_components",
			"auto_filtered_components", "auto_merged_components", "auto_grid_columns",
			"auto_grid_rows", "auto_grid_cell_w", "auto_grid_cell_h",
			"auto_grid_offset_x", "auto_grid_offset_y", "auto_occupied_cells",
			"auto_cells_with_multi"]:
		if diag.has(key):
			auto_diag[key] = diag[key]
	auto_diag_changed.emit(auto_diag)
	if data != null:
		data.sprites = sprites.duplicate()   # 内存同步（复杂结构），保存时落盘
		mark_dirty()
	_sync_rects()
	# 状态文案：auto 模式带决策策略，其余保持原样
	var suffix: String = ""
	if int(auto_diag.get("auto_mode", -1)) >= 0:
		var mode_name: String = _auto_mode_name(int(auto_diag.get("auto_mode", 0)))
		suffix = "（%s）" % mode_name
	count_changed.emit("切分结果: %d 个精灵%s" % [sprites.size(), suffix])
	status_changed.emit("切分完成: %d 个精灵%s" % [sprites.size(), suffix], sprites.is_empty())


func _auto_mode_name(mode: int) -> String:
	if mode == 1:
		return "网格单元"
	if mode == 2:
		return "物体边界（网格内）"
	return "物体边界"


# ---------- 去背景（独立操作，替换当前图） ----------

# 去背景：处理图写盘为新 PNG（res://out_sprites/<原名>_transparent.png，与 CLI 同名），
# 并更新项目数据源（source_image / source_texture / sheet_uid）与 image_res_path。
# backend：color（纯算法，可用手动吸色 bg_color）| remote（HTTP AI 服务，bg_url）。
# 编辑器模式：触发扫描导入 → 等新 PNG 的 uid 生成 → 更新 uid/纹理 + 迁移 data_path；
# 运行模式（无导入流程）：仅更新路径，uid/纹理留待编辑器扫描后生效。
func remove_background(threshold: int, backend: String, use_bg_color: bool,
		bg_color: Color, bg_url: String, shrink: int = 0, feather: int = 0) -> void:
	if image == null:
		status_changed.emit("先打开素材表", true)
		return
	suspend_autosave()   # 数据源迁移期间暂停自动保存（避免写竞争旧 data_path）
	var remote_hint: String = "（remote AI 推理可能较慢）" if backend == "remote" else ""
	status_changed.emit("去背景中（%s）%s..." % [backend, remote_hint], false)
	await _wait_frame()
	var opts: Dictionary = {
		"background_threshold": threshold,
		"backend": backend,
		"shrink": shrink,
		"feather": feather,
	}
	if backend == "color":
		if use_bg_color:
			opts["use_bg_color"] = true
			opts["bg_color"] = bg_color
	elif not bg_url.strip_edges().is_empty():
		opts["bg_url"] = bg_url.strip_edges()
	var out: Image = splitter.remove_background(image, opts)
	if out == null:
		resume_autosave()   # 失败路径：恢复自动保存
		status_changed.emit("去背景失败（图像格式不支持）", true)
		return
	image = out
	var new_path: String = _write_transparent_png(out)
	image_res_path = new_path   # 新 PNG 成为当前素材（可 uid 关联、AtlasTexture 可用；写盘失败为 "" 回退内存图）
	rects = []
	selection = Rect2i(-1, -1, 0, 0)
	sprites = []
	if data != null:
		data.sprites = []   # 图已变，旧切片失效
		mark_dirty()
	if not new_path.is_empty():
		await _commit_bg_source(new_path)
	auto_diag = {}
	auto_diag_changed.emit({})
	image_loaded.emit(ImageTexture.create_from_image(out))
	_sync_rects()   # 图已变：清空画布红框与切片列表
	count_changed.emit("")
	status_changed.emit("已去背景（%s，背景透明）" % backend, false)
	resume_autosave()   # 数据源迁移完成 → 恢复自动保存（dirty 则补调度）


# 去背景图写盘：<输出根目录>/<原名stem>_transparent.png（与 CLI remove-background 一致）
func _write_transparent_png(img: Image) -> String:
	var stem: String = image_name.get_basename() if not image_name.is_empty() else "sprite"
	var dir: String = get_out_root()
	DirAccess.make_dir_recursive_absolute(dir)
	var p: String = dir + "/" + stem + "_transparent.png"
	if img.save_png(p) != OK:
		status_changed.emit("去背景图写盘失败，保持内存图", true)
		return ""
	register_output_file(p, SpriteOutputRegistry.KIND_PNG)
	return p


# 去背景后更新项目数据源。编辑器模式：扫描导入新 PNG → 等 uid 生成 → 更新 uid/纹理 + 迁移 data_path。
func _commit_bg_source(new_path: String) -> void:
	if data == null:
		return
	data.source_image = new_path
	_sync_source_texture()   # 已导入则直接生效；未导入 load 失败自动置 null
	if not Engine.is_editor_hint():
		return   # 运行模式：无导入流程，uid/纹理留待编辑器
	_refresh_filesystem()   # 触发编辑器扫描导入新 PNG
	var uid_text: String = await _wait_uid_ready(new_path)
	if uid_text.is_empty():
		status_changed.emit("去背景图导入超时，路径已更新（uid 待下次扫描）", true)
		return
	data.sheet_uid = uid_text
	data.source_texture = load(new_path)
	_migrate_data_path(new_path, uid_text)


# 轮询等待新 PNG 导入完成（.uid 文件出现 / uid 缓存可查），返回 uid 文本；超时返回 ""。
func _wait_uid_ready(path: String, max_frames: int = 180) -> String:
	var uid_file: String = path + ".uid"
	for _i: int in max_frames:
		if FileAccess.file_exists(uid_file):
			var text: String = FileAccess.get_file_as_string(uid_file).strip_edges()
			if text.begins_with("uid://"):
				return text
		var id: int = ResourceLoader.get_resource_uid(path)
		if id != ResourceUID.INVALID_ID:
			return ResourceUID.id_to_text(id)
		await _wait_frame()
	return ""


# uid 更新后迁移项目数据文件：<旧uid>.tres → <新uid>.tres（_load_or_create_data 按 uid 推文件名），
# 同步更新注册表与选中项；旧文件删除。
func _migrate_data_path(new_path: String, uid_text: String) -> void:
	var tag: String = uid_text.trim_prefix("uid://")
	var new_data_path: String = data_dir + "/" + tag + ".tres"
	var old_path: String = data_path
	data_path = new_data_path
	if registry != null and registry.entries.has(old_path):
		registry.entries.erase(old_path)   # 旧条目（文件将删）移除
	_register_data(new_data_path)   # 登记新条目 + 保存 registry + 刷新列表
	ResourceSaver.save(data, new_data_path)   # 立即落盘，保证 uid 关联一致
	if old_path != new_data_path and not old_path.is_empty() \
			and ResourceLoader.exists(old_path):
		DirAccess.remove_absolute(old_path)
	data_path_changed.emit(new_data_path)   # 注册表选中项跟随新路径


# ---------- 画布事件转发（主视图 → 侧栏状态） ----------

func set_crop_rect(rect_world: Rect2i) -> void:
	if rect_world.size.x <= 0 or rect_world.size.y <= 0:
		selection = Rect2i(-1, -1, 0, 0)
		status_changed.emit("已清除裁切框", false)
		return
	selection = rect_world
	status_changed.emit("裁切区域 %dx%d @ (%d,%d)" % [selection.size.x, selection.size.y,
			selection.position.x, selection.position.y], false)


# 画布选中 → 写入切片数据 selected 字段（列表 emoji 渲染 + 画布高亮显示统一走数据）。
# 选中是 UI 状态：不 mark_dirty（不算项目修改）、不 _sync_rects（避免画布交互态被重建干扰）。
func on_canvas_selection(selected: Array[Rect2i]) -> void:
	status_changed.emit("选中 %d 个精灵" % selected.size(), false)
	var selected_set: Dictionary = {}
	for r: Rect2i in selected:
		selected_set[r] = true
	var changed: bool = false
	for i: int in sprites.size():
		var want: bool = selected_set.has(_rect_of(sprites[i]))
		if bool(sprites[i].get("selected", false)) != want:
			sprites[i]["selected"] = want
			changed = true
	if changed:
		sprites_changed.emit(sprites)


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
		dir = get_out_root()
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


# 导出选中切片（画布 SELECT 框选 / 列表多选，sprites[i].selected 数据驱动）：
# 单选/多选均支持；ignored（导出忽略）项排除；锁定项不可选自然不参与。
# 跟随导出模式：PNG 逐片导出 / meta.json（meta_selected.json）/ AtlasTexture（atlas_sel_NN.tres）。
func export_selected(out_dir: String, mode: int = EXPORT_PNG) -> void:
	if exporting:
		return
	if image == null:
		status_changed.emit("先打开素材表", true)
		return
	var selected_rects: Array[Rect2i] = []
	for s: Dictionary in sprites:
		if bool(s.get("selected", false)) and not bool(s.get("ignored", false)):
			selected_rects.append(_rect_of(s))
	if selected_rects.is_empty():
		status_changed.emit("先选中一个或多个切片（画布框选/列表选择）再导出", true)
		return
	exporting = true
	exporting_changed.emit(true)
	var dir: String = out_dir.strip_edges()
	if dir.is_empty():
		dir = get_out_root()
	DirAccess.make_dir_recursive_absolute(dir)
	match mode:
		EXPORT_PNG:
			await _export_selected_png(dir, selected_rects)
		EXPORT_TRES:
			await _export_selected_tres(dir, selected_rects)
		_:
			# meta.json 模式不支持导出选中（UI 层已禁用按钮，此处兜底提示）
			status_changed.emit("meta.json 模式不支持导出选中（仅 PNG / AtlasTexture）", true)
	exporting = false   # 子函数内部已各自让帧（写盘完成），此处立即复位防连调被挡
	exporting_changed.emit(false)


# 导出选中 PNG：<原名>_<序号>_<x>_<y>.png（序号防同位置重名）
func _export_selected_png(dir: String, rects: Array[Rect2i]) -> void:
	var stem: String = image_name.get_basename()
	var ok: int = 0
	for i: int in rects.size():
		var r: Rect2i = rects[i]
		var out_path: String = "%s/%s_%02d_%d_%d.png" % [dir, stem, i + 1,
				r.position.x, r.position.y]
		if splitter.export_sprite(image, r, out_path) == OK:
			ok += 1
			register_output_file(out_path, SpriteOutputRegistry.KIND_PNG)
	await _wait_frame()
	status_changed.emit("导出选中 %d/%d 个 PNG → %s" % [ok, rects.size(),
			dir], ok != rects.size())
	_refresh_filesystem()


# 导出选中 AtlasTexture：atlas_sel_NN.tres（需项目内素材，动态 ImageTexture 无法内联）
func _export_selected_tres(dir: String, rects: Array[Rect2i]) -> void:
	if image_res_path.is_empty():
		status_changed.emit("AtlasTexture 需要项目内素材（当前文件在项目外）", true)
		return
	var atlas: Texture2D = load(image_res_path)
	if atlas == null:
		status_changed.emit("无法加载导入纹理: " + image_res_path, true)
		return
	var saved: int = 0
	for i: int in rects.size():
		var at: AtlasTexture = AtlasTexture.new()
		at.atlas = atlas
		at.region = rects[i]
		var p: String = "%s/atlas_sel_%02d.tres" % [dir, i + 1]
		if ResourceSaver.save(at, p) == OK:
			saved += 1
			register_output_file(p, SpriteOutputRegistry.KIND_TRES)
		if i % 10 == 9:
			await _wait_frame()
	status_changed.emit("选中 AtlasTexture .tres ×%d → %s" % [saved, dir],
			saved != rects.size())
	if saved > 0:
		_refresh_filesystem()


func _export_png(out_dir: String, options: Dictionary) -> void:
	var files: Array = splitter.split_and_export(image, options, out_dir)
	await _wait_frame()
	var ok: bool = true
	for f: Variant in files:
		if not FileAccess.file_exists(String(f)):
			ok = false
		else:
			register_output_file(String(f), SpriteOutputRegistry.KIND_PNG)
	status_changed.emit("导出 %d 个 PNG → %s" % [files.size(), out_dir], not ok)
	_refresh_filesystem()


func _export_meta(out_dir: String) -> void:
	var meta_path: String = out_dir + "/meta.json"
	var err: Error = splitter.export_metadata(image, get_export_rects(), image_name, meta_path)
	await _wait_frame()
	status_changed.emit("meta.json → %s (err=%d)" % [meta_path, int(err)], err != OK)
	if err == OK:
		register_output_file(meta_path, SpriteOutputRegistry.KIND_META)
		_refresh_filesystem()


func _export_tres(out_dir: String) -> void:
	if image_res_path.is_empty():
		status_changed.emit("AtlasTexture 需要项目内素材（当前文件在项目外）", true)
		return
	var atlas: Texture2D = load(image_res_path)
	if atlas == null:
		status_changed.emit("无法加载导入纹理: " + image_res_path, true)
		return
	var rs: Array[Rect2i] = get_export_rects()   # 复制：循环内 await 让出帧，数据可能被切分/导入清空；忽略 ignored 项
	var saved: int = 0
	for i: int in rs.size():
		var at: AtlasTexture = AtlasTexture.new()
		at.atlas = atlas
		at.region = rs[i]
		var p: String = "%s/atlas_%02d.tres" % [out_dir, i + 1]
		if ResourceSaver.save(at, p) == OK:
			saved += 1
			register_output_file(p, SpriteOutputRegistry.KIND_TRES)
		if i % 10 == 9:
			await _wait_frame()
	status_changed.emit("AtlasTexture .tres ×%d → %s" % [saved, out_dir], saved != rs.size())
	if saved > 0:
		_refresh_filesystem()


# 编辑器模式下触发资产库（FileSystem dock）重扫；运行模式直接返回
func _refresh_filesystem() -> void:
	if not Engine.is_editor_hint():
		return
	var fs: EditorFileSystem = EditorInterface.get_resource_filesystem()
	fs.scan_sources()


# ---------- Sheet 重排（SpriteSheetBuilder，独立于 export() 模式分发；对齐 CLI sheet） ----------

# 内存预览：当前切片 → 规整网格 sheet（不落盘）。
# cell_w/cell_h > 0 时固定格尺寸（精灵居中，超格裁剪）；0 = 自适应（最大精灵 + 2*padding）。
# 返回 {sheet: Image, rects: Array[Rect2i], clipped: int}；无图/无切片/参数非法 → 空字典 + status。
func build_sheet_preview(cols: int, padding: int, cell_w: int = 0, cell_h: int = 0) -> Dictionary:
	if image == null:
		status_changed.emit("先打开素材表", true)
		return {}
	var rects_use: Array[Rect2i] = get_export_rects()
	if rects_use.is_empty():
		status_changed.emit("先切分再重排 Sheet（当前无切片）", true)
		return {}
	var result: Dictionary = sheet_builder.build(image, rects_use, cols, padding, cell_w, cell_h)
	if result.is_empty():
		status_changed.emit("Sheet 预览失败（检查列数/间距参数）", true)
	return result


# 导出 Sheet：<out_dir>/<stem>.png + <stem>_meta.json（src/dst 映射，与 CLI sheet 命令一致）。
# cell_w/cell_h > 0 固定格（超格裁剪，返回 clipped 计数）；0 = 自适应。
# file_stem 空 = 自动用 <源名>_sheet；overwrite=false = 同名自动递增另存为（_2、_3…）。
# 返回 {sheet_path, sheet_meta_path, width, height, count, clipped}；失败空字典。
func export_sheet(cols: int, padding: int, out_dir: String, cell_w: int = 0, cell_h: int = 0,
		file_stem: String = "", overwrite: bool = true) -> Dictionary:
	if image == null:
		status_changed.emit("先打开素材表", true)
		return {}
	var rects_use: Array[Rect2i] = get_export_rects()
	if rects_use.is_empty():
		status_changed.emit("先切分再导出 Sheet（当前无切片）", true)
		return {}
	var stem: String = file_stem.strip_edges()
	if stem.is_empty() and not image_name.is_empty():
		stem = image_name.get_basename() + "_sheet"   # 源唯一命名：<源名>_sheet
	status_changed.emit("Sheet 导出中…", false)
	var result: Dictionary = sheet_builder.save_sheet(image, rects_use, cols, padding,
			out_dir, cell_w, cell_h, stem, overwrite)
	if result.is_empty():
		status_changed.emit("Sheet 导出失败", true)
	else:
		var clip_note: String = ""
		var clipped: int = int(result.get("clipped", 0))
		if clipped > 0:
			clip_note = "（%d 个精灵超出格子，已裁剪）" % clipped
		status_changed.emit("Sheet 已导出: %s（%dx%d，%d 个精灵%s）" % [
			result.get("sheet_path", ""), result.get("width", 0),
			result.get("height", 0), result.get("count", 0), clip_note], false)
		_register_sheet_outputs(result)
		_refresh_filesystem()   # 编辑器资产库重扫（新 PNG / json 可见）
	return result


# ---------- Sheet 从多张小图组装（不依赖源图/rects） ----------

# 加载多张 PNG 小图 → Array[Image]；任一失败返回空并提示
func _load_sheet_images(files: Array) -> Array[Image]:
	var images: Array[Image] = []
	for f: String in files:
		var im: Image = Image.load_from_file(f)
		if im == null:
			status_changed.emit("图片加载失败: " + f, true)
			return []
		images.append(im)
	return images


# 内存预览：多张小图 → 规整网格 sheet（不落盘）。参数与 build_sheet_preview 一致。
func build_sheet_from_files(files: Array, cols: int, padding: int,
		cell_w: int = 0, cell_h: int = 0) -> Dictionary:
	if files.is_empty():
		status_changed.emit("先选择图片文件", true)
		return {}
	var images: Array[Image] = _load_sheet_images(files)
	if images.is_empty():
		return {}
	var result: Dictionary = sheet_builder.build_from_images(images, cols, padding, cell_w, cell_h)
	if result.is_empty():
		status_changed.emit("Sheet 预览失败（检查列数/间距参数）", true)
	return result


# 导出：多张小图 → <out_dir>/<stem>.png + <stem>_meta.json（src_files 记录源路径）。
func export_sheet_from_files(files: Array, cols: int, padding: int, out_dir: String,
		cell_w: int = 0, cell_h: int = 0, file_stem: String = "",
		overwrite: bool = true) -> Dictionary:
	if files.is_empty():
		status_changed.emit("先选择图片文件", true)
		return {}
	var images: Array[Image] = _load_sheet_images(files)
	if images.is_empty():
		return {}
	status_changed.emit("Sheet 导出中…", false)
	var result: Dictionary = sheet_builder.save_from_images(images, cols, padding, out_dir,
			cell_w, cell_h, file_stem.strip_edges(), overwrite, files)
	if result.is_empty():
		status_changed.emit("Sheet 导出失败", true)
	else:
		var clip_note: String = ""
		var clipped: int = int(result.get("clipped", 0))
		if clipped > 0:
			clip_note = "（%d 个精灵超出格子，已裁剪）" % clipped
		status_changed.emit("Sheet 已导出: %s（%dx%d，%d 个精灵%s）" % [
			result.get("sheet_path", ""), result.get("width", 0),
			result.get("height", 0), result.get("count", 0), clip_note], false)
		_register_sheet_outputs(result)
		_refresh_filesystem()
	return result


# sheet 导出成功后登记产物（sheet PNG + sheet meta.json）到生成资源注册表
func _register_sheet_outputs(result: Dictionary) -> void:
	var sheet_path: String = String(result.get("sheet_path", ""))
	if not sheet_path.is_empty():
		register_output_file(sheet_path, SpriteOutputRegistry.KIND_SHEET)
	var sheet_meta: String = String(result.get("sheet_meta_path", ""))
	if not sheet_meta.is_empty():
		register_output_file(sheet_meta, SpriteOutputRegistry.KIND_SHEET)


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
	sprites = _sprites_from_any(imported)
	if data != null:
		data.sprites = sprites.duplicate()   # 导入 meta.json → 同步项目数据（复杂结构）
		mark_dirty()
	_sync_rects()
	count_changed.emit("导入 %d 个区域（meta.json）" % sprites.size())
	status_changed.emit("已导入区域: " + path, false)
