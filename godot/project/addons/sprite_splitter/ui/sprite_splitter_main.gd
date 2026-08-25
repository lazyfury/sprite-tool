@tool
extends Control

## 主视图（M5.3，挂编辑器主屏幕）：全屏画布 + 底部工具条（PS 风格）。
## 业务逻辑在 SpsController（由 EditorPlugin 注入）；画布信号转发给 controller，
## controller 信号驱动画布刷新 —— 主屏幕与侧栏 dock 跨区域交互。

## 编码约定（项目强制）：var 显式类型标注，不用 :=。

var _controller: SpsController = null
var _side: Control = null   # 侧栏引用（editor_plugin 注入，切换前保存用当前参数）
const REG_THUMB_SIZE: int = 48   # 注册表缩略图边长（等比 contain 居中）
const REG_ITEM_SCENE: PackedScene = preload("res://addons/sprite_splitter/ui/reg_item.tscn")
const REG_SEP_SCENE: PackedScene = preload("res://addons/sprite_splitter/ui/reg_sep.tscn")
# 文件对话框：编辑器模式 EditorFileDialog / 运行模式 FileDialog → Variant
var _dialog: Variant = null
var _data_dialog: Variant = null
# 拖放导入：确认弹窗 + 待确认文件
var _drop_dialog: ConfirmationDialog = null
var _pending_drop_path: String = ""
# 注册表切换：脏数据确认弹窗 + 待切换条目
var _switch_dialog: ConfirmationDialog = null
var _pending_switch_path: String = ""
var _active_reg_path: String = ""                 # 当前选中/加载的项目路径（手动维护选中态）

@onready var _header: PanelContainer = get_node("MainVBox/Header")
@onready var _file_button: Button = get_node("MainVBox/Header/HeaderRow/FileButton")
@onready var _data_btn: Button = get_node("MainVBox/Header/HeaderRow/DataBtn")
@onready var _close_btn: Button = get_node("MainVBox/Header/HeaderRow/CloseBtn")
@onready var _file_label: Label = get_node("MainVBox/Header/HeaderRow/FileLabel")
@onready var _canvas: Control = get_node("%CanvasView")   # 唯一名（结构变动不失效）
@onready var _split_list: ItemList = get_node("MainVBox/HSplitContainer/Control/RightVBox/CardPanel/CardVBox/ListArea/SplitList")
@onready var _split_empty: Label = get_node("MainVBox/HSplitContainer/Control/RightVBox/CardPanel/CardVBox/ListArea/EmptyLabel")
@onready var _split_card: PanelContainer = get_node("MainVBox/HSplitContainer/Control/RightVBox/CardPanel")
@onready var _reg_card: PanelContainer = get_node("MainVBox/HSplitContainer/Control/RightVBox/RegCardPanel")
@onready var _reg_scroll: ScrollContainer = get_node("MainVBox/HSplitContainer/Control/RightVBox/RegCardPanel/RegVBox/RegListArea/RegScroll")
@onready var _reg_vbox: VBoxContainer = get_node("MainVBox/HSplitContainer/Control/RightVBox/RegCardPanel/RegVBox/RegListArea/RegScroll/RegVBoxList")
@onready var _reg_empty: Label = get_node("MainVBox/HSplitContainer/Control/RightVBox/RegCardPanel/RegVBox/RegListArea/RegEmptyLabel")
@onready var _move_btn: Button = get_node("MainVBox/ToolBarMargin/ToolBarRow/ToolBar/MoveBtn")
@onready var _select_btn: Button = get_node("MainVBox/ToolBarMargin/ToolBarRow/ToolBar/SelectBtn")
@onready var _crop_btn: Button = get_node("MainVBox/ToolBarMargin/ToolBarRow/ToolBar/CropBtn")
@onready var _zoom_out_btn: Button = get_node("MainVBox/ToolBarMargin/ToolBarRow/ZoomGroup/ZoomOutBtn")
@onready var _zoom_label: Label = get_node("MainVBox/ToolBarMargin/ToolBarRow/ZoomGroup/ZoomLabel")
@onready var _zoom_in_btn: Button = get_node("MainVBox/ToolBarMargin/ToolBarRow/ZoomGroup/ZoomInBtn")
@onready var _fit_btn: Button = get_node("MainVBox/ToolBarMargin/ToolBarRow/ZoomGroup/FitBtn")


