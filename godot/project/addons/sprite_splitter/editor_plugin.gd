@tool
extends EditorPlugin

# Sprite Splitter 编辑器插件（M5 最小可用版）：
#   菜单 Tools → "Sprite Splitter..." → 选 PNG 素材表 → 自动切分导出到 res://sprites/
# 核心算法在 C++ GDExtension（SpriteSplitter 类），本脚本只做编辑器 UI 薄封装。

const PLUGIN_DIR := "res://addons/sprite_splitter"
const MENU_NAME := "Sprite Splitter..."

var _splitter: SpriteSplitter
var _dialog: EditorFileDialog

func _enter_tree() -> void:
    _splitter = SpriteSplitter.new()
    add_tool_menu_item(MENU_NAME, _on_menu_split)
    print("[sps-plugin] Sprite Splitter plugin active (C++ core loaded: ",
            _splitter != null, ")")

func _exit_tree() -> void:
    remove_tool_menu_item(MENU_NAME)
    if _dialog != null:
        _dialog.queue_free()
    if _splitter != null:
        _splitter.free()

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
    var img := Image.load_from_file(path)
    if img == null:
        printerr("[sps-plugin] failed to load image: ", path)
        return
    var opts := {
        "mode": "auto",
        "min_width": 2,
        "min_height": 2,
    }
    var files := _splitter.split_and_export(img, opts, "res://sprites")
    print("[sps-plugin] exported ", files.size(), " sprites to res://sprites/")
    for f in files:
        print("[sps-plugin]   ", f)
    if files.is_empty():
        printerr("[sps-plugin] no sprites found (check threshold/options)")
