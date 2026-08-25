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
signal geometry_committed(index: int, rect: Rect2i)   # 编辑提交：拖拽移动 / 四角缩放（松手时发）

enum Tool { MOVE, SELECT, EDIT, CROP }
# 编辑拖拽模式（EDIT 工具的几何编辑：拖拽移动 / 四角缩放）
enum DragMode { NONE, MOVE, RESIZE_NW, RESIZE_NE, RESIZE_SW, RESIZE_SE }

const MIN_ZOOM: float = 0.02
const MAX_ZOOM: float = 64.0
const ZOOM_STEP: float = 1.2
const CLICK_THRESHOLD: float = 5.0     # 按下-松开位移小于此值视为点击（屏幕 px）
const HANDLE_SIZE: float = 7.0         # 编辑手柄命中半径（屏幕 px）
const HANDLE_FILL: Color = Color(1.0, 1.0, 1.0, 0.95)
const HANDLE_STROKE: Color = Color(0.25, 0.25, 0.28, 1.0)
const HANDLE_LOCKED: Color = Color(0.6, 0.6, 0.62, 0.5)   # 锁定项手柄（灰）
const BG_COLOR: Color = Color(0.097, 0.104, 0.1, 1.0)
const RECT_COLOR: Color = Color(1.0, 0.25, 0.25, 0.95)          # 切分红框
const GRID_COLOR: Color = Color(0.75, 0.75, 0.78, 0.35)         # Auto 网格布局线（灰，调试决策用）
const SEL_HL_FILL: Color = Color(1.0, 0.85, 0.2, 0.28)          # SELECT 选中填充（黄）
const SEL_HL_STROKE: Color = Color(1.0, 0.85, 0.2, 0.95)        # SELECT 选中描边
const CROP_FILL: Color = Color(0.3, 0.9, 1.0, 0.18)             # CROP 裁切框填充（青）
const CROP_STROKE: Color = Color(0.3, 0.9, 1.0, 0.95)           # CROP 裁切框描边

var _texture: Texture2D = null
var _sprites: Array[Dictionary] = []   # 复杂切片结构（含 name/locked/ignored）
var _rects: Array[Rect2i] = []         # 由 _sprites 派生（绘制/命中）
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
var _edit_index: int = -1                # SELECT 编辑对象（显示手柄）：sprite 索引；-1 无
var _drag_mode: int = DragMode.NONE      # 编辑拖拽模式（MOVE/RESIZE_*）
var _drag_origin: Rect2i = Rect2i()      # 编辑拖拽起始 rect
var _drag_anchor: Vector2 = Vector2.ZERO # 编辑拖拽起始鼠标世界坐标


# ---------- 数据注入 ----------

func set_texture(tex: Texture2D) -> void:
	_texture = tex
	_rects = []
	_sprites = []
	_grid = {}
	_selection = Rect2i(-1, -1, 0, 0)
	_selected = []
	_edit_index = -1
	_drag_mode = DragMode.NONE
	_pending_fit = true   # 等布局完成（RESIZED）后适应窗口
	queue_redraw()


func set_rects(rects_in: Array[Rect2i]) -> void:
	# 兼容旧接口：转为复杂结构（uid/name 自动生成）
	var sp: Array[Dictionary] = []
	for i: int in rects_in.size():
		var r: Rect2i = rects_in[i]
		sp.append({
			"uid": "sprite_%d" % (i + 1),
			"name": "精灵 %d" % (i + 1),
			"x": r.position.x, "y": r.position.y,
			"width": r.size.x, "height": r.size.y,
			"locked": false, "ignored": false,
		})
	set_sprites(sp)


# 复杂切片结构注入（controller sprites_changed → main 转发）
func set_sprites(sprites_in: Array[Dictionary]) -> void:
	_sprites = sprites_in
	_rects = []
	for s: Dictionary in _sprites:
		_rects.append(Rect2i(int(s.get("x", 0)), int(s.get("y", 0)),
				int(s.get("width", 0)), int(s.get("height", 0))))
	_edit_index = -1
	_selected = []
	_drag_mode = DragMode.NONE
	queue_redraw()


func _is_locked(index: int) -> bool:
	if index < 0 or index >= _sprites.size():
		return true
	return bool(_sprites[index].get("locked", false))


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
	_drag_mode = DragMode.NONE
	_edit_index = -1   # 切工具清编辑态
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


# 返回命中的 sprite 索引（编辑用）；无命中 -1
func pick_sprite_index_at(world_p: Vector2) -> int:
	for i: int in range(_rects.size() - 1, -1, -1):
		var r: Rect2i = _rects[i]
		if Rect2(Vector2(r.position), Vector2(r.size)).has_point(world_p):
			return i
	return -1


# 四角手柄命中：返回 DragMode（RESIZE_NW/NE/SW/SE）；未命中 DragMode.NONE
func _handle_at(world_p: Vector2, r: Rect2i) -> int:
	var h: float = HANDLE_SIZE / _zoom
	var corners: Array[Vector2] = [
		Vector2(r.position),                                   # NW
		Vector2(r.end.x, r.position.y),                        # NE
		Vector2(r.position.x, r.end.y),                        # SW
		Vector2(r.end),                                        # SE
	]
	for i: int in corners.size():
		if world_p.distance_to(corners[i]) <= h:
			return DragMode.RESIZE_NW + i
	return DragMode.NONE


