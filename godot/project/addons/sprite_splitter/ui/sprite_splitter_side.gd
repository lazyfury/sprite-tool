@tool
extends PanelContainer

## 侧栏（M5.3，挂编辑器 dock）：文件打开 / 切分参数 / 分析 / 去背景 / 切分 /
## 导入 meta.json / 导出 + 状态。业务逻辑在 SpsController（由 EditorPlugin 注入），
## dock ↔ 主屏幕（画布）跨区域交互由 controller 信号桥接。
##
## 编码约定（项目强制）：var 显式类型标注，不用 :=。

const MODE_AUTO: String = "auto"
const MODE_COMPONENTS: String = "components"
const MODE_GRID: String = "grid"
const MODE_LABELS: Array[String] = [
	"auto（自动检测）", "components（连通分量）", "grid（网格）"]
const SLICE_POLICY_LABELS: Array[String] = [
	"自动", "物体边界", "网格单元"]
const SLICE_POLICY_KEYS: Array[String] = [
	"auto", "components", "grid"]
const EXPORT_LABELS: Array[String] = [
	"切 PNG", "仅 meta.json", "AtlasTexture .tres"]
const BG_BACKEND_LABELS: Array[String] = [
	"color（纯算法）", "remote（AI 服务）"]
const BG_BACKEND_KEYS: Array[String] = [
	"color", "remote"]

var _controller: SpsController = null
# 打开素材对话框已移至主视图 Header；侧栏保留导入 meta.json 对话框
var _meta_dialog: Variant = null

