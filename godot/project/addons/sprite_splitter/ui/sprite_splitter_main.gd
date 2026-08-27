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
const SHEET_WINDOW_SCENE: PackedScene = preload("res://addons/sprite_splitter/ui/sheet_builder_window.tscn")
const CLEANUP_WINDOW_SCENE: PackedScene = preload("res://addons/sprite_splitter/ui/cleanup_window.tscn")
# 创建 Sheets 独立窗口（复用单例，关闭仅 hide）
var _sheet_window: Window = null
# 清理生成资源窗口（复用单例，关闭仅 hide）
var _cleanup_window: Window = null
# 文件对话框：编辑器模式 EditorFileDialog / 运行模式 FileDialog → Variant
var _dialog: Variant = null
var _data_dialog: Variant = null
# 拖放导入：确认弹窗 + 待确认文件
var _drop_dialog: ConfirmationDialog = null
var _pending_drop_path: String = ""
# 注册表切换：脏数据确认弹窗 + 待切换条目
var _switch_dialog: ConfirmationDialog = null
var _pending_switch_path: String = ""
# 注册表项右键上下文菜单（文件系统风格：打开/复制路径/显示/删除）
const REG_MENU_OPEN: int = 0
const REG_MENU_COPY: int = 1
const REG_MENU_REVEAL: int = 2
const REG_MENU_DELETE: int = 3
var _reg_menu: PopupMenu = null
var _reg_menu_path: String = ""
# 注册表项删除：确认弹窗 + 待删除路径（右键菜单 → 删除）
var _delete_dialog: ConfirmationDialog = null
var _pending_delete_path: String = ""
# 选中切片批量删除：确认弹窗
var _delete_sel_dialog: ConfirmationDialog = null
# 切片列表右键菜单（编辑/重命名/锁定/导出忽略）与重命名弹窗
const SPRITE_MENU_EDIT: int = 0
const SPRITE_MENU_RENAME: int = 1
const SPRITE_MENU_LOCK: int = 2
const SPRITE_MENU_IGNORE: int = 3
var _sprite_menu: PopupMenu = null
var _sprite_menu_index: int = -1
var _rename_dialog: AcceptDialog = null
var _rename_edit: LineEdit = null
var _rename_index: int = -1
var _active_reg_path: String = ""                 # 当前选中/加载的项目路径（手动维护选中态）

