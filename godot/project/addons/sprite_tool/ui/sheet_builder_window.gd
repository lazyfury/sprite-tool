@tool
extends Window

## 创建 Sheets 独立窗口（M5.x）：从多张已切分小图组装 sprite sheet。
## 与 Sheet tab（源图 + rects 重排）解耦——本窗口专注「多图文件组装」，
## 复用 SpsController 的 build_sheet_from_files / export_sheet_from_files。
##
## 编码约定（项目强制）：var 显式类型标注，不用 :=。

const FILE_FILTERS: PackedStringArray = ["*.png ; PNG Image"]

var _controller: SpsController = null
var _files_dialog: Variant = null   # 多选 PNG 对话框（编辑器模式挂编辑器根）
var _files: Array[String] = []      # 已选小图路径（空 = 未选）

@onready var _files_btn: Button = get_node("Scroll/VBox/MarginContainer/FilesRow/FilesBtn")
@onready var _files_label: Label = get_node("Scroll/VBox/MarginContainer/FilesRow/FilesLabel")
@onready var _reset_btn: Button = get_node("Scroll/VBox/MarginContainer/FilesRow/ResetBtn")
@onready var _source_list: ItemList = get_node("Scroll/VBox/Split/LeftPanel/LeftVBox/SourceList")
@onready var _up_btn: Button = get_node("Scroll/VBox/Split/LeftPanel/LeftVBox/OrderRow/UpBtn")
@onready var _down_btn: Button = get_node("Scroll/VBox/Split/LeftPanel/LeftVBox/OrderRow/DownBtn")
@onready var _cols: SpinBox = get_node("Scroll/VBox/Split/RightPanel/ParamCard/ParamVBox/ColsRow/Cols")
@onready var _padding: SpinBox = get_node("Scroll/VBox/Split/RightPanel/ParamCard/ParamVBox/PaddingRow/Padding")
@onready var _cell_w: SpinBox = get_node("Scroll/VBox/Split/RightPanel/ParamCard/ParamVBox/CellWRow/CellW")
@onready var _cell_h: SpinBox = get_node("Scroll/VBox/Split/RightPanel/ParamCard/ParamVBox/CellHRow/CellH")
@onready var _file_name: LineEdit = get_node("Scroll/VBox/Split/RightPanel/ParamCard/ParamVBox/FileNameRow/SheetFileName")
@onready var _random_name_btn: Button = get_node("Scroll/VBox/Split/RightPanel/ParamCard/ParamVBox/FileNameBtns/RandomNameBtn")
@onready var _overwrite_check: CheckButton = get_node("Scroll/VBox/Split/RightPanel/ParamCard/ParamVBox/OverwriteCheck")
@onready var _preview_btn: Button = get_node("Scroll/VBox/Split/RightPanel/ActionCard/ActionVBox/PreviewBtn")
@onready var _preview_tex: TextureRect = get_node("Scroll/VBox/Split/RightPanel/ActionCard/ActionVBox/Preview")
@onready var _export_btn: Button = get_node("Scroll/VBox/Split/RightPanel/ActionCard/ActionVBox/ExportBtn")
@onready var _out_dir: LineEdit = get_node("Scroll/VBox/Split/RightPanel/ActionCard/ActionVBox/OutDirRow/OutDir")
@onready var _result_label: Label = get_node("Scroll/VBox/Split/RightPanel/ActionCard/ActionVBox/ResultLabel")


func set_controller(c: SpsController) -> void:
	_controller = c
	if _controller != null:
		_controller.status_changed.connect(_on_status)


func _ready() -> void:
	close_requested.connect(_hide_self)
	visibility_changed.connect(_on_visibility_changed)
	_files_btn.pressed.connect(_on_files_btn)
	_preview_btn.pressed.connect(_on_preview)
	_export_btn.pressed.connect(_on_export)
	_up_btn.pressed.connect(_on_move_up)
	_down_btn.pressed.connect(_on_move_down)
	_random_name_btn.pressed.connect(_on_random_name_btn)
	_reset_btn.pressed.connect(_on_reset_btn)
	_files_label.text = "未选择图片"