@onready var _tabs: TabContainer = get_node("VBox/TabContainer")
@onready var _project_name_edit: LineEdit = get_node("VBox/ProjectRow/ProjectNameEdit")
@onready var _save_btn: Button = get_node("VBox/ProjectRow/SaveBtn")
@onready var _save_state_label: Label = get_node("VBox/ProjectRow/SaveStateLabel")
@onready var _analyze_btn: Button = get_node("VBox/TabContainer/SplitTab/VBox/AnalyzeCard/AnalyzeVBox/AnalyzeBody/AnalyzeBtn")
@onready var _info_label: Label = get_node("VBox/TabContainer/SplitTab/VBox/AnalyzeCard/AnalyzeVBox/AnalyzeBody/InfoLabel")
@onready var _mode_option: OptionButton = get_node("VBox/TabContainer/SplitTab/VBox/ParamCard/ParamVBox/ParamBody/ModeOption")
@onready var _min_w: SpinBox = get_node("VBox/TabContainer/SplitTab/VBox/ParamCard/ParamVBox/ParamBody/ParamGrid/MinW")
@onready var _min_h: SpinBox = get_node("VBox/TabContainer/SplitTab/VBox/ParamCard/ParamVBox/ParamBody/ParamGrid/MinH")
@onready var _cell_size: SpinBox = get_node("VBox/TabContainer/SplitTab/VBox/ParamCard/ParamVBox/ParamBody/ParamGrid/CellSize")
@onready var _merge_dist: SpinBox = get_node("VBox/TabContainer/SplitTab/VBox/ParamCard/ParamVBox/ParamBody/ParamGrid/MergeDist")
@onready var _alpha_thr: SpinBox = get_node("VBox/TabContainer/SplitTab/VBox/ParamCard/ParamVBox/ParamBody/ParamGrid/AlphaThr")
@onready var _slice_policy: OptionButton = get_node("VBox/TabContainer/SplitTab/VBox/ParamCard/ParamVBox/ParamBody/ParamGrid/SlicePolicy")
@onready var _padding: SpinBox = get_node("VBox/TabContainer/SplitTab/VBox/ParamCard/ParamVBox/ParamBody/ParamGrid/Padding")
@onready var _split_btn: Button = get_node("VBox/TabContainer/SplitTab/VBox/ActionCard/ActionVBox/ActionBody/SplitBtn")
@onready var _auto_analyze: CheckButton = get_node("VBox/TabContainer/SplitTab/VBox/ActionCard/ActionVBox/ActionBody/AutoAnalyze")
@onready var _import_meta_btn: Button = get_node("VBox/TabContainer/ImportTab/VBox/ImportCard/ImportVBox/ImportMetaBtn")
@onready var _count_label: Label = get_node("VBox/TabContainer/SplitTab/VBox/ActionCard/ActionVBox/ActionBody/CountLabel")
@onready var _bg_thr: SpinBox = get_node("VBox/TabContainer/BgTab/VBox/BgCard/BgVBox/BgThrRow/BgThr")
@onready var _bg_shrink: SpinBox = get_node("VBox/TabContainer/BgTab/VBox/BgCard/BgVBox/BgShrinkRow/BgShrink")
@onready var _bg_feather: SpinBox = get_node("VBox/TabContainer/BgTab/VBox/BgCard/BgVBox/BgFeatherRow/BgFeather")
@onready var _bg_backend_option: OptionButton = get_node("VBox/TabContainer/BgTab/VBox/BgCard/BgVBox/BackendRow/BackendOption")
@onready var _bg_color_enable: CheckButton = get_node("VBox/TabContainer/BgTab/VBox/BgCard/BgVBox/BgColorRow/BgColorEnable")
@onready var _bg_color_picker: ColorPickerButton = get_node("VBox/TabContainer/BgTab/VBox/BgCard/BgVBox/BgColorRow/BgColorPicker")
@onready var _bg_url: LineEdit = get_node("VBox/TabContainer/BgTab/VBox/BgCard/BgVBox/BgUrlRow/BgUrl")
@onready var _bg_remove_btn: Button = get_node("VBox/TabContainer/BgTab/VBox/BgCard/BgVBox/BgRemoveBtn")
@onready var _export_mode_option: OptionButton = get_node("VBox/TabContainer/ExportTab/VBox/ExportCard/ExportVBox/ExportModeOption")
@onready var _out_dir: LineEdit = get_node("VBox/TabContainer/ExportTab/VBox/ExportCard/ExportVBox/OutDir")
@onready var _export_btn: Button = get_node("VBox/TabContainer/ExportTab/VBox/ExportCard/ExportVBox/ExportRow/ExportBtn")
@onready var _export_sel_btn: Button = get_node("VBox/TabContainer/ExportTab/VBox/ExportCard/ExportVBox/ExportRow/ExportSelBtn")
@onready var _status_label: Label = get_node("VBox/StatusLabel")
# 功能卡片（主题色分组样式，_apply_theme 统一设置）
@onready var _cards: Array[PanelContainer] = [
	get_node("VBox/TabContainer/SplitTab/VBox/AnalyzeCard"),
	get_node("VBox/TabContainer/SplitTab/VBox/ParamCard"),
	get_node("VBox/TabContainer/SplitTab/VBox/ActionCard"),
	get_node("VBox/TabContainer/BgTab/VBox/BgCard"),
	get_node("VBox/TabContainer/ExportTab/VBox/ExportCard"),
	get_node("VBox/TabContainer/ImportTab/VBox/ImportCard"),
]


func set_controller(c: SpsController) -> void:
	_controller = c
	if _controller == null:
		return
	_controller.status_changed.connect(_on_status)
	_controller.count_changed.connect(_on_count)
	_controller.analyze_done.connect(_on_analyze_done)
	_controller.auto_diag_changed.connect(_on_auto_diag)
	_controller.exporting_changed.connect(_on_exporting_changed)
	_controller.data_loaded.connect(_on_data_loaded)
	_controller.data_dirty_changed.connect(_on_data_dirty)
	_controller.data_saved.connect(_on_data_saved)


func _ready() -> void:
	_setup_tabs()
	_rebuild_options()
	_setup_fold_headers()
	_connect_signals()
	_apply_theme()
	if Engine.is_editor_hint():
		EditorInterface.get_base_control().theme_changed.connect(_apply_theme)


# 功能分组 tab（切分[含分析]/去背景/导出/导入；选择图片+地址在顶部 Header）
func _setup_tabs() -> void:
	_tabs.set_tab_title(0, "切分")
	_tabs.set_tab_title(1, "去背景")
	_tabs.set_tab_title(2, "导出")
	_tabs.set_tab_title(3, "导入")


