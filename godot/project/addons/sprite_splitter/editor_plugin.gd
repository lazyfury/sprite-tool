@tool
extends EditorPlugin

## Sprite Splitter 编辑器插件（M5.3）—— 主屏幕 + 侧栏 dock 双挂载：
##   - 主屏幕（EditorInterface.get_editor_main_screen()）：全屏画布预览
##     （图片/切分红框/框选 + 工具条 移动/选择/裁切 + 缩放），顶部标签栏
##     2D｜3D｜Sprite Splitter 切换，参考 limboai LimboAIEditorPlugin。
##   - 侧栏 dock（DOCK_SLOT_RIGHT_BL）：操作面板（打开素材/参数/分析/去背景/
##     切分/导入 meta.json/导出）。
## 两视图共享 SpsController（业务逻辑），跨区域交互由信号桥接。
## 保留 Tools 菜单快捷入口：选 PNG → 自动切分导出 res://sprites/。
##
## 编码约定（项目强制）：var 显式类型标注，不用 :=。

const MENU_NAME: String = "Sprite Splitter..."
const MAIN_SCENE: PackedScene = preload("res://addons/sprite_splitter/ui/sprite_splitter_main.tscn")
const SIDE_SCENE: PackedScene = preload("res://addons/sprite_splitter/ui/sprite_splitter_side.tscn")
const ICON_PATH: String = "res://addons/sprite_splitter/icon.svg"

var _controller: SpsController = null
var _main_ui: Control = null
var _side_ui: Control = null
var _dialog: EditorFileDialog = null


func _enter_tree() -> void:
	_controller = SpsController.new(get_tree())
	_main_ui = MAIN_SCENE.instantiate()
	_main_ui.set_v_size_flags(Control.SIZE_EXPAND_FILL)
	EditorInterface.get_editor_main_screen().add_child(_main_ui)
	_main_ui.hide()   # 默认隐藏，点主屏幕标签时 _make_visible 切换
	_main_ui.set_controller(_controller)
	_side_ui = SIDE_SCENE.instantiate()
	add_control_to_dock(EditorPlugin.DOCK_SLOT_RIGHT_BL, _side_ui)
	_side_ui.set_controller(_controller)
	_main_ui.set_side(_side_ui)   # 主视图需要侧栏当前参数（注册表切换前保存用）
	add_tool_menu_item(MENU_NAME, _on_menu_split)
	print("[sps-plugin] Sprite Splitter plugin active (C++ core loaded: ", _controller.splitter != null, ")")


func _exit_tree() -> void:
	remove_tool_menu_item(MENU_NAME)
	if _main_ui != null:
		EditorInterface.get_editor_main_screen().remove_child(_main_ui)
		_main_ui.queue_free()
	if _side_ui != null:
		remove_control_from_docks(_side_ui)
		_side_ui.queue_free()
	if _dialog != null:
		_dialog.queue_free()
	# SpsController/SpriteSplitter 是 RefCounted：置 null 由引用计数释放，禁止 free()
	_controller = null


# ---------- 主屏幕标签（参考 limboai LimboAIEditorPlugin） ----------

# 必须返回 true，插件才会出现在 2D/3D/Script 旁边的工作区选择器
func _has_main_screen() -> bool:
	return true


func _get_plugin_name() -> String:
	return "Sprite Splitter"


func _get_plugin_icon() -> Texture2D:
	var icon: Texture2D = load(ICON_PATH)
	if icon == null:
		# svg 尚未导入时兜底用编辑器内置图标，避免主屏幕按钮创建失败
		return EditorInterface.get_editor_theme().get_icon("Node", "EditorIcons")
	return icon


func _make_visible(p_visible: bool) -> void:
	_main_ui.visible = p_visible


# ---------- Tools 菜单快捷入口（一键切分导出） ----------

func _on_menu_split() -> void:
	if _dialog == null:
		_dialog = EditorFileDialog.new()
		_dialog.title = "Select sprite sheet (PNG)"
		_dialog.access = EditorFileDialog.ACCESS_FILESYSTEM
		_dialog.file_mode = EditorFileDialog.FILE_MODE_OPEN_FILE
		_dialog.filters = PackedStringArray(["*.png ; PNG Image"])
		_dialog.file_selected.connect(_on_file_selected)
		EditorInterface.get_base_control().add_child(_dialog)
	_dialog.popup_centered_ratio(0.6)


func _on_file_selected(path: String) -> void:
	if _controller == null:
		return
	if not _controller.load_image(path):
		return
	var opts: Dictionary = {
		"mode": "auto",
		"min_width": 2,
		"min_height": 2,
	}
	var files: PackedStringArray = _controller.splitter.split_and_export(
			_controller.image, opts, "res://sprites")
	print("[sps-plugin] exported ", files.size(), " sprites to res://sprites/")
	for f: String in files:
		print("[sps-plugin]   ", f)
	if files.is_empty():
		printerr("[sps-plugin] no sprites found (check threshold/options)")