# 打开窗口（首次显示 / 再次 popup）→ 初始化随机文件名，避免撞名。
# main 在每次 popup 前显式调用 reset_file_name()（确定链路），此处作为兜底。
func _on_visibility_changed() -> void:
	if visible:
		reset_file_name()


func reset_file_name() -> void:
	_file_name.text = _random_name()


# 🎲 随机文件名：仅重新生成随机名
func _on_random_name_btn() -> void:
	reset_file_name()
	_result_label.text = "已生成随机文件名：" + _file_name.text
	_result_label.add_theme_color_override("font_color", Color(0.7, 1.0, 0.7))


# 🔄 重新开始：清空已选素材/列表/预览/结果 → 重新生成随机文件名
func _on_reset_btn() -> void:
	_files = []
	_source_list.clear()
	_preview_tex.texture = null
	_preview_tex.visible = false
	_result_label.text = ""
	_files_label.text = "未选择图片"
	reset_file_name()


func _random_name() -> String:
	var rng: RandomNumberGenerator = RandomNumberGenerator.new()
	rng.randomize()
	var hex_digits: String = "0123456789abcdef"
	var out_dir: String = ""
	if _out_dir != null:
		out_dir = _out_dir.text.strip_edges()
	for attempt in 16:   # 最多尝试 16 次（约 2^24 空间，撞名几乎不可能连续 16 次）
		var s: String = "sheet_"
		for i in 6:
			s += hex_digits[rng.randi_range(0, 15)]
		if not _name_in_use(out_dir, s):
			return s
	# 兜底：时间戳名（仍检查冲突）
	var ts: String = "sheet_" + String.num_int64(Time.get_unix_time_from_system())
	if not _name_in_use(out_dir, ts):
		return ts
	return "sheet_" + String.num_int64(randi())


# 检查文件名是否已被占用（png / meta 任一存在即冲突）
func _name_in_use(out_dir: String, stem: String) -> bool:
	if out_dir.is_empty():
		return false   # 输出目录未知时跳过检查（导出时仍有禁止覆盖兜底）
	return FileAccess.file_exists(out_dir + "/" + stem + ".png") \
			or FileAccess.file_exists(out_dir + "/" + stem + "_meta.json")


func _hide_self() -> void:
	hide()   # 复用单例：关闭仅隐藏，下次点击直接显示


func _on_status(text: String, is_error: bool) -> void:
	_result_label.text = text
	_result_label.add_theme_color_override("font_color",
			Color(1.0, 0.4, 0.4) if is_error else Color(0.7, 1.0, 0.7))


func _on_files_btn() -> void:
	if _files_dialog == null:
		if Engine.is_editor_hint():
			var ed: EditorFileDialog = EditorFileDialog.new()
			ed.access = EditorFileDialog.ACCESS_FILESYSTEM
			ed.file_mode = EditorFileDialog.FILE_MODE_OPEN_FILES
			ed.filters = FILE_FILTERS
			ed.title = "选择精灵小图（可多选）"
			ed.files_selected.connect(_on_files_selected)
			ed.close_requested.connect(_free_files_dialog)
			EditorInterface.get_base_control().add_child(ed)   # 编辑器根（独立窗口内 dialog 显示稳定）
			_files_dialog = ed
		else:
			var fd: FileDialog = FileDialog.new()
			fd.access = FileDialog.ACCESS_FILESYSTEM
			fd.file_mode = FileDialog.FILE_MODE_OPEN_FILES
			fd.filters = FILE_FILTERS
			fd.title = "选择精灵小图（可多选）"
			fd.files_selected.connect(_on_files_selected)
			fd.close_requested.connect(_free_files_dialog)
			add_child(fd)
			_files_dialog = fd
	_files_dialog.popup_centered_ratio(0.6)


func _free_files_dialog() -> void:
	if _files_dialog != null:
		_files_dialog.queue_free()
		_files_dialog = null


func _exit_tree() -> void:
	_free_files_dialog()


func _on_files_selected(paths: PackedStringArray) -> void:
	if paths.is_empty():
		return
	var arr: Array[String] = []
	for p: String in paths:
		arr.append(p)
	_files = arr
	var first: String = _files[0].get_file()
	_files_label.text = "已选 %d 张：%s%s" % [_files.size(), first,
			" 等" if _files.size() > 1 else ""]
	_refresh_source_preview()   # 源素材缩略图预览


