@tool
extends Control

## 画布式预览视图（M5.3）—— PS/图片编辑软件式工具：
## 图片保持逻辑像素坐标（世界坐标恒定），UI 层相机式缩放/平移——
## 类似 2D 节点 + Camera2D 的效果，纯 Control 自绘实现。
## 变换：screen = world * zoom + (size/2 - center*zoom)；红框/选中/裁切框与图片
## 同一变换绘制 → 天然像素对齐。
##
## 工具模式（工具栏切换，PS 风格）：
##   MOVE   拖拽移动（左键/中键拖拽平移视图）；滚轮缩放、双击 fit
##   SELECT 点击选中切分好的方块（高亮）；画矩形选中多个相交方块；右键清空
##   CROP   画矩形定义自由裁切区域（发出 selection_drawn 供导出选中）
##
## 信号：
##   selection_drawn(rect_world)      CROP 裁切框完成（世界坐标 Rect2i）
##   selection_changed(rects)         SELECT 选中方块集变化
##   view_changed                     缩放/平移变化
##
## 编码约定（项目强制）：var 显式类型标注，不用 :=。

signal selection_drawn(rect_world: Rect2i)
signal selection_changed(rects: Array[Rect2i])
signal view_changed
signal drop_requested(path: String)   # 编辑器 FileSystem dock 拖入文件（确认弹窗前）

enum Tool { MOVE, SELECT, CROP }

const MIN_ZOOM: float = 0.02
const MAX_ZOOM: float = 64.0
const ZOOM_STEP: float = 1.2
const CLICK_THRESHOLD: float = 5.0     # 按下-松开位移小于此值视为点击（屏幕 px）
const BG_COLOR: Color = Color(0.097, 0.104, 0.1, 1.0)
const RECT_COLOR: Color = Color(1.0, 0.25, 0.25, 0.95)          # 切分红框
const GRID_COLOR: Color = Color(0.75, 0.75, 0.78, 0.35)         # Auto 网格布局线（灰，调试决策用）
const SEL_HL_FILL: Color = Color(1.0, 0.85, 0.2, 0.28)          # SELECT 选中填充（黄）
const SEL_HL_STROKE: Color = Color(1.0, 0.85, 0.2, 0.95)        # SELECT 选中描边
const CROP_FILL: Color = Color(0.3, 0.9, 1.0, 0.18)             # CROP 裁切框填充（青）
const CROP_STROKE: Color = Color(0.3, 0.9, 1.0, 0.95)           # CROP 裁切框描边

var _texture: Texture2D = null
var _rects: Array[Rect2i] = []
var _grid: Dictionary = {}   # Auto 网格布局 overlay（auto_diag：cell_w/h、columns/rows、offset_x/y；空 = 不画）
var _zoom: float = 1.0
var _center: Vector2 = Vector2.ZERO      # 视口中心对应的世界坐标（图片像素）
var _selection: Rect2i = Rect2i(-1, -1, 0, 0)   # CROP 裁切框（世界坐标）；size<=0 无
var _tool: int = Tool.SELECT
var _selected: Array[Rect2i] = []        # SELECT 选中的方块（世界坐标）
var _panning: bool = false
var _pan_start: Vector2 = Vector2.ZERO   # 平移开始时的鼠标屏幕坐标
var _pan_center0: Vector2 = Vector2.ZERO # 平移开始时的 center
var _dragging: bool = false              # 左键拖拽（SELECT 框选 / CROP 裁切）
var _drag_start: Vector2 = Vector2.ZERO  # 拖拽起点（屏幕坐标）
var _drag_cur: Vector2 = Vector2.ZERO    # 拖拽当前点（屏幕坐标）
var _pending_fit: bool = false           # 布局未完成时标记，RESIZED 后执行 fit


# ---------- 数据注入 ----------

func set_texture(tex: Texture2D) -> void:
	_texture = tex
	_rects = []
	_grid = {}
	_selection = Rect2i(-1, -1, 0, 0)
	_selected = []
	_pending_fit = true   # 等布局完成（RESIZED）后适应窗口
	queue_redraw()


func set_rects(rects: Array[Rect2i]) -> void:
	_rects = rects
	queue_redraw()


# Auto 网格布局 overlay（controller auto_diag_changed → 主视图转发）。空字典清除。
func set_grid_overlay(diag: Dictionary) -> void:
	_grid = diag
	queue_redraw()


