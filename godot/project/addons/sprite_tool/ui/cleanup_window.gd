@tool
extends Window

## 生成资源清理窗口：列出输出注册表记录的全部生成资源，逐个检查占用状态
## （插件自身使用 / 项目文本引用），仅清理未占用资源（占用项拒绝删除）。
## 复用 SpsController 的 check_outputs_usage / cleanup_outputs，与 Sheet 窗口
## 同级独立顶级窗口（main Header「清理资源」打开，复用单例，关闭仅 hide）。
##
## 编码约定（项目强制）：var 显式类型标注，不用 :=。

var _controller: SpsController = null
var _usage: Dictionary = {}   # {path: occupied}（refresh 时由 controller 扫描）

@onready var _summary: Label = get_node("VBox/Row/SummaryLabel")
@onready var _refresh_btn: Button = get_node("VBox/Row/RefreshBtn")
@onready var _list: ItemList = get_node("VBox/ListArea/List")
@onready var _empty: Label = get_node("VBox/ListArea/EmptyLabel")
@onready var _clean_sel_btn: Button = get_node("VBox/ActionRow/CleanSelBtn")
@onready var _clean_all_btn: Button = get_node("VBox/ActionRow/CleanAllBtn")
@onready var _close_btn: Button = get_node("VBox/ActionRow/CloseBtn")
@onready var _result_label: Label = get_node("VBox/ResultLabel")

var _confirm: ConfirmationDialog = null
var _pending_clean: Array[String] = []   # 待确认删除的路径


func set_controller(c: SpsController) -> void:
	_controller = c
	if _controller != null:
		_controller.status_changed.connect(_on_status)


func _ready() -> void:
	close_requested.connect(_hide_self)
	_refresh_btn.pressed.connect(refresh)
	_clean_sel_btn.pressed.connect(_on_clean_selected)
	_clean_all_btn.pressed.connect(_on_clean_all)
	_close_btn.pressed.connect(_hide_self)
	_list.item_activated.connect(_on_item_activated)
	refresh()


func _hide_self() -> void:
	hide()   # 复用单例：关闭仅隐藏，下次点击直接显示


func _on_status(text: String, is_error: bool) -> void:
	_result_label.text = text
	_result_label.add_theme_color_override("font_color",
			Color(1.0, 0.4, 0.4) if is_error else Color(0.7, 1.0, 0.7))


# 刷新：重扫占用状态 + 重建列表（purge 已消失的幽灵条目）
func refresh() -> void:
	_result_label.text = ""
	_usage = {}
	if _controller == null or _controller.output_registry == null:
		_rebuild_list()
		return
	_controller.output_registry.purge_missing()
	_usage = _controller.check_outputs_usage()
	_rebuild_list()


func _rebuild_list() -> void:
	_list.clear()
	var reg: SpriteOutputRegistry = _controller.output_registry \
			if _controller != null else null
	if reg == null or reg.entries.is_empty():
		_empty.visible = true
		_summary.text = "暂无生成资源"
		_clean_sel_btn.disabled = true
		_clean_all_btn.disabled = true
		return
	_empty.visible = false
	var occupied_n: int = 0
	for e: Dictionary in reg.entries:
		var path: String = String(e.get("path", ""))
		var occ: bool = bool(_usage.get(path, false))
		if occ:
			occupied_n += 1
		var idx: int = _list.add_item(_item_text(e, occ))
		_list.set_item_metadata(idx, path)
		_list.set_item_tooltip(idx, path)
		_list.set_item_custom_fg_color(idx, _status_color(occ))
	_summary.text = "共 %d 个 | 占用 %d | 可清理 %d" % [
			reg.entries.size(), occupied_n, reg.entries.size() - occupied_n]
	_clean_sel_btn.disabled = false
	_clean_all_btn.disabled = occupied_n >= reg.entries.size()


func _item_text(e: Dictionary, occupied: bool) -> String:
	var kind: String = String(e.get("kind", ""))
	var name: String = String(e.get("path", "")).get_file()
	var size: int = int(e.get("size", 0))
	var time_s: String = ""
	var ts: int = int(e.get("created_at", 0))
	if ts > 0:
		var dt: Dictionary = Time.get_datetime_dict_from_unix_time(ts)
		time_s = " %02d-%02d %02d:%02d" % [dt.month, dt.day, dt.hour, dt.minute]
	return "%s [%s] %s (%s%s)" % [
			"🔴 占用" if occupied else "🟢 可清理",
			kind, name, _fmt_size(size), time_s]


func _fmt_size(bytes: int) -> String:
	if bytes < 1024:
		return "%dB" % bytes
	if bytes < 1024 * 1024:
		return "%.1fKB" % (bytes / 1024.0)
	return "%.1fMB" % (bytes / 1048576.0)


func _status_color(occupied: bool) -> Color:
	return Color(1.0, 0.45, 0.45) if occupied else Color(0.6, 1.0, 0.65)


# 双击条目 → 提示占用状态（不自动删除，避免误删）
func _on_item_activated(index: int) -> void:
	var path: String = String(_list.get_item_metadata(index))
	if _usage.has(path) and bool(_usage[path]):
		_result_label.text = "🔴 占用中，不能清理：" + path
		_result_label.add_theme_color_override("font_color", Color(1.0, 0.4, 0.4))
	else:
		_result_label.text = "🟢 可清理：" + path
		_result_label.add_theme_color_override("font_color", Color(0.7, 1.0, 0.7))


func _on_clean_selected() -> void:
	var sel: Array = _list.get_selected_items()
	if sel.is_empty():
		_result_label.text = "先在列表中选择要清理的资源"
		_result_label.add_theme_color_override("font_color", Color(1.0, 0.4, 0.4))
		return
	var paths: Array[String] = []
	for idx: Variant in sel:
		paths.append(String(_list.get_item_metadata(int(idx))))
	_request_confirm(paths)


func _on_clean_all() -> void:
	if _controller == null:
		return
	var paths: Array[String] = []
	for e: Dictionary in _controller.output_registry.entries:
		var p: String = String(e.get("path", ""))
		if not bool(_usage.get(p, false)):
			paths.append(p)
	if paths.is_empty():
		_result_label.text = "没有可清理的资源"
		_result_label.add_theme_color_override("font_color", Color(1.0, 0.4, 0.4))
		return
	_request_confirm(paths)


func _request_confirm(paths: Array[String]) -> void:
	if _confirm == null:
		_confirm = ConfirmationDialog.new()
		_confirm.confirmed.connect(_on_confirm_clean)
		add_child(_confirm)
	_confirm.dialog_text = "将删除 %d 个未占用的生成资源（不可撤销）：\n%s\n确认清理？" % [
			paths.size(), paths[0] + ("\n…等" if paths.size() > 1 else "")]
	_confirm.ok_button_text = "确认删除"
	_pending_clean = paths
	_confirm.popup_centered()


func _on_confirm_clean() -> void:
	if _controller == null or _pending_clean.is_empty():
		return
	var result: Dictionary = _controller.cleanup_outputs(_pending_clean)
	_pending_clean = []
	refresh()
	var refused: Array = result.get("refused", [])
	var note: String = ""
	if not refused.is_empty():
		note = "（%d 个占用项已跳过）" % refused.size()
	_on_status("已清理 %d 个资源%s" % [int(result.get("deleted", 0)), note],
			not refused.is_empty())