# 源素材预览：左侧列表（缩略图 + 文件名，fixed_icon_size 统一 48x48）
func _refresh_source_preview() -> void:
	_source_list.clear()
	for f: String in _files:
		var im: Image = Image.load_from_file(f)
		if im == null:
			continue
		_source_list.add_item(f.get_file(), ImageTexture.create_from_image(im))


# 排序：上移/下移选中素材（调整 _files 顺序 → sheet 排布顺序跟随，core 无需改动）
func _on_move_up() -> void:
	_move_selected(-1)


func _on_move_down() -> void:
	_move_selected(1)


func _move_selected(delta: int) -> void:
	var sel: Array = _source_list.get_selected_items()
	if sel.is_empty():
		_result_label.text = "先选中一个素材再调整顺序"
		_result_label.add_theme_color_override("font_color", Color(1.0, 0.4, 0.4))
		return
	var idx: int = int(sel[0])
	var target: int = idx + delta
	if target < 0 or target >= _files.size():
		return   # 已在边界
	var item: String = _files[idx]
	_files.remove_at(idx)
	_files.insert(target, item)
	_refresh_source_preview()
	_source_list.select(target)   # 保持选中（刷新后索引即新位置）


func _on_preview() -> void:
	if _controller == null:
		return
	var result: Dictionary
	if _files.is_empty():
		_result_label.text = "先选择图片文件"
		_result_label.add_theme_color_override("font_color", Color(1.0, 0.4, 0.4))
		return
	result = _controller.build_sheet_from_files(_files, int(_cols.value),
			int(_padding.value), int(_cell_w.value), int(_cell_h.value))
	if result.is_empty():
		_preview_tex.visible = false
		return
	var sheet_img: Image = result["sheet"]
	_preview_tex.texture = ImageTexture.create_from_image(sheet_img)
	_preview_tex.visible = true
	var clip_note: String = ""
	if int(result.get("clipped", 0)) > 0:
		clip_note = "  ⚠ %d 个超出格子已裁剪" % int(result.get("clipped", 0))
	_result_label.text = "预览：%dx%d，%d 个精灵（列 %d，间距 %d%s）" % [
		sheet_img.get_width(), sheet_img.get_height(), result["rects"].size(),
		int(_cols.value), int(_padding.value), clip_note]
	_result_label.add_theme_color_override("font_color", Color(0.7, 1.0, 0.7))


func _on_export() -> void:
	if _controller == null:
		return
	if _files.is_empty():
		_result_label.text = "先选择图片文件"
		_result_label.add_theme_color_override("font_color", Color(1.0, 0.4, 0.4))
		return
	var out_dir: String = _out_dir.text.strip_edges()
	var stem: String = _file_name.text.strip_edges()
	if stem.is_empty():
		stem = "sheet"   # 与 controller 默认一致：空名也参与防覆盖检查（防随机名失效时误覆盖）
	# 未勾选「另存为」= 禁止覆盖：目标已存在直接拦截，不写盘
	if not _overwrite_check.button_pressed:
		var target: String = out_dir + "/" + stem + ".png"
		if FileAccess.file_exists(target):
			_result_label.text = "⚠ 文件已存在，禁止覆盖：" + target + "（勾选「另存为」可自动加序号，或修改文件名）"
			_result_label.add_theme_color_override("font_color", Color(1.0, 0.4, 0.4))
			return
	var result: Dictionary = _controller.export_sheet_from_files(_files, int(_cols.value),
			int(_padding.value), out_dir,
			int(_cell_w.value), int(_cell_h.value),
			stem, not _overwrite_check.button_pressed)
	if not result.is_empty():
		var clip_note: String = ""
		if int(result.get("clipped", 0)) > 0:
			clip_note = "\n⚠ %d 个精灵超出格子已裁剪" % int(result.get("clipped", 0))
		_result_label.text = "已导出：\n%s\n%s（%dx%d，%d 个精灵%s）" % [
			result.get("sheet_path", ""), result.get("sheet_meta_path", ""),
			result.get("width", 0), result.get("height", 0), result.get("count", 0), clip_note]
		_result_label.add_theme_color_override("font_color", Color(0.7, 1.0, 0.7))