func set_controller(c: SpsController) -> void:
	_controller = c
	if _controller == null:
		return
	_controller.connect_fs_signals()   # 编辑器模式：源文件重命名/重导入 → 修复注册表失效路径
	_controller.image_loaded.connect(_on_image_loaded)
	_controller.rects_changed.connect(_on_rects_changed)
	_controller.auto_diag_changed.connect(_on_auto_diag_changed)
	_canvas.selection_drawn.connect(_on_canvas_selection_drawn)
	_canvas.selection_changed.connect(_on_canvas_selection_changed)
	_canvas.view_changed.connect(_on_canvas_view_changed)
	_canvas.drop_requested.connect(_on_canvas_drop_requested)
	_controller.registry_updated.connect(_on_registry_updated)
	_controller.data_path_changed.connect(_on_data_path_changed)
	_on_registry_updated()   # 初始填充注册表列表


func set_side(s: Control) -> void:
	_side = s


func get_canvas_view() -> Control:
	return _canvas


func _ready() -> void:
	_setup_tool_buttons()
	_apply_theme()
	if Engine.is_editor_hint():
		EditorInterface.get_base_control().theme_changed.connect(_apply_theme)
	_file_button.pressed.connect(_on_file_button)
	_data_btn.pressed.connect(_on_open_data)
	_close_btn.pressed.connect(_on_close_image)


# ---------- Header（打开素材 + 图片地址，主题化） ----------

func _apply_theme() -> void:
	var th: Theme = null
	if Engine.is_editor_hint():
		th = EditorInterface.get_editor_theme()
	var bg: Color = _theme_color(th, "dark_color_1", Color(0.16, 0.16, 0.18))
	var sep: Color = _theme_color(th, "separator", Color(0.36, 0.36, 0.38))
	# 切片数据卡片（与侧栏卡片一致：主题色 dark_color_2 + 圆角 10 + 无边框 + padding）
	var card_bg: Color = _theme_color(th, "dark_color_2", Color(0.19, 0.19, 0.21))
	for card: PanelContainer in [_split_card, _reg_card]:
		card.add_theme_stylebox_override("panel", _make_card_style(card_bg))


func _make_card_style(bg: Color) -> StyleBoxFlat:
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


func _on_file_button() -> void:
	if _dialog == null:
		_dialog = _make_dialog(PackedStringArray(["*.png, *.jpg, *.jpeg ; Image files"]),
				"选择素材表")
		_dialog.file_selected.connect(_on_file_selected)
	_dialog.popup_centered_ratio(0.7)


func _on_file_selected(path: String) -> void:
	if _controller != null:
		_controller.load_image(path)


# 打开 SpriteSplitterData 配置：文件对话框选择 .tres → 加载应用到插件
func _on_open_data() -> void:
	if _data_dialog == null:
		_data_dialog = _make_dialog(PackedStringArray(["*.tres ; SpriteSplitterData 配置"]),
				"选择 SpriteSplitterData 配置")
		_data_dialog.file_selected.connect(_on_data_selected)
		# 默认定位到项目数据目录
		if DirAccess.dir_exists_absolute("res://sps_data"):
			_data_dialog.current_dir = "res://sps_data"
	_data_dialog.popup_centered_ratio(0.7)


func _on_data_selected(path: String) -> void:
	var d: Variant = load(path)
	if not (d is SpriteSplitterData):
		if _controller != null:
			_controller.status_changed.emit("所选文件不是 SpriteSplitterData 配置: " + path, true)
		return
	if _controller == null:
		return
	_controller.apply_data(d, path)