# 检查器风格折叠分组（原生 Button flat + toggle）：header 点击 → body 展开/收起，
# 箭头 ▾/▸ 同步。tscn 的 header 已设 flat/toggle/左对齐，这里补箭头前缀与连接。
func _setup_fold_header(header: Button, body: Control) -> void:
	header.text = "▾ " + header.text
	header.toggled.connect(func(on: bool) -> void:
		body.visible = on
		header.text = ("▾ " if on else "▸ ") + header.text.trim_prefix("▾ ").trim_prefix("▸ ")
	)


func _setup_fold_headers() -> void:
	_setup_fold_header(
			get_node("VBox/TabContainer/SplitTab/VBox/AnalyzeCard/AnalyzeVBox/AnalyzeHeader"),
			get_node("VBox/TabContainer/SplitTab/VBox/AnalyzeCard/AnalyzeVBox/AnalyzeBody"))
	_setup_fold_header(
			get_node("VBox/TabContainer/SplitTab/VBox/ParamCard/ParamVBox/ParamHeader"),
			get_node("VBox/TabContainer/SplitTab/VBox/ParamCard/ParamVBox/ParamBody"))
	_setup_fold_header(
			get_node("VBox/TabContainer/SplitTab/VBox/ActionCard/ActionVBox/ActionHeader"),
			get_node("VBox/TabContainer/SplitTab/VBox/ActionCard/ActionVBox/ActionBody"))


# OptionButton 选项用代码重建（Godot 4.6 下 tscn item_N/text 不恢复文本）
func _rebuild_options() -> void:
	_mode_option.clear()
	for label: String in MODE_LABELS:
		_mode_option.add_item(label)
	_mode_option.select(0)
	_slice_policy.clear()
	for label: String in SLICE_POLICY_LABELS:
		_slice_policy.add_item(label)
	_slice_policy.select(0)
	_export_mode_option.clear()
	for label: String in EXPORT_LABELS:
		_export_mode_option.add_item(label)
	_export_mode_option.select(0)
	_bg_backend_option.clear()
	for label: String in BG_BACKEND_LABELS:
		_bg_backend_option.add_item(label)
	_bg_backend_option.select(0)


func _connect_signals() -> void:
	_analyze_btn.pressed.connect(_on_analyze)
	_split_btn.pressed.connect(_on_split)
	_bg_remove_btn.pressed.connect(_on_bg_remove)
	_import_meta_btn.pressed.connect(_on_import_meta)
	_export_btn.pressed.connect(_on_export)
	_export_sel_btn.pressed.connect(_on_export_selected)
	_save_btn.pressed.connect(_on_save_project)
	_mode_option.item_selected.connect(_on_mode_changed)
	# 参数/项目名/导出位置变化 → 标记未保存（脏数据）
	_project_name_edit.text_changed.connect(_on_dirty_signal)
	_out_dir.text_changed.connect(_on_dirty_signal)
	_export_mode_option.item_selected.connect(_on_dirty_signal)
	_min_w.value_changed.connect(_on_dirty_signal)
	_min_h.value_changed.connect(_on_dirty_signal)
	_cell_size.value_changed.connect(_on_dirty_signal)
	_merge_dist.value_changed.connect(_on_dirty_signal)
	_alpha_thr.value_changed.connect(_on_dirty_signal)
	_slice_policy.item_selected.connect(_on_dirty_signal)
	_padding.value_changed.connect(_on_dirty_signal)
	_bg_thr.value_changed.connect(_on_dirty_signal)
	_bg_shrink.value_changed.connect(_on_dirty_signal)
	_bg_feather.value_changed.connect(_on_dirty_signal)
	_bg_backend_option.item_selected.connect(_on_bg_backend_changed)
	_bg_color_enable.toggled.connect(_on_bg_color_enable_toggled)
	_bg_color_picker.color_changed.connect(_on_dirty_signal)
	_bg_url.text_changed.connect(_on_dirty_signal)


# ---------- 主题化（跟随编辑器主题） ----------

