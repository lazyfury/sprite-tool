extends Control

## Sprite Splitter 插件 UI —— 独立场景版（M5.1，不挂载编辑器）。
## 运行方式：编辑器选中本场景直接运行（F6），或命令行：
##   Godot --headless --path . res://addons/sprite_splitter/ui/sprite_splitter_ui.tscn
## 无头自动测试：环境变量 SPS_UI_TEST=1 或用户参数 --sps-ui-test → 跑全链路（分析/切分/
## PNG/meta/AtlasTexture 导出）并退出。
##
## 编码约定（项目强制）：
##   1) 节点路径一律用 / 表示子层级（get_node("Main/Content/SidePanel/Side/SplitBtn")）
##   2) var 一律显式类型标注，不用 :=（const 因语言限制除外）

const DEFAULT_SHEET: String = "res://sprites/sheet.png"
const AUTO_TEST_FLAG: String = "--sps-ui-test"
const MODE_AUTO: String = "auto"
const MODE_COMPONENTS: String = "components"
const MODE_GRID: String = "grid"
const EXPORT_PNG: int = 0
const EXPORT_META: int = 1
const EXPORT_TRES: int = 2

var _splitter: SpriteSplitter = null
var _image: Image = null
var _image_name: String = ""
var _image_res_path: String = ""   # 若素材在项目内，可转 res:// 供 AtlasTexture 引用
var _rects: Array[Rect2i] = []
var _exporting: bool = false
var _fail: bool = false
var _dialog: FileDialog = null

@onready var _file_button: Button = get_node("Main/TopBar/FileButton")
@onready var _file_label: Label = get_node("Main/TopBar/FileLabel")
@onready var _texture: TextureRect = get_node("Main/Content/PreviewPanel/Preview/Texture")
@onready var _overlay: Control = get_node("Main/Content/PreviewPanel/Preview/Overlay")
@onready var _mode_option: OptionButton = get_node("Main/Content/SidePanel/Side/ModeOption")
@onready var _min_w: SpinBox = get_node("Main/Content/SidePanel/Side/ParamGrid/MinW")
@onready var _min_h: SpinBox = get_node("Main/Content/SidePanel/Side/ParamGrid/MinH")
@onready var _cell_size: SpinBox = get_node("Main/Content/SidePanel/Side/ParamGrid/CellSize")
@onready var _merge_dist: SpinBox = get_node("Main/Content/SidePanel/Side/ParamGrid/MergeDist")
@onready var _alpha_thr: SpinBox = get_node("Main/Content/SidePanel/Side/ParamGrid/AlphaThr")
@onready var _bg_remove: CheckButton = get_node("Main/Content/SidePanel/Side/BgRemove")
@onready var _analyze_btn: Button = get_node("Main/Content/SidePanel/Side/AnalyzeBtn")
@onready var _info_label: Label = get_node("Main/Content/SidePanel/Side/InfoLabel")
@onready var _split_btn: Button = get_node("Main/Content/SidePanel/Side/SplitBtn")
@onready var _count_label: Label = get_node("Main/Content/SidePanel/Side/CountLabel")
@onready var _export_mode_option: OptionButton = get_node("Main/BottomBar/ExportModeOption")
@onready var _out_dir: LineEdit = get_node("Main/BottomBar/OutDir")
@onready var _export_btn: Button = get_node("Main/BottomBar/ExportBtn")
@onready var _status_label: Label = get_node("Main/BottomBar/StatusLabel")


func _ready() -> void:
	_splitter = SpriteSplitter.new()
	_connect_signals()
	_update_mode_sensitivity()
	_selftest()
	if _auto_test_requested():
		_auto_test()


func _check(cond: bool, msg: String) -> void:
	if cond:
		print("[sps-ui] PASS: ", msg)
	else:
		printerr("[sps-ui] FAIL: ", msg)
		_fail = true


func _connect_signals() -> void:
	_file_button.pressed.connect(_on_file_button)
	_analyze_btn.pressed.connect(_on_analyze)
	_split_btn.pressed.connect(_on_split)
	_export_btn.pressed.connect(_on_export)
	_mode_option.item_selected.connect(_on_mode_changed)
	_bg_remove.toggled.connect(_on_param_changed)
	_min_w.value_changed.connect(_on_param_changed)
	_min_h.value_changed.connect(_on_param_changed)
	_cell_size.value_changed.connect(_on_param_changed)
	_merge_dist.value_changed.connect(_on_param_changed)
	_alpha_thr.value_changed.connect(_on_param_changed)


# ---------- 输入 ----------