@onready var _header: PanelContainer = get_node("MainVBox/Header")
@onready var _file_button: Button = get_node("MainVBox/Header/HeaderRow/FileButton")
@onready var _create_sheets_btn: Button = get_node("MainVBox/Header/HeaderRow/CreateSheetsBtn")
@onready var _cleanup_btn: Button = get_node("MainVBox/Header/HeaderRow/CleanupBtn")
@onready var _data_btn: Button = get_node("MainVBox/Header/HeaderRow/DataBtn")
@onready var _close_btn: Button = get_node("MainVBox/Header/HeaderRow/CloseBtn")
@onready var _file_label: Label = get_node("MainVBox/Header/HeaderRow/FileLabel")
@onready var _canvas: Control = get_node("%CanvasView")   # 唯一名（结构变动不失效）
@onready var _split_list: ItemList = get_node("MainVBox/HSplitContainer/Control/RightVBox/CardPanel/CardVBox/ListArea/SplitList")
@onready var _split_empty: Label = get_node("MainVBox/HSplitContainer/Control/RightVBox/CardPanel/CardVBox/ListArea/EmptyLabel")
@onready var _split_card: PanelContainer = get_node("MainVBox/HSplitContainer/Control/RightVBox/CardPanel")
@onready var _group_option: OptionButton = get_node("MainVBox/HSplitContainer/Control/RightVBox/CardPanel/CardVBox/GroupRow/GroupOption")
@onready var _select_group_btn: Button = get_node("MainVBox/HSplitContainer/Control/RightVBox/CardPanel/CardVBox/GroupRow/SelectGroupBtn")
@onready var _delete_sel_btn: Button = get_node("MainVBox/ToolBarMargin/ToolBarRow/DeleteSelBtn")
@onready var _reg_card: PanelContainer = get_node("MainVBox/HSplitContainer/Control/RightVBox/RegCardPanel")
@onready var _reg_scroll: ScrollContainer = get_node("MainVBox/HSplitContainer/Control/RightVBox/RegCardPanel/RegVBox/RegListArea/RegScroll")
@onready var _reg_vbox: VBoxContainer = get_node("MainVBox/HSplitContainer/Control/RightVBox/RegCardPanel/RegVBox/RegListArea/RegScroll/RegVBoxList")
@onready var _reg_empty: Label = get_node("MainVBox/HSplitContainer/Control/RightVBox/RegCardPanel/RegVBox/RegListArea/RegEmptyLabel")
@onready var _move_btn: Button = get_node("MainVBox/ToolBarMargin/ToolBarRow/ToolBar/MoveBtn")
@onready var _select_btn: Button = get_node("MainVBox/ToolBarMargin/ToolBarRow/ToolBar/SelectBtn")
@onready var _edit_btn: Button = get_node("MainVBox/ToolBarMargin/ToolBarRow/ToolBar/EditBtn")
@onready var _crop_btn: Button = get_node("MainVBox/ToolBarMargin/ToolBarRow/ToolBar/CropBtn")
@onready var _confirm_crop_btn: Button = get_node("MainVBox/ToolBarMargin/ToolBarRow/ToolBar/ConfirmCropBtn")
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
	_controller.sprites_changed.connect(_on_sprites_changed)
	_controller.auto_diag_changed.connect(_on_auto_diag_changed)
	_canvas.selection_drawn.connect(_on_canvas_selection_drawn)
	_canvas.selection_changed.connect(_on_canvas_selection_changed)
	_canvas.view_changed.connect(_on_canvas_view_changed)
	_canvas.drop_requested.connect(_on_canvas_drop_requested)
	_canvas.geometry_committed.connect(_on_canvas_geometry_committed)
	_canvas.crop_confirmed.connect(_on_crop_confirmed)
	_controller.registry_updated.connect(_on_registry_updated)
	_controller.data_path_changed.connect(_on_data_path_changed)
	_on_registry_updated()   # 初始填充注册表列表


func set_side(s: Control) -> void:
	_side = s


func get_canvas_view() -> Control:
	return _canvas


func _ready() -> void:
	_setup_tool_buttons()
	_setup_reg_menu()
	_setup_sprite_menu()
	_apply_theme()
	if Engine.is_editor_hint():
		EditorInterface.get_base_control().theme_changed.connect(_apply_theme)
	_file_button.pressed.connect(_on_file_button)
	_create_sheets_btn.pressed.connect(_on_create_sheets)
	_cleanup_btn.pressed.connect(_on_cleanup)
	_data_btn.pressed.connect(_on_open_data)
	_close_btn.pressed.connect(_on_close_image)
	_split_list.item_clicked.connect(_on_split_list_clicked)
	_split_list.item_selected.connect(_on_split_list_selected)
	_split_list.item_activated.connect(_on_split_list_activated)
	_select_group_btn.pressed.connect(_on_select_group)
	_delete_sel_btn.pressed.connect(_on_delete_selected)


# 注册表项右键上下文菜单（文件系统风格）：打开配置 / 复制路径 / 在文件管理器中显示 / 删除
func _setup_reg_menu() -> void:
	_reg_menu = PopupMenu.new()
	_reg_menu.add_item("打开配置", REG_MENU_OPEN)
	_reg_menu.add_item("复制路径", REG_MENU_COPY)
	_reg_menu.add_item("在文件管理器中显示", REG_MENU_REVEAL)
	_reg_menu.add_separator()
	_reg_menu.add_item("删除…", REG_MENU_DELETE)
	_reg_menu.id_pressed.connect(_on_reg_menu_id_pressed)
	add_child(_reg_menu)


