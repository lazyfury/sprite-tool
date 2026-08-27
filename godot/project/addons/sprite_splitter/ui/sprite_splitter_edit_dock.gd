@tool
extends PanelContainer

## 切片数据 dock（独立面板，与侧栏 dock 并列停靠，DOCK_SLOT_RIGHT_BR）：
## 单选切片后在表单编辑名称/分组/几何/锁定/忽略，保存写入 controller 数据；
## 多选时切换为批量分组表单（输入分组名或下拉选已有分组 → 保存应用到全部选中）。
## 联动由 controller 信号桥接：edit_sprite_requested（列表双击/右键「编辑…」）
## → 填充表单；sprites_changed（任何选中/数据变化）→ 实时同步三分支状态。
##
## 编码约定（项目强制）：var 显式类型标注，不用 :=。

var _controller: SpsController = null
var _edit_index: int = -1   # 当前编辑的切片索引（-1 = 无）

@onready var _hint: Label = get_node("Scroll/VBox/EditHint")
@onready var _form: VBoxContainer = get_node("Scroll/VBox/EditForm")
# 多选批量分组表单
@onready var _group_batch: VBoxContainer = get_node("Scroll/VBox/GroupBatch")
@onready var _batch_hint: Label = get_node("Scroll/VBox/GroupBatch/BatchHint")
@onready var _batch_pick: OptionButton = get_node("Scroll/VBox/GroupBatch/BatchRow/BatchPick")
@onready var _batch_group_edit: LineEdit = get_node("Scroll/VBox/GroupBatch/BatchRow/BatchGroupEdit")
@onready var _batch_save_btn: Button = get_node("Scroll/VBox/GroupBatch/BatchRow/BatchSaveBtn")
@onready var _uid_label: Label = get_node("Scroll/VBox/EditForm/BaseCard/BaseVBox/BaseBody/UidRow/EditUid")
@onready var _name_edit: LineEdit = get_node("Scroll/VBox/EditForm/BaseCard/BaseVBox/BaseBody/NameRow/EditName")
@onready var _group_edit: LineEdit = get_node("Scroll/VBox/EditForm/BaseCard/BaseVBox/BaseBody/GroupRow/EditGroup")
@onready var _x: SpinBox = get_node("Scroll/VBox/EditForm/PosCard/PosVBox/PosBody/EditXRow/EditX")
@onready var _y: SpinBox = get_node("Scroll/VBox/EditForm/PosCard/PosVBox/PosBody/EditYRow/EditY")
@onready var _w: SpinBox = get_node("Scroll/VBox/EditForm/SizeCard/SizeVBox/SizeBody/EditWRow/EditW")
@onready var _h: SpinBox = get_node("Scroll/VBox/EditForm/SizeCard/SizeVBox/SizeBody/EditHRow/EditH")
@onready var _locked: CheckBox = get_node("Scroll/VBox/EditForm/FlagCard/FlagVBox/FlagBody/EditLocked")
@onready var _ignored: CheckBox = get_node("Scroll/VBox/EditForm/FlagCard/FlagVBox/FlagBody/EditIgnored")
@onready var _save_btn: Button = get_node("Scroll/VBox/EditForm/SaveEditBtn")
# 折叠分组卡片（与 side dock 一致的样式：圆角卡片 + ▾ header）
@onready var _base_header: Button = get_node("Scroll/VBox/EditForm/BaseCard/BaseVBox/BaseHeader")
@onready var _base_body: VBoxContainer = get_node("Scroll/VBox/EditForm/BaseCard/BaseVBox/BaseBody")
@onready var _pos_header: Button = get_node("Scroll/VBox/EditForm/PosCard/PosVBox/PosHeader")
@onready var _pos_body: VBoxContainer = get_node("Scroll/VBox/EditForm/PosCard/PosVBox/PosBody")
@onready var _size_header: Button = get_node("Scroll/VBox/EditForm/SizeCard/SizeVBox/SizeHeader")
@onready var _size_body: VBoxContainer = get_node("Scroll/VBox/EditForm/SizeCard/SizeVBox/SizeBody")
@onready var _flag_header: Button = get_node("Scroll/VBox/EditForm/FlagCard/FlagVBox/FlagHeader")
@onready var _flag_body: HBoxContainer = get_node("Scroll/VBox/EditForm/FlagCard/FlagVBox/FlagBody")
# 分组卡片统一主题化（dark_color_2 + 圆角，与 side 卡片一致；外层无卡，dock 面板自身为背景）
@onready var _cards: Array[PanelContainer] = [
	get_node("Scroll/VBox/EditForm/BaseCard"),
	get_node("Scroll/VBox/EditForm/PosCard"),
	get_node("Scroll/VBox/EditForm/SizeCard"),
	get_node("Scroll/VBox/EditForm/FlagCard"),
]