func _apply_theme() -> void:
	var th: Theme = null
	if Engine.is_editor_hint():
		th = EditorInterface.get_editor_theme()
	var bg: Color = _theme_color(th, "dark_color_1", Color(0.16, 0.16, 0.18))
	# 侧栏整体背景：无边框 + 大圆角（与卡片风格一致）
	var sb: StyleBoxFlat = StyleBoxFlat.new()
	sb.bg_color = bg
	sb.content_margin_left = 10.0
	sb.content_margin_top = 8.0
	sb.content_margin_right = 10.0
	sb.content_margin_bottom = 8.0
	sb.set_corner_radius_all(8)
	add_theme_stylebox_override("panel", sb)
	# 功能卡片：主题色（dark_color_2 略亮一档）+ 圆角（无边框）
	var card_bg: Color = _theme_color(th, "dark_color_2", Color(0.19, 0.19, 0.21))
	for card: PanelContainer in _cards:
		card.add_theme_stylebox_override("panel", _make_card(card_bg))


func _make_card(bg: Color) -> StyleBoxFlat:
	var card_sb: StyleBoxFlat = StyleBoxFlat.new()
	card_sb.bg_color = bg
	card_sb.set_corner_radius_all(10)
	card_sb.content_margin_left = 10.0
	card_sb.content_margin_top = 8.0
	card_sb.content_margin_right = 10.0
	card_sb.content_margin_bottom = 8.0
	return card_sb



func _theme_color(th: Theme, name: String, fallback: Color) -> Color:
	if th != null and th.has_color(name, "Editor"):
		return th.get_color(name, "Editor")
	return fallback


# ---------- 文件 / 分析 / 切分 ----------

func _make_dialog(filters: PackedStringArray, title: String) -> Variant:
	if Engine.is_editor_hint():
		var ed: EditorFileDialog = EditorFileDialog.new()
		ed.access = EditorFileDialog.ACCESS_FILESYSTEM
		ed.file_mode = EditorFileDialog.FILE_MODE_OPEN_FILE
		ed.filters = filters
		ed.title = title
		add_child(ed)
		return ed
	var fd: FileDialog = FileDialog.new()
	fd.access = FileDialog.ACCESS_FILESYSTEM
	fd.file_mode = FileDialog.FILE_MODE_OPEN_FILE
	fd.filters = filters
	fd.title = title
	add_child(fd)
	return fd


func _on_analyze() -> void:
	if _controller != null:
		_controller.analyze()


# 去背景：按钮禁用 + 状态提示（loading，remote 可能耗时数秒~分钟），完成后恢复
func _on_bg_remove() -> void:
	if _controller == null:
		return
	_bg_remove_btn.disabled = true   # loading：防重复点击（remote 调用为同步阻塞）
	await _controller.remove_background(int(_bg_thr.value),
			_current_bg_backend(), _bg_color_enable.button_pressed,
			_bg_color_picker.color, _bg_url.text,
			int(_bg_shrink.value), int(_bg_feather.value))
	_bg_remove_btn.disabled = false


func _current_bg_backend() -> String:
	return BG_BACKEND_KEYS[_bg_backend_option.selected]


# 后端切换：remote 显示 URL、禁用 color 参数（阈值/吸色/收缩/羽化）；color 反之
func _on_bg_backend_changed(_index: int) -> void:
	var is_remote: bool = _current_bg_backend() == "remote"
	_bg_url.editable = is_remote
	_bg_thr.editable = not is_remote
	_bg_shrink.editable = not is_remote
	_bg_feather.editable = not is_remote
	_bg_color_enable.disabled = is_remote
	_bg_color_picker.disabled = is_remote or not _bg_color_enable.button_pressed
	if _controller != null:
		_controller.mark_dirty()


func _on_bg_color_enable_toggled(_on: bool) -> void:
	_bg_color_picker.disabled = not _on or _current_bg_backend() == "remote"
	_on_dirty_signal()


func _on_split() -> void:
	if _controller == null:
		return
	if _auto_analyze.button_pressed:
		_on_analyze()   # 切分前自动分析填推荐参数（set_value_no_signal 不清 rects）
	_controller.split(_build_options())


