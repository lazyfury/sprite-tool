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

var path: String = ""
var selected: bool = false

const SEL_BG: Color = Color(0.24, 0.24, 0.28)   # 选中高亮背景（可改）
const SEL_RADIUS: int = 8


func setup(p: String, title: String, uid_text: String, time_text: String,
		tex: Texture2D) -> void:
	path = p
	get_node("Row/Col/TitleLabel").text = title
	get_node("Row/Col/UidLabel").text = uid_text
	get_node("Row/Col/TimeLabel").text = time_text
	get_node("Row/Thumb").texture = tex


# 选中状态：换根面板样式（main 维护，互斥；未选中恢复 tscn 里的透明面板）
# 从 tscn 面板 duplicate，保留圆角/内容边距，只换背景色
func set_selected(s: bool) -> void:
	selected = s
	if s:
		var base: StyleBox = get_theme_stylebox("panel")
		var sb: StyleBoxFlat = base.duplicate()
		sb.bg_color = SEL_BG
		sb.set_corner_radius_all(SEL_RADIUS)
		add_theme_stylebox_override("panel", sb)
	else:
		remove_theme_stylebox_override("panel")


func get_thumb() -> TextureRect:
	return get_node("Row/Thumb")


func get_labels() -> Array[Label]:
	return [
		get_node("Row/Col/TitleLabel"),
		get_node("Row/Col/UidLabel"),
		get_node("Row/Col/TimeLabel"),
	]


func _gui_input(event: InputEvent) -> void:
	var mb: InputEventMouseButton = event as InputEventMouseButton
	if mb != null and mb.pressed and mb.button_index == MOUSE_BUTTON_LEFT:
		clicked.emit(path)
		accept_event()