func set_controller(c: SpsController) -> void:
	_controller = c
	if _controller == null:
		return
	_controller.edit_sprite_requested.connect(_on_edit_requested)
	_controller.sprites_changed.connect(_on_sprites_changed)


func _ready() -> void:
	_save_btn.pressed.connect(_on_save)
	_locked.toggled.connect(_on_locked_toggled)
	_batch_pick.item_selected.connect(_on_batch_pick_selected)
	_batch_save_btn.pressed.connect(_on_batch_save)
	_setup_fold_headers()
	_apply_theme()
	if Engine.is_editor_hint():
		EditorInterface.get_base_control().theme_changed.connect(_apply_theme)
	_refresh_form()   # 初始占位提示


# 分组折叠（与 side dock 一致：header 点击 → body 展开/收起，箭头 ▾/▸ 同步）
func _setup_fold_headers() -> void:
	_setup_fold_header(_base_header, _base_body)
	_setup_fold_header(_pos_header, _pos_body)
	_setup_fold_header(_size_header, _size_body)
	_setup_fold_header(_flag_header, _flag_body)


func _setup_fold_header(header: Button, body: Control) -> void:
	# 幂等：tscn 里 header 可能已带 ▾（手动编辑），先 trim 再加，避免双三角
	header.text = "▾ " + header.text.trim_prefix("▾ ").trim_prefix("▸ ")
	header.toggled.connect(func(on: bool) -> void:
		body.visible = on
		header.text = ("▾ " if on else "▸ ") + header.text.trim_prefix("▾ ").trim_prefix("▸ ")
	)


# ---------- controller 信号 ----------

# 列表双击 / 右键「编辑…」：切到目标切片并填充表单
func _on_edit_requested(index: int) -> void:
	_edit_index = index
	_fill_form(index)
	_hint.visible = false
	_form.visible = true


# 数据/选中变化 → 实时同步：单选 → 完整表单；多选 → 批量分组表单；无选 → 提示占位
func _on_sprites_changed(_sprites: Array[Dictionary]) -> void:
	_refresh_form()


# 刷新表单状态（三分支）：恰好单选 → 填充表单；多选 → 批量分组表单；无选 → 提示占位
func _refresh_form() -> void:
	var multi: int = _selected_count()
	_edit_index = _single_selected_index()
	if multi > 1:
		# 多选：批量设置分组（输入 / 下拉选已有分组 / 保存）
		_hint.visible = false
		_form.visible = false
		_batch_hint.text = "已选 %d 个切片：批量设置分组" % multi
		_group_batch.visible = true
		_refresh_batch_options()
		return
	_group_batch.visible = false
	if _edit_index < 0:
		_hint.visible = true
		_form.visible = false
		return
	_fill_form(_edit_index)
	_hint.visible = false
	_form.visible = true


# 当前选中切片数（画布框选/列表多选，数据 selected 驱动）
func _selected_count() -> int:
	if _controller == null:
		return 0
	var n: int = 0
	for s: Dictionary in _controller.sprites:
		if bool(s.get("selected", false)):
			n += 1
	return n