func _on_mode_changed(_index: int) -> void:
	var mode: String = _current_mode()
	_cell_size.editable = mode == MODE_GRID
	_merge_dist.editable = mode == MODE_COMPONENTS
	# 切割策略 / Padding 仅 auto 模式生效（可编辑）
	_slice_policy.editable = mode == MODE_AUTO
	_padding.editable = mode == MODE_AUTO
	if _controller != null:
		_controller.mark_dirty()


func _current_mode() -> String:
	var modes: Array[String] = [MODE_AUTO, MODE_COMPONENTS, MODE_GRID]
	return modes[_mode_option.selected]


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
	if _current_mode() == MODE_AUTO:
		# 切割策略 / Padding 仅 auto 模式透传
		opts["slice_policy"] = SLICE_POLICY_KEYS[_slice_policy.selected]
		if int(_padding.value) > 0:
			opts["padding"] = int(_padding.value)
		if int(_merge_dist.value) > 0:
			opts["merge_distance"] = int(_merge_dist.value)
	# 去背景参数（save_project 持久化用；split 的 parse_options 忽略未知 key）
	opts["background_backend"] = _current_bg_backend()
	opts["use_bg_color"] = _bg_color_enable.button_pressed
	opts["bg_color"] = _bg_color_picker.color
	opts["bg_url"] = _bg_url.text
	opts["shrink"] = int(_bg_shrink.value)
	opts["feather"] = int(_bg_feather.value)
	return opts


# ---------- 导入 meta.json ----------

func _on_import_meta() -> void:
	if _controller == null:
		return
	if _controller.image == null:
		_on_status("先打开素材表，再导入区域", true)
		return
	if _meta_dialog == null:
		_meta_dialog = _make_dialog(PackedStringArray(["*.json ; meta.json"]),
				"导入 meta.json（区域）")
		_meta_dialog.file_selected.connect(_on_meta_selected)
	_meta_dialog.popup_centered_ratio(0.6)


func _on_meta_selected(path: String) -> void:
	if _controller != null:
		_controller.import_meta(path)


# ---------- 导出 ----------

func _on_export() -> void:
	if _controller != null:
		_controller.export(_export_mode_option.selected, _out_dir.text, _build_options())


func _on_export_selected() -> void:
	if _controller != null:
		_controller.export_selected(_out_dir.text)


# ---------- controller 信号 → 侧栏 UI ----------

# 加载素材时恢复关联项目数据（参数/导出/项目名）到 UI
func _on_data_loaded(data: SpriteSplitterData) -> void:
	if data.project_name != "":
		_project_name_edit.text = data.project_name
	var mode_idx: int = MODE_LABELS.find(data.mode)
	if mode_idx >= 0:
		_mode_option.select(mode_idx)
	_min_w.set_value_no_signal(float(data.min_width))
	_min_h.set_value_no_signal(float(data.min_height))
	_cell_size.set_value_no_signal(float(data.grid_cell_size))
	_merge_dist.set_value_no_signal(float(data.merge_distance))
	_alpha_thr.set_value_no_signal(float(data.alpha_threshold))
	var sp_idx: int = SLICE_POLICY_KEYS.find(data.slice_policy)
	if sp_idx >= 0:
		_slice_policy.select(sp_idx)
	_padding.set_value_no_signal(float(data.padding))
	_bg_thr.set_value_no_signal(float(data.background_threshold))
	_bg_shrink.set_value_no_signal(float(data.background_shrink))
	_bg_feather.set_value_no_signal(float(data.background_feather))
	var bg_idx: int = BG_BACKEND_KEYS.find(data.background_backend)
	if bg_idx < 0:
		bg_idx = 0
	_bg_backend_option.select(bg_idx)
	_bg_color_enable.set_pressed_no_signal(data.use_bg_color)
	_bg_color_picker.color = data.bg_color
	_bg_url.text = data.bg_url
	# 刷新 enable 态（remote 禁用 color 参数）——不调 _on_bg_backend_changed（会 mark_dirty）
	var is_remote_bg: bool = _current_bg_backend() == "remote"
	_bg_url.editable = is_remote_bg
	_bg_thr.editable = not is_remote_bg
	_bg_color_enable.disabled = is_remote_bg
	_bg_color_picker.disabled = is_remote_bg or not _bg_color_enable.button_pressed
	_out_dir.text = data.out_dir
	_export_mode_option.select(data.export_mode)
	_info_label.text = "已恢复项目: " + data.project_name


