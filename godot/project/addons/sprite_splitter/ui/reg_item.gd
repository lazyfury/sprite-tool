@tool
extends PanelContainer

## 注册表条目场景（可编辑样式）：PanelContainer（根自带圆角背景，选中时换根面板样式）
## + Row(HBox) > Thumb(缩略图) + Col(标题/uid/修改时间)。
## 顶级用 PanelContainer 而非裸 Control：容器最小尺寸随内容推导，
## 放入 ScrollContainer>VBox 才有高度（裸 Control + 全锚点子节点会塌成 0 高）。
## 选中状态由 main.gd 手动维护（set_selected）；点击发 clicked(path) 信号。
## 样式直接在此 tscn 里改；setup 可能在节点入树前被调用，用 get_node 而非 @onready。
##
## 编码约定（项目强制）：var 显式类型标注，不用 :=。

signal clicked(path: String)
signal menu_requested(path: String, at_global: Vector2)   # 右键 → 请求上下文菜单（文件系统风格）

var path: String = ""
var selected: bool = false

const SEL_BG: Color = Color(0.19, 0.19, 0.21)  # 选中高亮背景（可改）


func setup(p: String, title: String, uid_text: String, time_text: String, path_text:String,
		tex: Texture2D) -> void:
	path = p
	get_node("Row/Col/TitleLabel").text = title
	get_node("Row/Col/UidLabel").text = uid_text
	get_node("Row/Col/TimeLabel").text = time_text
	get_node("Row/Col/PathLabel").text = path_text
	get_node("Row/Thumb").texture = tex
	_apply_label_spacing()
	$Row/Col.add_theme_constant_override("separation",0)


# 用代码覆盖主题：Label 主题默认行距可能撑高列表项，统一改为 0（紧凑排版）。
# add_theme_constant_override 是纯数据操作，setup 在节点入树前被调用也安全。
func _apply_label_spacing() -> void:
	for label: Label in get_labels():
		label.add_theme_constant_override("line_spacing", -4)


# 选中状态：换根面板样式（main 维护，互斥；未选中恢复 tscn 里的透明面板）
# 注意：不能 remove_theme_stylebox_override("panel")——tscn 里 theme_override_styles/panel
# 与代码 add 的 override 存在同一张映射，remove 会把场景里定义的透明面板一并删掉，
# 取消选中后回落成默认主题暗面板（2026-08-25 修复）。正确做法：首次缓存 tscn 面板，
# 之后每次基于它 duplicate，只换背景色，始终 add override。
var _base_panel: StyleBoxFlat = null


func set_selected(s: bool) -> void:
	selected = s
	if _base_panel == null:
		var base: StyleBox = get_theme_stylebox("panel")
		if base == null:
			return   # 兜底：主题无 panel 样式时跳过（不会发生）
		_base_panel = (base as StyleBoxFlat).duplicate()
	var sb: StyleBoxFlat = _base_panel.duplicate()
	if s:
		sb.bg_color = SEL_BG
	add_theme_stylebox_override("panel", sb)


func get_thumb() -> TextureRect:
	return get_node("Row/Thumb")


func get_labels() -> Array[Label]:
	return [
		get_node("Row/Col/TitleLabel"),
		get_node("Row/Col/UidLabel"),
		get_node("Row/Col/TimeLabel"),
		get_node("Row/Col/PathLabel"),
	]


func _gui_input(event: InputEvent) -> void:
	var mb: InputEventMouseButton = event as InputEventMouseButton
	if mb == null or not mb.pressed:
		return
	if mb.button_index == MOUSE_BUTTON_LEFT:
		clicked.emit(path)
		accept_event()
	elif mb.button_index == MOUSE_BUTTON_RIGHT:
		menu_requested.emit(path, get_global_mouse_position())
		accept_event()