# 切片列表右键菜单（复杂结构操作）：编辑 / 重命名 / 锁定解锁 / 导出忽略包含
func _setup_sprite_menu() -> void:
	_sprite_menu = PopupMenu.new()
	_sprite_menu.add_item("编辑…", SPRITE_MENU_EDIT)
	_sprite_menu.add_item("重命名…", SPRITE_MENU_RENAME)
	_sprite_menu.add_item("🔒 锁定", SPRITE_MENU_LOCK)
	_sprite_menu.add_item("🙈 导出忽略", SPRITE_MENU_IGNORE)
	_sprite_menu.id_pressed.connect(_on_sprite_menu_id_pressed)
	add_child(_sprite_menu)


# 列表项点击（item_clicked：左键选中 / 右键菜单）
func _on_split_list_clicked(index: int, at_position: Vector2, mouse_button_index: int) -> void:
	if mouse_button_index == MOUSE_BUTTON_RIGHT:
		_on_split_list_rmb(index)


# 列表项选中（item_selected）→ 画布联动选中对应切片
func _on_split_list_selected(index: int) -> void:
	if _canvas != null:
		_canvas.select_index(index)


# 列表项右键 → 弹出切片菜单（锁定/忽略文本随当前状态切换）
func _on_split_list_rmb(index: int) -> void:
	if _controller == null or index < 0 or index >= _controller.sprites.size():
		return
	_sprite_menu_index = index
	var s: Dictionary = _controller.sprites[index]
	_sprite_menu.set_item_text(SPRITE_MENU_LOCK,
			"🔓 解锁" if bool(s.get("locked", false)) else "🔒 锁定")
	_sprite_menu.set_item_text(SPRITE_MENU_IGNORE,
			"👁 导出包含" if bool(s.get("ignored", false)) else "🙈 导出忽略")
	# 菜单位置用全局鼠标坐标（ItemList 的 at_position 是局部坐标，会错位）
	_sprite_menu.popup(Rect2i(Vector2i(get_global_mouse_position()), Vector2i.ZERO))


func _on_sprite_menu_id_pressed(id: int) -> void:
	if _controller == null or _sprite_menu_index < 0 \
			or _sprite_menu_index >= _controller.sprites.size():
		return
	var idx: int = _sprite_menu_index
	var s: Dictionary = _controller.sprites[idx]
	match id:
		SPRITE_MENU_EDIT:
			_open_sprite_editor(idx)
		SPRITE_MENU_RENAME:
			_open_rename_dialog(idx)
		SPRITE_MENU_LOCK:
			_controller.set_sprite_locked(idx, not bool(s.get("locked", false)))
		SPRITE_MENU_IGNORE:
			_controller.set_sprite_ignored(idx, not bool(s.get("ignored", false)))


# 重命名弹窗：AcceptDialog + LineEdit
func _open_rename_dialog(index: int) -> void:
	_rename_index = index
	if _rename_dialog == null:
		_rename_dialog = AcceptDialog.new()
		_rename_dialog.title = "重命名切片"
		_rename_dialog.ok_button_text = "确定"
		var box: VBoxContainer = VBoxContainer.new()
		_rename_edit = LineEdit.new()
		_rename_edit.placeholder_text = "切片名称"
		box.add_child(_rename_edit)
		_rename_dialog.add_child(box)
		_rename_dialog.register_text_enter(_rename_edit)
		_rename_dialog.confirmed.connect(_on_rename_confirmed)
		add_child(_rename_dialog)
	if index >= 0 and index < _controller.sprites.size():
		_rename_edit.text = String(_controller.sprites[index].get("name", ""))
	_rename_edit.select_all()
	_rename_dialog.popup_centered()
	_rename_edit.grab_focus()


func _on_rename_confirmed() -> void:
	if _controller != null and _rename_index >= 0:
		_controller.rename_sprite(_rename_index, _rename_edit.text.strip_edges())


# ---------- 切片编辑入口（独立 dock 面板，侧栏停靠） ----------

# 列表双击 → 请求编辑该切片（多选守卫/收敛单选在 controller）
func _on_split_list_activated(index: int) -> void:
	_open_sprite_editor(index)