# 批量分组下拉：未分组 + 已有分组（代码重建，保留当前输入匹配项）
func _refresh_batch_options() -> void:
	var groups: Array[String] = _controller.get_groups() if _controller != null else []
	var cur: String = _batch_group_edit.text.strip_edges()
	_batch_pick.clear()
	_batch_pick.add_item("未分组")
	for g: String in groups:
		_batch_pick.add_item(g)
	_batch_pick.select(0)
	for i: int in groups.size():
		if groups[i] == cur:
			_batch_pick.select(i + 1)
			break


# 下拉选已有分组 → 填充输入框（未分组 → 清空）
func _on_batch_pick_selected(index: int) -> void:
	if index <= 0:
		_batch_group_edit.text = ""
		return
	_batch_group_edit.text = _batch_pick.get_item_text(index)


# 批量保存：把输入的分组应用到所有选中切片
func _on_batch_save() -> void:
	if _controller == null:
		return
	_controller.set_group_for_selected(_batch_group_edit.text)


# 数据中恰好 1 个 selected → 返回其索引；多选/无选返回 -1
func _single_selected_index() -> int:
	if _controller == null:
		return -1
	var idx: int = -1
	for i: int in _controller.sprites.size():
		if bool(_controller.sprites[i].get("selected", false)):
			if idx >= 0:
				return -1   # 多选：暂不支持编辑
			idx = i
	return idx


func _fill_form(index: int) -> void:
	if _controller == null or index < 0 or index >= _controller.sprites.size():
		return
	var s: Dictionary = _controller.sprites[index]
	_uid_label.text = String(s.get("uid", ""))   # 前缀「UID」由 UidLabel 承担（非备注小字）
	_name_edit.text = String(s.get("name", ""))
	_group_edit.text = String(s.get("group", ""))
	_x.value = float(int(s.get("x", 0)))
	_y.value = float(int(s.get("y", 0)))
	_w.value = float(int(s.get("width", 0)))
	_h.value = float(int(s.get("height", 0)))
	_locked.button_pressed = bool(s.get("locked", false))
	_ignored.button_pressed = bool(s.get("ignored", false))
	_apply_lock_state()


func _on_locked_toggled(_pressed: bool) -> void:
	_apply_lock_state()


# 锁定勾选 → 几何输入禁用（锁定不可编辑几何，保存时 controller 也强制保持原值）
func _apply_lock_state() -> void:
	var l: bool = _locked.button_pressed
	_x.editable = not l
	_y.editable = not l
	_w.editable = not l
	_h.editable = not l


# 保存 → 批量提交数据（名称/几何/锁定/忽略）
func _on_save() -> void:
	if _controller == null or _edit_index < 0 \
			or _edit_index >= _controller.sprites.size():
		return
	_controller.update_sprite_fields(_edit_index, {
		"name": _name_edit.text,
		"group": _group_edit.text,
		"x": int(_x.value),
		"y": int(_y.value),
		"width": int(_w.value),
		"height": int(_h.value),
		"locked": _locked.button_pressed,
		"ignored": _ignored.button_pressed,
	})


# ---------- 主题化（跟随编辑器主题，与 side dock 一致） ----------

func _apply_theme() -> void:
	var th: Theme = null
	if Engine.is_editor_hint():
		th = EditorInterface.get_editor_theme()
	# 外层面板透明：背景按分组卡片呈现（用户要求去掉整层 dock 背景）
	# 尊重 tscn 手动定义的 panel override（用户手动改样式优先）
	if not has_theme_stylebox_override("panel"):
		var transparent: StyleBoxFlat = StyleBoxFlat.new()
		transparent.bg_color = Color(0, 0, 0, 0)
		add_theme_stylebox_override("panel", transparent)
	# 分组卡卡片效果（dark_color_2 + 圆角，与 side 卡片一致；tscn 已定义则不覆盖）
	var card_bg: Color = _theme_color(th, "dark_color_2", Color(0.19, 0.19, 0.21))
	for card: PanelContainer in _cards:
		if not card.has_theme_stylebox_override("panel"):
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
