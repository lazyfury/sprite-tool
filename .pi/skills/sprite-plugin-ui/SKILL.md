---
name: sprite-plugin-ui
description: Sprite Splitter 插件 UI 开发规范——Godot 插件图形界面（选图/分析/切分/预览/导出）的设计与实现，含独立场景测试工作流（不挂载编辑器，headless 可回归）。开发/修改 godot/project/addons/sprite_splitter/ui/ 下的场景、或在 Godot 中为工具类插件做 UI 时使用。
---

# Sprite Splitter 插件 UI 开发规范

> 配套：`docs/plugin-ui-plan.md`（设计规划，含节点树/交互状态机）、`sprite-splitter` skill（CLI 语义）、`godot-gdextension` skill（C++ 侧）。
> 里程碑：M5.1 独立场景已落地；M5.2 挂载编辑器 dock 待做。

## 0. 现状与定位

- UI 场景：`godot/project/addons/sprite_splitter/ui/sprite_splitter_ui.tscn` + `sprite_splitter_ui.gd` + `overlay.gd`
- **不挂载编辑器**：4.6.2 编辑器模式有 `EditorHelp::_gen_extensions_docs` 崩溃 bug，EditorPlugin GUI 无法 headless 验证 → M5.1 用独立可运行场景验证全部交互，验证通过后再挂 dock。
- 能力上限 = GDExtension API：`split / analyze / crop / export_sprite / split_and_export / export_metadata`（无 sheet 打包）。

## 1. 编码约定（项目强制，违反即返工）

1. **节点路径一律用 `/` 表示子层级**：`get_node("Main/Content/SidePanel/Side/SplitBtn")`；不链式 get_node、不用 `%` 唯一名。
2. **var 一律显式类型标注，不用 `:=`**（`const` 因语言限制除外）：
   ```gdscript
   var opts: Dictionary = {"mode": "auto", "min_width": 2}
   var rects: Array[Rect2i] = []
   var img: Image = Image.load_from_file(path)
   ```
3. 控件引用：`@onready var _x: Button = get_node("...")`；信号在 `_ready` 集中 `connect`。
4. 重操作（切分/导出/大图加载）放协程 + `await get_tree().process_frame` 让帧，UI 不冻结。
5. 函数参数与返回值一律标注类型；`match` 分支用常量（`EXPORT_PNG: int = 0`）。

## 2. UI 结构（tscn 节点树，路径即 / 层级）

```
SpriteSplitterUI (Control)                      [sprite_splitter_ui.gd]
└── Main (VBoxContainer)
    ├── TopBar (HBoxContainer)                  FileButton / FileLabel
    ├── Content (HSplitContainer)
    │   ├── PreviewPanel/Preview (Control)
    │   │   ├── Texture (TextureRect)           stretch_mode=5 (KEEP_ASPECT_CENTERED)
    │   │   └── Overlay (Control)               [overlay.gd] _draw 描边
    │   └── SidePanel/Side (VBoxContainer)      ModeOption / ParamGrid / BgRemove /
    │                                           AnalyzeBtn / InfoLabel / SplitBtn / CountLabel
    └── BottomBar (HBoxContainer)               ExportModeOption / OutDir / ExportBtn / StatusLabel
```

- 预览坐标映射：`scale = min(w/img_w, h/img_h)`、`offset = (tex_size - img*scale)/2`，注入 Overlay 后 `_draw` 逐矩形描边（与 TextureRect 的 KEEP_ASPECT_CENTERED 对齐）。
- 参数 → options 字典键（与 GDExtension 对齐）：`mode / min_width / min_height / grid_cell_size / merge_distance / alpha_threshold / remove_background`。

## 3. 独立场景测试工作流（不挂编辑器）

```bash
# GUI 交互验证：编辑器打开 tscn 按 F6（或命令行不带 SPS_UI_TEST）
# headless 自动回归（全链路：selftest→分析→切分→PNG/meta/tres 导出→断言→quit）：
cd godot/project
SPS_UI_TEST=1 "/Applications/Godot.app/Contents/MacOS/Godot" --headless --path . \
  --quit-after 120 res://addons/sprite_splitter/ui/sprite_splitter_ui.tscn
# 期望：[sps-ui] PASS 全绿 + "auto test done (fail=false)" + 退出码 0
```

- `_selftest()`：每次加载都跑（内置 sheet.png → auto split → 断言 64），GUI 下也快速验证 C++ 核心在位。
- `_auto_test()`：仅当 `SPS_UI_TEST=1` 或用户参数 `--sps-ui-test` 时跑完整导出链路 + 断言 + `get_tree().quit()`。
- 产物检查：`out_sprites/ui_test/` 应有 N 个 PNG + N 个 .tres + meta.json（N=精灵数）。

## 4. 导出能力

| 模式 | 输出 | 实现 |
|---|---|---|
| 切 PNG | `<out>/*.png` + meta.json | `split_and_export(image, opts, dir)` |
| 仅 meta.json | `<out>/meta.json` | `export_metadata(image, rects, name, path)` |
| AtlasTexture .tres | `<out>/atlas_*.tres` | 遍历 rects 建 AtlasTexture，`ResourceSaver.save` |

**AtlasTexture 关键点**：atlas 必须用**导入管线纹理**（`load(res://...)`），动态 `ImageTexture` 无法内联进 .tres 文本资源；素材在项目外时该项导出不可用（`ProjectSettings.localize_path` 判断，开头非 `res://` 则禁用）。

## 5. 挂载编辑器（M5.2 路线）

- 复用同一套逻辑脚本，EditorPlugin 只做容器：`add_control_to_dock(EditorPlugin.DOCK_SLOT_RIGHT_UL, ui)` 或 `add_control_to_bottom_panel`。
- 加载顺序：`_enter_tree` 里实例化场景（`load("res://addons/sprite_splitter/ui/sprite_splitter_ui.tscn").instantiate()`）。
- 编辑器模式崩溃规避：两步法导入（临时移走 .gdextension → --import → 恢复 + 手动写 extension_list.cfg）。

## 6. 陷阱

- GDScript 协程作用域：`await` 把函数编译成状态机，`if/for` 块内声明的局部变量块外不可见 → 引用块内变量的语句必须保持块内缩进（否则 `Identifier not declared`）。
- `_ready` 里同步跑全量测试会阻塞首帧渲染 → 场景显示慢；改为协程分帧。
- OptionButton 在 tscn 里用 `item_N/id` + `item_N/text` 序列化。
- TextureRect.size 在布局完成前为 0 → 预览映射前 `await get_tree().process_frame` 兜底。