# 编辑拖拽应用：根据 _drag_mode 计算新 rect（move 平移 / resize 按角调整），clamp 图内
func _apply_edit_drag(wp: Vector2) -> void:
	if _edit_index < 0 or _drag_mode == DragMode.NONE:
		return
	var delta: Vector2 = wp - _drag_anchor
	var o: Rect2i = _drag_origin
	var img: Vector2 = get_image_size()
	var x0: float = o.position.x
	var y0: float = o.position.y
	var x1: float = o.end.x
	var y1: float = o.end.y
	match _drag_mode:
		DragMode.MOVE:
			x0 = o.position.x + delta.x
			y0 = o.position.y + delta.y
			x1 = o.end.x + delta.x
			y1 = o.end.y + delta.y
		DragMode.RESIZE_NW:
			x0 = minf(o.end.x - 1, o.position.x + delta.x)
			y0 = minf(o.end.y - 1, o.position.y + delta.y)
		DragMode.RESIZE_NE:
			x1 = maxf(o.position.x + 1, o.end.x + delta.x)
			y0 = minf(o.end.y - 1, o.position.y + delta.y)
		DragMode.RESIZE_SW:
			x0 = minf(o.end.x - 1, o.position.x + delta.x)
			y1 = maxf(o.position.y + 1, o.end.y + delta.y)
		DragMode.RESIZE_SE:
			x1 = maxf(o.position.x + 1, o.end.x + delta.x)
			y1 = maxf(o.position.y + 1, o.end.y + delta.y)
	# clamp 到图像边界
	if img.x > 0.0:
		x0 = clampf(x0, 0.0, img.x - 1.0)
		x1 = clampf(x1, 1.0, img.x)
	if img.y > 0.0:
		y0 = clampf(y0, 0.0, img.y - 1.0)
		y1 = clampf(y1, 1.0, img.y)
	_rects[_edit_index] = Rect2i(int(round(x0)), int(round(y0)),
			int(round(x1 - x0)), int(round(y1 - y0)))
	queue_redraw()


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
	# 右键：SELECT/EDIT 清空选中与编辑态 / CROP 清除裁切框
	if e.button_index == MOUSE_BUTTON_RIGHT and e.pressed:
		_dragging = false
		_panning = false
		if _tool == Tool.SELECT or _tool == Tool.EDIT:
			if not _selected.is_empty() or _edit_index >= 0:
				_selected = []
				_edit_index = -1
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
			# EDIT 工具：已有编辑对象 → 手柄/本体命中优先（拖拽移动 / 四角缩放）
			if _tool == Tool.EDIT and _edit_index >= 0 and not _is_locked(_edit_index):
				var wp: Vector2 = screen_to_world(e.position)
				var hnd: int = _handle_at(wp, _rects[_edit_index])
				if hnd != DragMode.NONE:
					_drag_mode = hnd
					_drag_origin = _rects[_edit_index]
					_drag_anchor = wp
					_dragging = true
					queue_redraw()
					return
				var er: Rect2 = Rect2(Vector2(_rects[_edit_index].position),
						Vector2(_rects[_edit_index].size))
				if er.has_point(wp):
					_drag_mode = DragMode.MOVE
					_drag_origin = _rects[_edit_index]
					_drag_anchor = wp
					_dragging = true
					queue_redraw()
					return
			_dragging = true
			_drag_start = e.position
			_drag_cur = e.position
		queue_redraw()
		return
	# 左键松开
	if _panning:
		_panning = false
		return
	if _dragging and _drag_mode != DragMode.NONE:
		# 编辑拖拽结束：提交几何变更（锁定项不提交）
		var idx: int = _edit_index
		var new_rect: Rect2i = _rects[idx]
		_dragging = false
		_drag_mode = DragMode.NONE
		if not _is_locked(idx):
			geometry_committed.emit(idx, new_rect)
		queue_redraw()
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
		if _drag_mode != DragMode.NONE:
			# 编辑拖拽：本地实时更新显示（松手才提交）
			_apply_edit_drag(screen_to_world(e.position))
			return
		queue_redraw()


# ---------- 工具行为 ----------

func _on_click_select(world_p: Vector2) -> void:
	var idx: int = pick_sprite_index_at(world_p)
	if idx < 0:
		_selected = []   # 点空白清空选中
		_edit_index = -1
	else:
		_selected = [_rects[idx]]
		# 仅 EDIT 工具进入编辑态（显示四角手柄）；SELECT 只负责选中/框选
		_edit_index = idx if _tool == Tool.EDIT else -1
	selection_changed.emit(_selected)


func _on_drag_select() -> void:
	var drag_rect: Rect2 = _drag_world_rect()
	var sel: Array[Rect2i] = []
	for r: Rect2i in _rects:
		if Rect2(Vector2(r.position), Vector2(r.size)).intersects(drag_rect):
			sel.append(r)
	_selected = sel
	_edit_index = -1   # 框选多选不进编辑态
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
	# 编辑手柄（EDIT 工具单选编辑态：四角小方块；锁定项灰色，不可拖拽/缩放）
	if _tool == Tool.EDIT and _edit_index >= 0 and _edit_index < _rects.size():
		var er: Rect2 = Rect2(Vector2(_rects[_edit_index].position),
				Vector2(_rects[_edit_index].size))
		var h: float = HANDLE_SIZE / _zoom
		var fill: Color = HANDLE_LOCKED if _is_locked(_edit_index) else HANDLE_FILL
		var corners: Array[Vector2] = [er.position,
				Vector2(er.end.x, er.position.y),
				Vector2(er.position.x, er.end.y),
				er.end]
		for c: Vector2 in corners:
			draw_rect(Rect2(c - Vector2(h, h), Vector2(h * 2, h * 2)), fill, true)
			draw_rect(Rect2(c - Vector2(h, h), Vector2(h * 2, h * 2)),
					HANDLE_STROKE, false, 1.0 / _zoom)
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