# 转发给 controller：request_edit_sprite 做多选守卫（>1 拒绝提示）并收敛单选，
# 发 edit_sprite_requested 让编辑 dock 填充表单
func _open_sprite_editor(index: int) -> void:
	if _controller != null:
		_controller.request_edit_sprite(index)


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


# 创建 Sheets：独立窗口（从多张已切分小图组装 sheet）。单例复用，关闭仅 hide。
# 窗口尺寸按当前屏幕比例自适应（macOS 屏幕缩放场景固定像素会偏小）。
func _on_create_sheets() -> void:
	if _sheet_window == null:
		_sheet_window = SHEET_WINDOW_SCENE.instantiate()
		if Engine.is_editor_hint():
			EditorInterface.get_base_control().add_child(_sheet_window)   # 编辑器根下独立顶级窗口
		else:
			add_child(_sheet_window)
		_sheet_window.set_controller(_controller)
	_sheet_window.reset_file_name()   # 每次打开初始化随机文件名（确定性链路，不依赖 visibility 信号）
	var screen: Vector2i = DisplayServer.screen_get_size()
	_sheet_window.min_size = Vector2i(maxi(520, screen.x / 3), maxi(560, screen.y / 2))
	_sheet_window.size = Vector2i(maxi(640, screen.x * 60 / 100), maxi(640, screen.y * 56 / 100))
	_sheet_window.popup_centered()


# 清理生成资源：独立窗口（列出注册表全部产物 + 占用检查 + 清理未占用）。
# 复用单例：首次 instantiate 挂编辑器根，之后仅 hide/show；打开即 refresh（最新占用态）。
func _on_cleanup() -> void:
	if _cleanup_window == null:
		_cleanup_window = CLEANUP_WINDOW_SCENE.instantiate()
		if Engine.is_editor_hint():
			EditorInterface.get_base_control().add_child(_cleanup_window)   # 编辑器根下独立顶级窗口
		else:
			add_child(_cleanup_window)
		_cleanup_window.set_controller(_controller)
	_cleanup_window.refresh()
	var screen2: Vector2i = DisplayServer.screen_get_size()
	_cleanup_window.min_size = Vector2i(maxi(560, screen2.x / 3), maxi(420, screen2.y / 2))
	_cleanup_window.size = Vector2i(maxi(680, screen2.x * 52 / 100), maxi(480, screen2.y * 46 / 100))
	_cleanup_window.popup_centered()


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
	item.menu_requested.connect(_on_reg_item_menu_requested)
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


# 注册表项右键 → 上下文菜单（在鼠标位置弹出）
func _on_reg_item_menu_requested(path: String, at_global: Vector2) -> void:
	_reg_menu_path = path
	_reg_menu.popup(Rect2i(Vector2i(at_global), Vector2i.ZERO))


# 上下文菜单分派（文件系统风格）
func _on_reg_menu_id_pressed(id: int) -> void:
	var path: String = _reg_menu_path
	if path.is_empty() or _controller == null:
		return
	match id:
		REG_MENU_OPEN:
			_on_reg_item_clicked(path)   # 打开配置 = 左键点击语义（含脏数据确认）
		REG_MENU_COPY:
			DisplayServer.clipboard_set(path)
			if _controller != null:
				_controller.status_changed.emit("已复制配置路径: " + path, false)
		REG_MENU_REVEAL:
			OS.shell_show_in_file_manager(ProjectSettings.globalize_path(path), true)
		REG_MENU_DELETE:
			_on_reg_item_delete_requested(path)


# 注册表项删除确认弹窗（删除 .tres 配置文件，不可撤销）
func _on_reg_item_delete_requested(path: String) -> void:
	_pending_delete_path = path
	if _delete_dialog == null:
		_delete_dialog = ConfirmationDialog.new()
		_delete_dialog.title = "删除项目配置"
		_delete_dialog.ok_button_text = "删除"
		_delete_dialog.confirmed.connect(_on_delete_confirmed)
		_delete_dialog.canceled.connect(_on_delete_canceled)
		add_child(_delete_dialog)
	var title: String = path.get_file().get_basename()
	if ResourceLoader.exists(path):
		var d: Variant = load(path)
		if d is SpriteSplitterData and not String(d.project_name).is_empty():
			title = String(d.project_name)
	_delete_dialog.dialog_text = "删除项目「%s」？\n\n将删除配置文件（%s），此操作不可撤销。" \
			% [title, path.get_file()]
	_delete_dialog.popup_centered()