# ---------- 拖放导入（编辑器 FileSystem dock → 预览区画布，带确认弹窗） ----------

func _on_canvas_drop_requested(path: String) -> void:
	_pending_drop_path = path
	_show_drop_confirm(path)


func _show_drop_confirm(path: String) -> void:
	if _drop_dialog == null:
		_drop_dialog = ConfirmationDialog.new()
		_drop_dialog.title = "确认导入"
		_drop_dialog.ok_button_text = "打开"
		_drop_dialog.cancel_button_text = "取消"
		_drop_dialog.confirmed.connect(_on_drop_confirmed)
		_drop_dialog.canceled.connect(_on_drop_canceled)
		add_child(_drop_dialog)
	var is_tres: bool = path.get_extension().to_lower() == "tres"
	var desc: String = "项目配置（.tres）" if is_tres else "图片素材"
	_drop_dialog.dialog_text = "确认打开：%s？\n\n类型：%s\n%s" % [path.get_file(), desc,
			"将恢复配置参数与切分区域" if is_tres else "将替换当前素材"]
	_drop_dialog.popup_centered()


func _on_drop_confirmed() -> void:
	if _pending_drop_path.is_empty():
		return
	var p: String = _pending_drop_path
	_pending_drop_path = ""
	if p.get_extension().to_lower() == "tres":
		_on_data_selected(p)   # 复用：校验 + apply_data
	elif _controller != null:
		_controller.load_image(p)


func _on_drop_canceled() -> void:
	_pending_drop_path = ""


# ---------- controller 信号 → 画布 ----------

func _on_image_loaded(tex: Texture2D) -> void:
	_canvas.set_texture(tex)
	if tex == null:
		_file_label.text = "未选择素材"   # 关闭图片：恢复初始提示
		return
	_canvas.fit()
	if _controller != null and _controller.image != null:
		_file_label.text = "%s (%dx%d)" % [_controller.image_name,
				_controller.image.get_width(), _controller.image.get_height()]


func _on_close_image() -> void:
	if _controller != null:
		_controller.close_image()


# ---------- 项目注册表列表（右侧卡片，点击加载配置） ----------

func _on_registry_updated() -> void:
	if _controller == null or _controller.registry == null:
		return
	for c: Node in _reg_vbox.get_children():
		_reg_vbox.remove_child(c)
		c.queue_free()
	for path: String in _controller.registry.entries:
		_reg_vbox.add_child(_make_reg_item(path))
		var sep: HSeparator = REG_SEP_SCENE.instantiate()
		_reg_vbox.add_child(sep)
	var empty: bool = _controller.registry.entries.is_empty()
	_reg_empty.visible = empty
	_reg_scroll.visible = not empty


# 注册表条目：竖排（缩略图 + 标题/uid/修改时间），顶级 PanelContainer，选中态手动维护
# 条目样式在 ui/reg_item.tscn 编辑（用户可改），此处只填充数据与选中/点击逻辑
func _make_reg_item(path: String) -> Control:
	var item: Control = REG_ITEM_SCENE.instantiate()
	item.clicked.connect(_on_reg_item_clicked)
	# 条目数据
	var title: String = path.get_file().get_basename()
	var uid_text: String = ""
	var time_text: String = ""
	var tex: Texture2D = null
	var path_text:String = ""
	if ResourceLoader.exists(path):
		var d: Variant = load(path)
		if d is SpriteSplitterData:
			if not String(d.project_name).is_empty():
				title = d.project_name
			uid_text = String(d.sheet_uid) if not String(d.sheet_uid).is_empty() else "外部素材"
			path_text = String(d.source_image) if not String(d.source_image).is_empty() else "-"
			var t: String = _format_time(int(d.modified_at))
			if not t.is_empty():
				time_text = "修改于 " + t
			tex = _registry_thumbnail(d)
	item.call("setup", path, title, uid_text, time_text,path_text, tex)
	item.call("set_selected", path == _active_reg_path)
	return item


