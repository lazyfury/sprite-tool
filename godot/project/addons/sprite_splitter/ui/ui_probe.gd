extends RefCounted

## 插件 UI 插桩（instrumentation）：遍历 Control 树，记录每个组件
## 1) 位置信息：position/size/global_rect/锚点/偏移/size_flags/可见性
## 2) 影响数据：实际生效的主题项（font/font_size/line_spacing/font_color/面板样式）
##    + override 列表（代码/tscn 覆盖 vs 主题继承，可判断"谁影响了显示"）
## 输出 JSON 报告供布局与主题影响分析。不修改任何 UI，只读。
##
## 用法：UiProbeScript.write_report(root_node, "res://out_probe/ui_probe.json")
## 编码约定（项目强制）：var 显式类型标注，不用 :=。

const OUT_DIR: String = "res://out_probe"

# 关键主题项名（Label/容器通用）
const KEY_ITEMS: Array[String] = ["font", "font_size", "line_spacing",
		"font_color", "font_shadow_color", "separation"]


static func probe_tree(root: Node) -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	_walk(root, out)
	return out


static func _walk(n: Node, out: Array[Dictionary]) -> void:
	if n is Control:
		out.append(_probe_control(n))
	for child: Node in n.get_children():
		_walk(child, out)


# 单个 Control 的插桩数据（纯 JSON 类型，Vector2/Rect2 转数组）
static func _probe_control(c: Control) -> Dictionary:
	var d: Dictionary = {
		"node": String(c.get_path()),
		"class": c.get_class(),
		"visible": c.is_visible_in_tree(),
		"pos": [c.position.x, c.position.y],
		"size": [c.size.x, c.size.y],
		"gpos": [c.global_position.x, c.global_position.y],
		"grect": _rect_to_arr(c.get_global_rect()),
		"min_size": [c.custom_minimum_size.x, c.custom_minimum_size.y],
		"size_flags_h": c.size_flags_horizontal,
		"size_flags_v": c.size_flags_vertical,
		"mouse_filter": c.mouse_filter,
	}
	# 锚点/偏移（父为 Control 时，决定自适应行为）
	var parent: Node = c.get_parent()
	if parent is Control:
		d["anchor"] = [c.anchor_left, c.anchor_top, c.anchor_right, c.anchor_bottom]
		d["offset"] = [c.offset_left, c.offset_top, c.offset_right, c.offset_bottom]
	# 自身 theme 属性（是否有局部 theme / 变体）
	if c.theme != null:
		d["own_theme"] = String(c.theme.resource_path)
	if not c.theme_type_variation.is_empty():
		d["theme_variation"] = c.theme_type_variation
	# ---- 影响数据：主题 override 列表（代码/tscn 覆盖） ----
	d["overrides"] = _collect_overrides(c)
	# ---- 实际生效值（get_theme_* = 覆盖优先，否则继承主题/默认） ----
	d["theme_eff"] = _collect_effective(c)
	# ---- 特化：Label / ItemList / Button ----
	if c is Label:
		var lb: Label = c
		d["text"] = lb.text
		d["line_count"] = lb.get_line_count()
		d["v_align"] = lb.vertical_alignment
		d["h_align"] = lb.horizontal_alignment
		d["autowrap"] = lb.autowrap_mode
		d["overrun"] = lb.text_overrun_behavior
		d["clip"] = lb.clip_text
		# label_settings 渲染层优先于主题，且 get_theme_* 查不到 → 必须单独记录
		if lb.label_settings != null:
			var ls: LabelSettings = lb.label_settings
			d["label_settings"] = {
				"path": String(ls.resource_path),
				"font_size": ls.font_size,
				"line_spacing": ls.line_spacing,
				"font_color": _to_hex(ls.font_color),
			}
	if c is ItemList:
		var il: ItemList = c
		d["item_count"] = il.item_count
		d["fixed_icon_size"] = [il.fixed_icon_size.x, il.fixed_icon_size.y]
		d["icon_mode"] = il.icon_mode
	if c is Button:
		var bt: Button = c
		d["text"] = bt.text
		d["toggle"] = bt.toggle_mode
	return d


# 主题 override 收集：Godot 4.6 无 get_theme_*_override_list API，
# 改为对关注的 key 逐个 has_theme_*_override 检查（记录"谁被代码/tscn 覆盖了"）
static func _collect_overrides(c: Control) -> Dictionary:
	var out: Dictionary = {}
	for item: String in ["line_spacing", "separation"]:
		if c.has_theme_constant_override(item):
			_ov_append(out, "constant", item)
	for item: String in ["font_color", "font_shadow_color"]:
		if c.has_theme_color_override(item):
			_ov_append(out, "color", item)
	if c.has_theme_font_override("font"):
		_ov_append(out, "font", "font")
	if c.has_theme_font_size_override("font_size"):
		_ov_append(out, "font_size", "font_size")
	if c.has_theme_stylebox_override("panel"):
		_ov_append(out, "stylebox", "panel")
	return out


static func _ov_append(d: Dictionary, key: String, item: String) -> void:
	if not d.has(key):
		d[key] = []
	d[key].append(item)


# 实际生效主题值（结果 = override 优先，否则继承）
static func _collect_effective(c: Control) -> Dictionary:
	var e: Dictionary = {
		"font": _font_desc(c.get_theme_font("font")),
		"font_size": c.get_theme_font_size("font_size"),
		"line_spacing": c.get_theme_constant("line_spacing"),
		"font_color": _to_hex(c.get_theme_color("font_color")),
	}
	var sb: StyleBox = c.get_theme_stylebox("panel")
	if sb is StyleBoxFlat:
		var flat: StyleBoxFlat = sb
		e["panel_bg"] = _to_hex(flat.bg_color)
		e["panel_radius"] = [flat.corner_radius_top_left, flat.corner_radius_top_right,
				flat.corner_radius_bottom_right, flat.corner_radius_bottom_left]
		e["panel_margin"] = [flat.content_margin_left, flat.content_margin_top,
				flat.content_margin_right, flat.content_margin_bottom]
	return e


static func _font_desc(f: Font) -> Dictionary:
	if f == null:
		return {}
	var d: Dictionary = {"name": String(f.resource_name), "path": String(f.resource_path)}
	if f is SystemFont:
		d["system"] = _to_str_array(f.get_font_names())
	return d


static func _to_str_array(a: PackedStringArray) -> Array[String]:
	var out: Array[String] = []
	for s: String in a:
		out.append(s)
	return out


static func _rect_to_arr(r: Rect2) -> Array:
	return [r.position.x, r.position.y, r.size.x, r.size.y]


static func _to_hex(col: Color) -> String:
	return "#%02x%02x%02x" % [int(col.r * 255), int(col.g * 255), int(col.b * 255)]


# 遍历 root 全部 Control，写 JSON 报告，返回报告
static func write_report(root: Node, out_path: String) -> Dictionary:
	var nodes: Array[Dictionary] = probe_tree(root)
	var report: Dictionary = {
		"root": String(root.name),
		"generated_at": int(Time.get_unix_time_from_system()),
		"count": nodes.size(),
		"controls": nodes,
	}
	if not DirAccess.dir_exists_absolute(OUT_DIR):
		DirAccess.make_dir_recursive_absolute(OUT_DIR)
	var f: FileAccess = FileAccess.open(out_path, FileAccess.WRITE)
	if f == null:
		printerr("[ui_probe] cannot write ", out_path)
		return report
	f.store_string(JSON.stringify(report, "\t"))
	f.close()
	print("[ui_probe] wrote ", out_path, " (", nodes.size(), " controls)")
	return report