func _on_delete_confirmed() -> void:
	var p: String = _pending_delete_path
	_pending_delete_path = ""
	if _controller != null and not p.is_empty():
		_controller.remove_registry_entry(p)


func _on_delete_canceled() -> void:
	_pending_delete_path = ""


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


# 工具模式按钮组（单选互斥：移动/选择/编辑/裁切）
func _setup_tool_buttons() -> void:
	var group: ButtonGroup = ButtonGroup.new()
	_move_btn.toggle_mode = true
	_move_btn.button_group = group
	_select_btn.toggle_mode = true
	_select_btn.button_group = group
	_edit_btn.toggle_mode = true
	_edit_btn.button_group = group
	_crop_btn.toggle_mode = true
	_crop_btn.button_group = group
	_select_btn.button_pressed = true   # 默认「选择」工具
	_move_btn.pressed.connect(_on_tool_move)
	_select_btn.pressed.connect(_on_tool_select)
	_edit_btn.pressed.connect(_on_tool_edit)
	_crop_btn.pressed.connect(_on_tool_crop)
	_confirm_crop_btn.pressed.connect(_on_confirm_crop)
	_zoom_out_btn.pressed.connect(_on_zoom_out)
	_zoom_in_btn.pressed.connect(_on_zoom_in)
	_fit_btn.pressed.connect(_on_fit)


func _on_tool_move() -> void:
	_canvas.set_tool(_canvas.Tool.MOVE)
	_refresh_confirm_btn()


func _on_tool_select() -> void:
	_canvas.set_tool(_canvas.Tool.SELECT)
	_refresh_confirm_btn()


func _on_tool_edit() -> void:
	_canvas.set_tool(_canvas.Tool.EDIT)
	_refresh_confirm_btn()


func _on_tool_crop() -> void:
	_canvas.set_tool(_canvas.Tool.CROP)
	_confirm_crop_btn.disabled = false   # 裁切模式：确认按钮可用


# 非裁切工具：确认按钮禁用（切工具时由各 handler 调用 _refresh_confirm_btn）
func _refresh_confirm_btn() -> void:
	_confirm_crop_btn.disabled = _canvas.get_tool() != _canvas.Tool.CROP


# 确认裁切：把画布选区加入切片数据
func _on_confirm_crop() -> void:
	_canvas.crop_confirm()


func _on_crop_confirmed(rect: Rect2i) -> void:
	if _controller != null:
		_controller.add_sprite_from_rect(rect)


func _on_zoom_out() -> void:
	_canvas.zoom_out()


func _on_zoom_in() -> void:
	_canvas.zoom_in()


func _on_fit() -> void:
	_canvas.fit()


# ---------- controller 信号 → 画布 ----------

# 画布数据统一由 sprites_changed 注入（uid 追踪保留编辑态）；此信号仅供兼容/旧调用
func _on_rects_changed(rects: Array[Rect2i]) -> void:
	pass


# 复杂切片结构 → 画布 + 右侧列表（名称 + emoji 状态 + xywh）
# 列表原生高亮按数据 selected 恢复（单选高亮对应项；多选/无选取消）——
# 任何 sprites_changed 路径（画布选择/列表点击/编辑 dock 保存/右键锁定）都保持列表高亮同步
func _on_sprites_changed(sprites_in: Array[Dictionary]) -> void:
	_canvas.set_sprites(sprites_in)
	_split_list.clear()
	for i: int in sprites_in.size():
		_split_list.add_item(_sprite_label(sprites_in[i], i))
	# 数据 selected → 列表原生高亮（ItemList.select() 不发 item_selected，无循环）
	var sel_idx: int = -1
	for i: int in sprites_in.size():
		if bool(sprites_in[i].get("selected", false)):
			if sel_idx >= 0:
				sel_idx = -1   # 多选：不设原生高亮（emoji ✅ 仍标记）
				break
			sel_idx = i
	if sel_idx >= 0:
		_split_list.select(sel_idx)
	else:
		_split_list.deselect_all()
	# 分组下拉跟随数据重建（保留当前选择）
	_rebuild_group_options()
	# 空状态：无区域时显示「暂无数据」
	var empty: bool = sprites_in.is_empty()
	_split_empty.visible = empty
	_split_list.visible = not empty