func _on_reg_item_clicked(path: String) -> void:
	if _controller == null:
		return
	_active_reg_path = path   # 记录当前选中（手动维护，刷新互斥高亮）
	_refresh_reg_selection()
	# 当前有未保存修改 → 先询问是否保存再切换
	if _controller.is_dirty and _controller.data != null:
		_pending_switch_path = path
		_show_switch_dialog(path)
	else:
		_controller.load_registry_entry(path)


# 打开图片/配置后：按当前项目数据路径同步注册表选中项（无关联数据则取消选中）
func _on_data_path_changed(path: String) -> void:
	if path != _active_reg_path:
		_active_reg_path = path
		_refresh_reg_selection()


# 刷新注册表条目选中态（互斥：仅 active 项高亮）
func _refresh_reg_selection() -> void:
	for c: Node in _reg_vbox.get_children():
		if c.has_method("set_selected"):
			c.call("set_selected", String(c.get("path")) == _active_reg_path)


# 项目缩略图：优先用 data.source_texture（已保存的源纹理引用，注册表直接显示）；
# 旧数据/外部素材 source_texture 为 null 时回退从 source_image 路径加载。
# 等比 contain 居中到 REG_THUMB_SIZE 透明画布
func _registry_thumbnail(data: SpriteSplitterData) -> Texture2D:
	if data == null:
		return null
	var tex: Texture2D = data.source_texture
	if tex == null and not data.source_image.is_empty():
		if data.source_image.begins_with("res://") and ResourceLoader.exists(data.source_image):
			tex = load(data.source_image)   # 原文件引用（已导入纹理）
		else:
			var img0: Image = Image.load_from_file(data.source_image)
			if img0 != null:
				tex = ImageTexture.create_from_image(img0)
	if tex == null:
		return null
	var img: Image = tex.get_image()
	if img == null or img.get_width() <= 0 or img.get_height() <= 0:
		return null
	var canvas := Image.create(REG_THUMB_SIZE, REG_THUMB_SIZE, false, Image.FORMAT_RGBA8)
	canvas.fill(Color(0, 0, 0, 0))
	var w: int = img.get_width()
	var h: int = img.get_height()
	var scale: float = min(float(REG_THUMB_SIZE) / w, float(REG_THUMB_SIZE) / h)
	var tw: int = maxi(1, int(w * scale))
	var th: int = maxi(1, int(h * scale))
	var resized: Image = img.duplicate()
	if resized.get_format() != Image.FORMAT_RGBA8:
		resized.convert(Image.FORMAT_RGBA8)   # blit 要求源目标格式一致（白底 RGB8 图需转换）
	resized.resize(tw, th, Image.INTERPOLATE_BILINEAR)   # resize 是就地方法
	canvas.blit_rect(resized, Rect2i(0, 0, tw, th),
			Vector2i((REG_THUMB_SIZE - tw) / 2, (REG_THUMB_SIZE - th) / 2))
	return ImageTexture.create_from_image(canvas)


# Unix 秒 → 本地时间 "YYYY-MM-DD HH:MM"（Godot Time 无 local 参数，用系统时区偏移换算）
func _format_time(ts: int) -> String:
	if ts <= 0:
		return ""
	var bias: int = Time.get_time_zone_from_system().get("bias", 0)   # 分钟偏移（东八区 +480）
	var dt: Dictionary = Time.get_datetime_dict_from_unix_time(ts + bias * 60)
	return "%04d-%02d-%02d %02d:%02d" % [dt.year, dt.month, dt.day, dt.hour, dt.minute]


func _show_switch_dialog(path: String) -> void:
	if _switch_dialog == null:
		_switch_dialog = ConfirmationDialog.new()
		_switch_dialog.title = "未保存的修改"
		_switch_dialog.ok_button_text = "保存并切换"
		_switch_dialog.confirmed.connect(_on_switch_save)
		_switch_dialog.custom_action.connect(_on_switch_discard)
		_switch_dialog.canceled.connect(_on_switch_cancel)
		_switch_dialog.add_button("不保存并切换")
		add_child(_switch_dialog)
	var pn: String = _controller.data.project_name if _controller.data != null else ""
	_switch_dialog.dialog_text = "项目「%s」有未保存的修改。\n切换前要保存吗？" % pn
	_switch_dialog.popup_centered()