func set_selection(rect_world: Rect2i) -> void:
	_selection = rect_world
	queue_redraw()


func clear_selection() -> void:
	_selection = Rect2i(-1, -1, 0, 0)
	queue_redraw()


func has_image() -> bool:
	return _texture != null


func get_zoom() -> float:
	return _zoom


func get_zoom_percent() -> int:
	return int(round(_zoom * 100.0))


func get_image_size() -> Vector2:
	if _texture == null:
		return Vector2.ZERO
	return _texture.get_size()


# ---------- 工具模式 ----------

func set_tool(tool: int) -> void:
	_tool = tool
	_dragging = false
	_panning = false
	queue_redraw()


func get_tool() -> int:
	return _tool


# SELECT 命中测试：返回包含 world_p 的最后一个 rect（绘制顺序最上层）；无命中返回无效
func pick_rect_at(world_p: Vector2) -> Rect2i:
	var hit: Rect2i = Rect2i(-1, -1, 0, 0)
	for r: Rect2i in _rects:
		if Rect2(Vector2(r.position), Vector2(r.size)).has_point(world_p):
			hit = r
	return hit


func get_selected_rects() -> Array[Rect2i]:
	return _selected


# ---------- 视图控制（相机式） ----------

# 适应窗口：缩放到图片完整可见并居中
func fit() -> void:
	if _texture == null or size.x <= 0.0 or size.y <= 0.0:
		return
	var img_size: Vector2 = _texture.get_size()
	if img_size.x <= 0.0 or img_size.y <= 0.0:
		return
	# 减 10% 留边距（图片不贴边，观感更好）
	_zoom = clampf(min(size.x / img_size.x, size.y / img_size.y) * 0.9,
			MIN_ZOOM, MAX_ZOOM)
	_center = img_size * 0.5
	queue_redraw()
	view_changed.emit()


func zoom_in() -> void:
	zoom_at(size * 0.5, ZOOM_STEP)


func zoom_out() -> void:
	zoom_at(size * 0.5, 1.0 / ZOOM_STEP)


# 以屏幕锚点缩放：锚点处的世界坐标保持不变（鼠标下缩放）
func zoom_at(anchor_screen: Vector2, factor: float) -> void:
	if _texture == null:
		return
	var world_at: Vector2 = screen_to_world(anchor_screen)
	var new_zoom: float = clampf(_zoom * factor, MIN_ZOOM, MAX_ZOOM)
	if is_equal_approx(new_zoom, _zoom):
		return
	_zoom = new_zoom
	_center = world_at - (anchor_screen - size * 0.5) / _zoom
	queue_redraw()
	view_changed.emit()


# ---------- 坐标变换（世界 = 图片像素坐标，逻辑分辨率恒定） ----------

func view_origin() -> Vector2:
	return size * 0.5 - _center * _zoom


func world_to_screen(p: Vector2) -> Vector2:
	return p * _zoom + view_origin()


func screen_to_world(p: Vector2) -> Vector2:
	return (p - view_origin()) / _zoom


# ---------- 输入 ----------

func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		_handle_mouse_button(event)
	elif event is InputEventMouseMotion:
		_handle_mouse_motion(event)
	elif event is InputEventMagnifyGesture:
		# 触控板双指捏合缩放（macOS）：以手势位置为锚
		var mg: InputEventMagnifyGesture = event
		if has_image():
			zoom_at(mg.position, mg.factor)