# 保存项目（参数/导出位置/项目名/rects → .tres，uid 关联）
func _on_save_project() -> void:
	if _controller != null:
		_controller.save_project(_project_name_edit.text.strip_edges(),
				_build_options(), _out_dir.text, _export_mode_option.selected)


# 任意参数/项目名/导出位置变化 → 标记脏数据（参数信号统一入口）
func _on_dirty_signal(_v: Variant = null) -> void:
	if _controller != null:
		_controller.mark_dirty()


# 脏数据状态 → 保存按钮旁状态提示
func _on_data_dirty(dirty: bool) -> void:
	if dirty:
		_save_state_label.text = "⚠ 未保存"
		_save_state_label.add_theme_color_override("font_color", Color(1.0, 0.8, 0.3))
	else:
		_save_state_label.text = ""


# 保存成功 → 短暂显示 ✅，随后恢复（除非期间又产生脏数据）
func _on_data_saved() -> void:
	_save_state_label.text = "✅ 已保存"
	_save_state_label.add_theme_color_override("font_color", Color(0.7, 1.0, 0.7))
	await get_tree().create_timer(1.8).timeout
	if _controller == null or not _controller.is_dirty:
		_save_state_label.text = ""


func _on_status(text: String, is_error: bool) -> void:
	_status_label.text = text
	_status_label.add_theme_color_override("font_color",
			Color(1.0, 0.4, 0.4) if is_error else Color(0.7, 1.0, 0.7))


func _on_count(text: String) -> void:
	_count_label.text = text


func _on_analyze_done(stats: Dictionary) -> void:
	var count: int = int(stats.get("component_count", 0))
	var min_w: int = int(stats.get("suggested_min_width", 2))
	var min_h: int = int(stats.get("suggested_min_height", 2))
	var fg: float = float(stats.get("foreground_percent", 0.0))
	_info_label.text = "组件 %d | 建议 min %dx%d | 前景 %.1f%%" % [count, min_w, min_h, fg]
	_min_w.set_value_no_signal(float(min_w))
	_min_h.set_value_no_signal(float(min_h))
	if _controller != null:
		_controller.mark_dirty()   # 推荐参数已填充，视为未保存修改


# Auto 切分诊断（controller auto_diag_changed）：InfoLabel 显示检测结果/布局/策略/置信度
func _on_auto_diag(diag: Dictionary) -> void:
	if diag.is_empty():
		return
	var mode: int = int(diag.get("auto_mode", -1))
	if mode < 0:
		return
	var comps: int = int(diag.get("auto_merged_components", 0))
	var cols: int = int(diag.get("auto_grid_columns", 0))
	var rows: int = int(diag.get("auto_grid_rows", 0))
	var cw: int = int(diag.get("auto_grid_cell_w", 0))
	var ch: int = int(diag.get("auto_grid_cell_h", 0))
	var conf: float = float(diag.get("auto_confidence", 0.0))
	var mode_name: String = _auto_mode_label(mode)
	if cols > 0 and rows > 0:
		_info_label.text = "检测到 %d 个组件 | 布局 %d×%d | Cell %d×%d\n策略 %s | 置信度 %.0f%%" % [
			comps, cols, rows, cw, ch, mode_name, conf * 100.0]
	else:
		_info_label.text = "检测到 %d 个组件 | 无网格\n策略 %s" % [comps, mode_name]


func _auto_mode_label(mode: int) -> String:
	if mode == 1:
		return "网格单元"
	if mode == 2:
		return "物体边界（网格内）"
	return "物体边界"


func _on_exporting_changed(exporting: bool) -> void:
	_export_btn.disabled = exporting
	_export_sel_btn.disabled = exporting