func _on_file_button() -> void:
	if _dialog == null:
		_dialog = FileDialog.new()
		_dialog.access = FileDialog.ACCESS_FILESYSTEM
		_dialog.file_mode = FileDialog.FILE_MODE_OPEN_FILE
		_dialog.filters = PackedStringArray(["*.png, *.jpg, *.jpeg ; Image files"])
		_dialog.file_selected.connect(_on_file_selected)
		add_child(_dialog)
	_dialog.popup_centered_ratio(0.7)


func _on_file_selected(path: String) -> void:
	await _load_image(path)


func _load_image(path: String) -> void:
	var img: Image = Image.load_from_file(path)
	if img == null:
		_set_status("加载失败: " + path, true)
		return
	_image = img
	_image_name = path.get_file()
	_image_res_path = ProjectSettings.localize_path(path)
	if not _image_res_path.begins_with("res://"):
		_image_res_path = ""   # 项目外素材：AtlasTexture .tres 导出不可用
	_file_label.text = "%s (%dx%d)" % [path, img.get_width(), img.get_height()]
	_texture.texture = ImageTexture.create_from_image(img)
	_invalidate_rects()
	_info_label.text = ""
	_set_status("已加载 " + _image_name, false)
	_update_preview_view()


func _update_preview_view() -> void:
	if _image == null:
		return
	if _texture.size.x <= 0 or _texture.size.y <= 0:
		await get_tree().process_frame   # 布局未完成时等一帧
	var img_w: float = float(_image.get_width())
	var img_h: float = float(_image.get_height())
	var tex_size: Vector2 = _texture.size
	var scale: float = min(tex_size.x / img_w, tex_size.y / img_h)
	var offset: Vector2 = (tex_size - Vector2(img_w, img_h) * scale) * 0.5
	_overlay.set_view(scale, offset)


# ---------- 分析 / 切分 ----------

func _on_analyze() -> void:
	if _image == null:
		_set_status("先打开素材表", true)
		return
	var stats: Dictionary = _splitter.analyze(_image)
	var count: int = int(stats.get("component_count", 0))
	var min_w: int = int(stats.get("suggested_min_width", 2))
	var min_h: int = int(stats.get("suggested_min_height", 2))
	var fg: float = float(stats.get("foreground_percent", 0.0))
	_info_label.text = "组件 %d | 建议 min %dx%d | 前景 %.1f%%" % [count, min_w, min_h, fg]
	_min_w.set_value_no_signal(float(min_w))
	_min_h.set_value_no_signal(float(min_h))
	_set_status("分析完成", false)


func _on_split() -> void:
	if _image == null:
		_set_status("先打开素材表", true)
		return
	var opts: Dictionary = _build_options()
	var rects: Array = _splitter.split(_image, opts)
	_rects = []
	for r: Variant in rects:
		if r is Rect2i:
			_rects.append(r)
	_overlay.set_rects(_rects)
	_count_label.text = "切分结果: %d 个精灵" % _rects.size()
	_set_status("切分完成: %d 个精灵" % _rects.size(), _rects.is_empty())


func _build_options() -> Dictionary:
	var opts: Dictionary = {
		"mode": _current_mode(),
		"min_width": int(_min_w.value),
		"min_height": int(_min_h.value),
		"alpha_threshold": int(_alpha_thr.value),
	}
	if _current_mode() == MODE_GRID:
		opts["grid_cell_size"] = int(_cell_size.value)
	if _current_mode() == MODE_COMPONENTS and int(_merge_dist.value) > 0:
		opts["merge_distance"] = int(_merge_dist.value)
	if _bg_remove.button_pressed:
		opts["remove_background"] = true
	return opts


func _current_mode() -> String:
	var modes: Array[String] = [MODE_AUTO, MODE_COMPONENTS, MODE_GRID]
	return modes[_mode_option.selected]


func _on_mode_changed(_index: int) -> void:
	_update_mode_sensitivity()
	_invalidate_rects()


func _update_mode_sensitivity() -> void:
	_cell_size.editable = _current_mode() == MODE_GRID
	_merge_dist.editable = _current_mode() == MODE_COMPONENTS


func _on_param_changed(_value: Variant) -> void:
	_invalidate_rects()


func _invalidate_rects() -> void:
	_rects = []
	_overlay.clear()
	_count_label.text = ""


# ---------- 导出 ----------

func _on_export() -> void:
	if _exporting:
		return
	if _image == null:
		_set_status("先打开素材表", true)
		return
	if _rects.is_empty():
		_set_status("先切分，再导出", true)
		return
	_exporting = true
	_export_btn.disabled = true
	await _do_export()
	_export_btn.disabled = false
	_exporting = false