func _handle_mouse_button(e: InputEventMouseButton) -> void:
	# 滚轮缩放（所有模式）
	if e.button_index == MOUSE_BUTTON_WHEEL_UP:
		if e.pressed and has_image():
			zoom_at(e.position, ZOOM_STEP)
		return
	if e.button_index == MOUSE_BUTTON_WHEEL_DOWN:
		if e.pressed and has_image():
			zoom_at(e.position, 1.0 / ZOOM_STEP)
		return
	# 中键平移（所有模式）
	if e.button_index == MOUSE_BUTTON_MIDDLE:
		_panning = e.pressed
		if _panning:
			_pan_start = e.position
			_pan_center0 = _center
		return
	# 右键：SELECT 清空选中 / CROP 清除裁切框
	if e.button_index == MOUSE_BUTTON_RIGHT and e.pressed:
		_dragging = false
		_panning = false
		if _tool == Tool.SELECT:
			if not _selected.is_empty():
				_selected = []
				queue_redraw()
				selection_changed.emit(_selected)
		else:
			clear_selection()
			selection_drawn.emit(Rect2i(0, 0, 0, 0))
		return
	# 左键
	if e.button_index != MOUSE_BUTTON_LEFT:
		return
	if e.pressed:
		if e.double_click:
			fit()   # 双击适应窗口
			return
		if _tool == Tool.MOVE:
			_panning = true
			_pan_start = e.position
			_pan_center0 = _center
		else:
			_dragging = true
			_drag_start = e.position
			_drag_cur = e.position
		queue_redraw()
		return
	# 左键松开
	if _panning:
		_panning = false
		return
	if not _dragging:
		return
	_dragging = false
	var drag_len: float = (_drag_cur - _drag_start).length()
	if drag_len < CLICK_THRESHOLD:
		# 点击（SELECT 模式单选）
		if _tool == Tool.SELECT:
			_on_click_select(screen_to_world(e.position))
	else:
		if _tool == Tool.SELECT:
			_on_drag_select()
		elif _tool == Tool.CROP:
			_finish_crop()
	queue_redraw()


func _handle_mouse_motion(e: InputEventMouseMotion) -> void:
	if _panning:
		# 平移：center 随鼠标反向移动（抓取世界）
		_center = _pan_center0 - (e.position - _pan_start) / _zoom
		queue_redraw()
		view_changed.emit()
		return
	if _dragging:
		_drag_cur = e.position
		queue_redraw()


# ---------- 工具行为 ----------

func _on_click_select(world_p: Vector2) -> void:
	var hit: Rect2i = pick_rect_at(world_p)
	if hit.size.x <= 0:
		_selected = []   # 点空白清空选中
	else:
		_selected = [hit]
	selection_changed.emit(_selected)


func _on_drag_select() -> void:
	var drag_rect: Rect2 = _drag_world_rect()
	var sel: Array[Rect2i] = []
	for r: Rect2i in _rects:
		if Rect2(Vector2(r.position), Vector2(r.size)).intersects(drag_rect):
			sel.append(r)
	_selected = sel
	selection_changed.emit(_selected)


func _finish_crop() -> void:
	var w: Rect2 = _drag_world_rect()
	if w.size.x < 3.0 / _zoom or w.size.y < 3.0 / _zoom:
		clear_selection()
		selection_drawn.emit(Rect2i(0, 0, 0, 0))
		return
	var rect: Rect2i = Rect2i(
			Vector2i(int(round(w.position.x)), int(round(w.position.y))),
			Vector2i(maxi(1, int(round(w.size.x))), maxi(1, int(round(w.size.y)))))
	_selection = rect
	queue_redraw()
	selection_drawn.emit(rect)


# 拖拽屏幕矩形 → 世界矩形（线性变换：无旋转，缩放+平移）
func _drag_world_rect() -> Rect2:
	var tl: Vector2 = Vector2(min(_drag_start.x, _drag_cur.x), min(_drag_start.y, _drag_cur.y))
	var br: Vector2 = Vector2(max(_drag_start.x, _drag_cur.x), max(_drag_start.y, _drag_cur.y))
	var w0: Vector2 = screen_to_world(tl)
	var w1: Vector2 = screen_to_world(br)
	return Rect2(w0, w1 - w0)


# ---------- 绘制 ----------

func _notification(what: int) -> void:
	if what == NOTIFICATION_RESIZED:
		if _pending_fit:
			_pending_fit = false
			fit()
		queue_redraw()