func _on_switch_save() -> void:
	_save_current_project()
	_do_switch_entry()


func _on_switch_discard(_idx: int) -> void:
	_do_switch_entry()


func _on_switch_cancel() -> void:
	_pending_switch_path = ""   # 取消切换：保持当前项目，选中态回滚到当前项目
	if _controller != null:
		_active_reg_path = _controller.data_path
		_on_registry_updated()


func _do_switch_entry() -> void:
	var p: String = _pending_switch_path
	_pending_switch_path = ""
	if _controller == null or p.is_empty():
		return
	_controller.load_registry_entry(p)


# 用侧栏当前参数保存当前项目（注册表切换前）
func _save_current_project() -> void:
	if _controller == null or _side == null:
		return
	_controller.save_project(String(_side.get("_project_name_edit").text).strip_edges(),
			_side._build_options(), String(_side.get("_out_dir").text),
			int(_side.get("_export_mode_option").selected))


# 工具模式按钮组（单选互斥）
func _setup_tool_buttons() -> void:
	var group: ButtonGroup = ButtonGroup.new()
	_move_btn.toggle_mode = true
	_move_btn.button_group = group
	_select_btn.toggle_mode = true
	_select_btn.button_group = group
	_crop_btn.toggle_mode = true
	_crop_btn.button_group = group
	_select_btn.button_pressed = true   # 默认「选择」工具
	_move_btn.pressed.connect(_on_tool_move)
	_select_btn.pressed.connect(_on_tool_select)
	_crop_btn.pressed.connect(_on_tool_crop)
	_zoom_out_btn.pressed.connect(_on_zoom_out)
	_zoom_in_btn.pressed.connect(_on_zoom_in)
	_fit_btn.pressed.connect(_on_fit)


func _on_tool_move() -> void:
	_canvas.set_tool(_canvas.Tool.MOVE)


func _on_tool_select() -> void:
	_canvas.set_tool(_canvas.Tool.SELECT)


func _on_tool_crop() -> void:
	_canvas.set_tool(_canvas.Tool.CROP)


func _on_zoom_out() -> void:
	_canvas.zoom_out()


func _on_zoom_in() -> void:
	_canvas.zoom_in()


func _on_fit() -> void:
	_canvas.fit()


# ---------- controller 信号 → 画布 ----------

func _on_rects_changed(rects: Array[Rect2i]) -> void:
	_canvas.set_rects(rects)
	# 切分数据列表（右侧卡片）：切分/导入 meta 后刷新每个精灵区域
	_split_list.clear()
	for i: int in rects.size():
		var r: Rect2i = rects[i]
		_split_list.add_item("#%d  (%d,%d) %dx%d" % [i + 1, r.position.x,
				r.position.y, r.size.x, r.size.y])
	# 空状态：无区域时显示「暂无数据」
	var empty: bool = rects.is_empty()
	_split_empty.visible = empty
	_split_list.visible = not empty


# Auto 诊断 → 画布灰色网格布局 overlay（调试 Auto 决策：cell 线 + 红框 = 实际 sprite）
func _on_auto_diag_changed(diag: Dictionary) -> void:
	_canvas.set_grid_overlay(diag)


# ---------- 画布信号 → controller（侧栏状态） ----------

func _on_canvas_selection_drawn(rect_world: Rect2i) -> void:
	if _controller != null:
		_controller.set_crop_rect(rect_world)


func _on_canvas_selection_changed(selected: Array[Rect2i]) -> void:
	if _controller != null:
		_controller.on_canvas_selection(selected)


func _on_canvas_view_changed() -> void:
	_zoom_label.text = "%d%%" % _canvas.get_zoom_percent()