func _do_export() -> void:
	var mode: int = _export_mode_option.selected
	var out_dir: String = _out_dir.text.strip_edges()
	if out_dir.is_empty():
		out_dir = "res://out_sprites/ui"
	DirAccess.make_dir_recursive_absolute(out_dir)
	match mode:
		EXPORT_PNG:
			await _export_png(out_dir)
		EXPORT_META:
			await _export_meta(out_dir)
		EXPORT_TRES:
			await _export_tres(out_dir)
	await get_tree().process_frame


func _export_png(out_dir: String) -> void:
	var files: Array = _splitter.split_and_export(_image, _build_options(), out_dir)
	await get_tree().process_frame
	var ok: bool = true
	for f: Variant in files:
		if not FileAccess.file_exists(String(f)):
			ok = false
	_set_status("导出 %d 个 PNG → %s" % [files.size(), out_dir], not ok)


func _export_meta(out_dir: String) -> void:
	var meta_path: String = out_dir + "/meta.json"
	var err: Error = _splitter.export_metadata(_image, _rects, _image_name, meta_path)
	await get_tree().process_frame
	_set_status("meta.json → %s (err=%d)" % [meta_path, int(err)], err != OK)


func _export_tres(out_dir: String) -> void:
	if _image_res_path.is_empty():
		_set_status("AtlasTexture 需要项目内素材（当前文件在项目外）", true)
		return
	var atlas: Texture2D = load(_image_res_path)
	if atlas == null:
		_set_status("无法加载导入纹理: " + _image_res_path, true)
		return
	var saved: int = 0
	for i: int in range(_rects.size()):
		var at: AtlasTexture = AtlasTexture.new()
		at.atlas = atlas
		at.region = _rects[i]
		var p: String = "%s/atlas_%02d.tres" % [out_dir, i + 1]
		if ResourceSaver.save(at, p) == OK:
			saved += 1
		if i % 10 == 9:
			await get_tree().process_frame   # 大批量时分帧
	_set_status("AtlasTexture .tres ×%d → %s" % [saved, out_dir], saved != _rects.size())


func _set_status(text: String, is_error: bool) -> void:
	_status_label.text = text
	_status_label.add_theme_color_override("font_color",
			Color(1.0, 0.4, 0.4) if is_error else Color(0.7, 1.0, 0.7))


# ---------- 无头自测 ----------

func _auto_test_requested() -> bool:
	if OS.get_environment("SPS_UI_TEST") == "1":
		return true
	return OS.get_cmdline_user_args().has(AUTO_TEST_FLAG)


func _selftest() -> void:
	print("[sps-ui] selftest: load ", DEFAULT_SHEET)
	var img: Image = Image.load_from_file(DEFAULT_SHEET)
	if img == null:
		printerr("[sps-ui] selftest FAIL: cannot load ", DEFAULT_SHEET)
		_fail = true
		return
	var rects: Array = _splitter.split(img,
			{"mode": MODE_AUTO, "min_width": 2, "min_height": 2})
	print("[sps-ui] selftest: split -> ", rects.size(), " rects (expect 64)")
	if rects.size() == 64:
		print("[sps-ui] selftest PASS")
	else:
		printerr("[sps-ui] selftest FAIL: expected 64, got ", rects.size())
		_fail = true


func _auto_test() -> void:
	print("[sps-ui] === auto test ===")
	await _load_image(DEFAULT_SHEET)
	await get_tree().process_frame
	_on_analyze()
	_on_split()
	_check(not _rects.is_empty(), "auto test: split produced rects")
	var out_dir: String = "res://out_sprites/ui_test"
	DirAccess.make_dir_recursive_absolute(out_dir)
	await _export_png(out_dir)
	await _export_meta(out_dir)
	await _export_tres(out_dir)
	var meta_path: String = out_dir + "/meta.json"
	var j: Variant = null
	if FileAccess.file_exists(meta_path):
		j = JSON.parse_string(FileAccess.get_file_as_string(meta_path))
	_check(j is Dictionary, "auto test: meta.json parses")
	if j is Dictionary:
		_check(int(j.get("sprites", []).size()) == _rects.size(),
				"auto test: meta.json sprite count matches")
	var tres_count: int = 0
	for i: int in range(_rects.size()):
		if FileAccess.file_exists("%s/atlas_%02d.tres" % [out_dir, i + 1]):
			tres_count += 1
	_check(tres_count == _rects.size(), "auto test: all atlas .tres files exist")
	print("[sps-ui] === auto test done (fail=%s) ===" % _fail)
	get_tree().quit(1 if _fail else 0)