func _draw() -> void:
	# 画布底色
	draw_rect(Rect2(Vector2.ZERO, size), BG_COLOR, true)
	if _texture == null:
		_draw_empty_hint()   # 无图：居中拖放提示
		return
	# 相机式变换：之后所有世界坐标绘制自动缩放/平移
	draw_set_transform(view_origin(), 0.0, Vector2(_zoom, _zoom))
	# 图片（世界原点 = 图片左上角）
	draw_texture(_texture, Vector2.ZERO)
	# 线宽除以 zoom 保持恒定屏幕像素
	var lw: float = 2.0 / _zoom
	# Auto 网格布局 overlay（灰色 cell 线，先于红框绘制）
	if not _grid.is_empty():
		_draw_grid_overlay()
	# 切分红框
	for r: Rect2i in _rects:
		draw_rect(Rect2(Vector2(r.position), Vector2(r.size)), RECT_COLOR, false, lw)
	# SELECT：选中方块高亮（黄）
	if _tool == Tool.SELECT:
		for r: Rect2i in _selected:
			var rr: Rect2 = Rect2(Vector2(r.position), Vector2(r.size))
			draw_rect(rr, SEL_HL_FILL, true)
			draw_rect(rr, SEL_HL_STROKE, false, lw)
	# CROP：裁切框（青）
	if _tool == Tool.CROP and _selection.size.x > 0 and _selection.size.y > 0:
		var cr: Rect2 = Rect2(Vector2(_selection.position), Vector2(_selection.size))
		draw_rect(cr, CROP_FILL, true)
		draw_rect(cr, CROP_STROKE, false, lw)
	# 拖拽中的临时框（按工具着色）
	if _dragging:
		var dr: Rect2 = _drag_world_rect()
		if _tool == Tool.SELECT:
			draw_rect(dr, SEL_HL_FILL, true)
			draw_rect(dr, SEL_HL_STROKE, false, lw)
		elif _tool == Tool.CROP:
			draw_rect(dr, CROP_FILL, true)
			draw_rect(dr, CROP_STROKE, false, lw)


# Auto 网格布局 overlay：从 offset 起每 cell 尺寸画一条线（columns+1 竖、rows+1 横），
# 超出图像范围的线跳过。线宽 1 屏幕像素。
func _draw_grid_overlay() -> void:
	var cw: int = int(_grid.get("auto_grid_cell_w", 0))
	var ch: int = int(_grid.get("auto_grid_cell_h", 0))
	var cols: int = int(_grid.get("auto_grid_columns", 0))
	var rows: int = int(_grid.get("auto_grid_rows", 0))
	var ox: int = int(_grid.get("auto_grid_offset_x", 0))
	var oy: int = int(_grid.get("auto_grid_offset_y", 0))
	if cw <= 0 or ch <= 0 or cols <= 0 or rows <= 0:
		return
	var img_size: Vector2 = get_image_size()
	var line_w: float = 1.0 / _zoom
	for c: int in cols + 1:
		var x: float = ox + c * cw
		if x < 0.0 or x > img_size.x:
			continue
		draw_line(Vector2(x, 0), Vector2(x, img_size.y), GRID_COLOR, line_w)
	for r: int in rows + 1:
		var y: float = oy + r * ch
		if y < 0.0 or y > img_size.y:
			continue
		draw_line(Vector2(0, y), Vector2(img_size.x, y), GRID_COLOR, line_w)


# 无图时：画布居中显示拖放提示（引导从 FileSystem dock 拖入素材/配置）
func _draw_empty_hint() -> void:
	var msg: String = "拖入图片 或 .tres 配置\n\n支持 png / jpg / jpeg"
	var font: Font = ThemeDB.fallback_font
	var fs: int = 18
	var ms: Vector2 = font.get_multiline_string_size(msg, HORIZONTAL_ALIGNMENT_CENTER,
			-1.0, fs)
	var pos: Vector2 = (size - ms) * 0.5
	draw_multiline_string(font, pos, msg, HORIZONTAL_ALIGNMENT_CENTER, -1.0, fs,
			-1, Color(0.62, 0.62, 0.66, 0.9))


# ---------- 编辑器 FileSystem dock 拖放（拖到预览区画布上） ----------
# data 格式：{"type": "files", "files": PackedStringArray}；支持 png/jpg/jpeg/tres
func _can_drop_data(_pos: Vector2, data: Variant) -> bool:
	if not (data is Dictionary):
		return false
	if String(data.get("type", "")) != "files":
		return false
	var files: Variant = data.get("files", [])
	if not (files is PackedStringArray):
		return false
	for f: String in files:
		var ext: String = f.get_extension().to_lower()
		if ext in ["png", "jpg", "jpeg"] or ext == "tres":
			return true
	return false


func _drop_data(_pos: Vector2, data: Variant) -> void:
	if not (data is Dictionary):
		return
	var files: Variant = data.get("files", [])
	if not (files is PackedStringArray):
		return
	for f: String in files:
		var ext: String = f.get_extension().to_lower()
		if ext in ["png", "jpg", "jpeg"] or ext == "tres":
			drop_requested.emit(f)   # 主视图接信号 → 确认弹窗
			return