# 分组下拉选项：全部 + 未分组 + 全部分组（按出现顺序）；重建时尽量保留当前选择
func _rebuild_group_options() -> void:
	var groups: Array[String] = _controller.get_groups() if _controller != null else []
	var cur: String = ""
	var sel: int = _group_option.selected
	if sel > 0 and sel < _group_option.item_count:
		cur = _group_option.get_item_text(sel)
	_group_option.clear()
	_group_option.add_item("全部")
	_group_option.add_item("未分组")
	for g: String in groups:
		_group_option.add_item(g)
	_group_option.select(0)
	if cur == "未分组":
		_group_option.select(1)
		return
	for i: int in groups.size():
		if groups[i] == cur:
			_group_option.select(i + 2)
			break


# 按组选择：全部（选中所有）/ 未分组（group 为空串）/ 指定分组
func _on_select_group() -> void:
	if _controller == null:
		return
	var sel: int = _group_option.selected
	if sel <= 0:
		_controller.select_all()
		return
	if sel == 1:
		_controller.select_group("")
		return
	_controller.select_group(_group_option.get_item_text(sel))


# 批量删除选中切片：确认弹窗（不可撤销），确认后删除当前选中的全部切片
func _on_delete_selected() -> void:
	if _controller == null:
		return
	var n: int = 0
	for s: Dictionary in _controller.sprites:
		if bool(s.get("selected", false)):
			n += 1
	if n == 0:
		_controller.status_changed.emit("没有选中的切片可删除", true)
		return
	if _delete_sel_dialog == null:
		_delete_sel_dialog = ConfirmationDialog.new()
		_delete_sel_dialog.title = "删除选中切片"
		_delete_sel_dialog.ok_button_text = "删除"
		_delete_sel_dialog.confirmed.connect(_on_delete_sel_confirmed)
		add_child(_delete_sel_dialog)
	_delete_sel_dialog.dialog_text = "删除选中的 %d 个切片？\n此操作不可撤销。" % n
	_delete_sel_dialog.popup_centered()


func _on_delete_sel_confirmed() -> void:
	if _controller != null:
		_controller.remove_selected_sprites()


# 列表项文本：{✅选中}{🔒锁定}{🙈忽略}#N 名称  (x,y) w×h
func _sprite_label(s: Dictionary, index: int) -> String:
	var mark: String = ""
	if bool(s.get("selected", false)):
		mark += "✅ "
	if bool(s.get("locked", false)):
		mark += "🔒 "
	if bool(s.get("ignored", false)):
		mark += "🙈 "
	var name: String = String(s.get("name", ""))
	if name.is_empty():
		name = "#%d" % (index + 1)
	return "%s#%d %s  (%d,%d) %dx%d" % [mark, index + 1, name,
			int(s.get("x", 0)), int(s.get("y", 0)),
			int(s.get("width", 0)), int(s.get("height", 0))]


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
	# 列表高亮统一由 _on_sprites_changed 按数据 selected 恢复（单一数据源，
	# 避免此处手动高亮与编辑保存等路径的 sprites_changed 重建互相覆盖）


# 画布编辑提交（拖拽移动 / 四角缩放）→ 更新切片数据
func _on_canvas_geometry_committed(index: int, rect: Rect2i) -> void:
	if _controller != null:
		_controller.update_sprite_geometry(index, rect)


func _on_canvas_view_changed() -> void:
	_zoom_label.text = "%d%%" % _canvas.get_zoom_percent()
