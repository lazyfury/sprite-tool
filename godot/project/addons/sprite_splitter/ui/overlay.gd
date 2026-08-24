extends Control

## 切分区域叠加层：主脚本通过 set_rects()/set_view() 注入数据，_draw() 描边。
## 编码约定：var 显式类型（不用 :=）。

var _rects: Array[Rect2i] = []
var _scale: float = 1.0
var _offset: Vector2 = Vector2.ZERO
var _show: bool = true

func set_rects(rects: Array[Rect2i]) -> void:
	_rects = rects
	queue_redraw()

func set_view(scale: float, offset: Vector2) -> void:
	_scale = scale
	_offset = offset
	queue_redraw()

func set_show(value: bool) -> void:
	_show = value
	queue_redraw()

func clear() -> void:
	_rects = []
	queue_redraw()

func _draw() -> void:
	if not _show or _rects.is_empty():
		return
	var color: Color = Color(1.0, 0.25, 0.25, 0.95)
	for r: Rect2i in _rects:
		var view_rect: Rect2 = Rect2(
				_offset + Vector2(r.position) * _scale,
				Vector2(r.size) * _scale)
		draw_rect(view_rect, color, false, 1.5)
