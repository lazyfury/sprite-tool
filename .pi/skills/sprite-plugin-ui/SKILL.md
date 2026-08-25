---
name: sprite-plugin-ui
description: Sprite Splitter 插件 UI 开发规范——Godot 插件图形界面（选图/分析/切分/预览/导出）的设计与实现，含独立场景测试工作流（不挂载编辑器，headless 可回归）。开发/修改 godot/project/addons/sprite_splitter/ui/ 下的场景、或在 Godot 中为工具类插件做 UI 时使用。
---

# Sprite Splitter 插件 UI 开发规范

> 配套：`docs/plugin-ui-plan.md`（设计规划，含节点树/交互状态机）、`sprite-splitter` skill（CLI 语义）、`godot-gdextension` skill（C++ 侧）。
> 里程碑：M5.1 独立场景 ✅；M5.2 挂载编辑器 ✅；M5.3 主屏幕+侧栏双挂载 + controller 重构 ✅；sheet 打包可选。

## 0. 现状与定位

- UI 拆三件（M5.3 起）：`ui/sps_controller.gd`（RefCounted 业务控制器）+ `ui/sprite_splitter_main.tscn`（主屏幕画布）+ `ui/sprite_splitter_side.tscn`（dock 操作面板）+ `ui/canvas_view.gd`（画布）；测试入口 `ui/test_harness.tscn`。
- **M5.1 独立场景**：不挂载编辑器（当时 4.6.2 编辑器模式有 `EditorHelp::_gen_extensions_docs` 崩溃 bug）→ 独立场景验证 + headless 回归。
- **M5.2 挂载编辑器**：EditorPlugin + Tools 菜单 + 框选/导入 meta/导出。
- **M5.3 双挂载 + Controller**：主屏幕全屏画布（2D｜3D｜Sprite Splitter 标签）+ 侧栏 dock（DOCK_SLOT_RIGHT_BL）操作面板；两视图共享 SpsController 信号桥接跨区域交互（dock 按钮 → 画布响应）。**回答"能否交互"：能——同一 EditorPlugin 可同时挂 main_screen + dock，共享 controller 即交互。**
- 能力上限 = GDExtension API：`split / analyze / remove_background / crop / export_sprite / split_and_export / export_metadata`（无 sheet 打包，M5.3 需 C++ 补 API）。
- **去背景 = 独立按钮**：C++ 补 `SpriteSplitter.remove_background(image, {background_threshold}) -> Image`（core `BackgroundRemover::process_transparent` 整图透明），UI 点击替换图 + 画布刷新 + `image_res_path=""`（内存图 AtlasTexture 不可用）；参数面板「背景阈值」SpinBox（默认 12）。

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

## 2. UI 结构（M5.3 起：主屏幕 + 侧栏 dock 双挂载，共享 controller）

```
editor_plugin.gd（EditorPlugin，双挂载）
├── SpsController（ui/sps_controller.gd，RefCounted，全部业务逻辑+信号）
├── 主屏幕 SpriteSplitterMain（ui/sprite_splitter_main.tscn）  ← get_editor_main_screen()
│   └── MainVBox（Header 打开素材+地址 / PreviewArea（CanvasView + ToolBarMargin
│       （ToolBarRow = ToolBar 工具 + ZoomGroup 缩放）））
└── dock SpriteSplitterSide（ui/sprite_splitter_side.tscn）    ← DOCK_SLOT_RIGHT_BL
    └── VBox：TabContainer（切分/分析/去背景/导出，各页 ScrollContainer）/ 状态栏
```

- **双挂载 + 跨区域交互**：一个 EditorPlugin 可同时 `EditorInterface.get_editor_main_screen().add_child(main)` +
  `add_control_to_dock(DOCK_SLOT_RIGHT_BL, side)`；两视图 `set_controller(controller)` 绑定同一 SpsController，
  **信号桥接交互**（dock 按钮 → controller 方法 → 画布刷新；画布 selection/view 信号 → controller → 侧栏状态/计数）。
- **Controller 模式**：业务逻辑与状态全在 `sps_controller.gd`（RefCounted，`_init(tree: SceneTree)` 注入主循环供协程
  `await`）；信号：`status_changed / image_loaded / rects_changed / count_changed / analyze_done / exporting_changed`；
  视图只做 UI 绑定与转发。
- **画布式预览（CanvasView）**：@tool Control 自绘画布，图片/红框/框选/工具**全世界坐标（像素）绘制**，
  相机式 `_zoom/_center`（`draw_set_transform`）UI 层缩放平移（逻辑分辨率恒定 = 2D 节点+Camera2D 效果）；
  工具 MOVE（左键平移）/SELECT（点击单选+框选多选，黄色高亮）/CROP（青色裁切框）；滚轮以鼠标为锚缩放、
  中键平移、双击 fit；`clip_contents = true` 放大不溢出；线宽 `2.0/zoom` 恒定像素；
  ⚠️ **自定义方法别叫 `get_canvas()`**（Control 已有，返回 RID），用 `get_canvas_view()`。
- 参数 → options 字典键（与 GDExtension 对齐）：`mode / min_width / min_height / grid_cell_size / merge_distance / alpha_threshold / remove_background`。

## 3. 独立场景测试工作流（不挂编辑器）

```bash
# GUI 交互验证：编辑器打开 tscn 按 F6（或命令行不带 SPS_UI_TEST）
# headless 自动回归（全链路：加载→切分→导出→画布→工具→去背景→断言→quit）：
cd godot/project
SPS_UI_TEST=1 "/Applications/Godot.app/Contents/MacOS/Godot" --headless --path . \
  --quit-after 160 res://addons/sprite_splitter/ui/test_harness.tscn
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

## 5. 挂载编辑器（M5.2 已完成）

- **主屏幕插件**（参考 limboai `LimboAIEditorPlugin`）：UI 挂到编辑器主屏幕，顶部标签栏（2D｜3D｜Sprite Splitter）点标签进入全屏界面，而非侧边 dock：
  ```gdscript
  # _enter_tree
  _ui = UI_SCENE.instantiate()
  _ui.set_v_size_flags(Control.SIZE_EXPAND_FILL)
  EditorInterface.get_editor_main_screen().add_child(_ui)
  _ui.hide()                      # 初始隐藏
  # 虚方法（⚠️ 必须实现 _has_main_screen 返回 true，否则标签不出现在 2D/3D 旁边的工作区选择器）
  func _has_main_screen() -> bool: return true
  func _get_plugin_name() -> String: return "Sprite Splitter"
  func _get_plugin_icon() -> Texture2D:   # svg 未导入时兜底 theme 图标
      var icon: Texture2D = load("res://addons/sprite_splitter/icon.svg")
      if icon == null: return EditorInterface.get_editor_theme().get_icon("Node", "EditorIcons")
      return icon
  func _make_visible(p_visible: bool) -> void: _ui.visible = p_visible
  # _exit_tree
  EditorInterface.get_editor_main_screen().remove_child(_ui)
  _ui.queue_free()
  ```
- **主屏幕标签只在编辑器启动时注册**：改完 editor_plugin.gd 后必须**完全退出编辑器（Cmd+Q）重启**才生效，不会热添加（排查"看不到标签"第一步先查是否有旧编辑器进程在跑，`pgrep -fl Godot`）。
- 标签名 `_get_plugin_name()`、图标 `_get_plugin_icon()`（svg 放 addons 下，编辑器自动导入）、显隐 `_make_visible()`。
- 对话框适配：`Engine.is_editor_hint()` → 创建 `EditorFileDialog`，否则 `FileDialog`（EditorFileDialog 继承 FileDialog，`access/file_mode/filters/file_selected` 均继承可用）；两个 dialog 变量标注为 `Variant`（跨类型）。
- 编辑器模式崩溃规避：两步法导入（临时移走 .gdextension → --import → 恢复 + 手动写 extension_list.cfg）。
- GUI 实测命令：`Godot -e --path project --quit-after 500` → 启动阶段 grep `sps-plugin` 确认 `plugin active (C++ core loaded: true)`；退出时看有无 SCRIPT ERROR；`.godot/imported/icon.svg-*.ctex` 存在 = 图标导入成功。

## 6. 陷阱

- **编辑器模式下所有脚本必须 `@tool`，否则静默失效**（2026-08-25 实测踩坑）：主屏幕 UI 场景的 `sprite_splitter_ui.gd`/`canvas_view.gd` 曾缺 `@tool` → 编辑器里场景能显示但 `_ready` 不执行、信号不连接、按钮点击无任何反应且**无任何报错**（headless 运行模式正常，编辑器模式才暴露）。现象排查："点按钮没反应" + 编辑器输出无 SCRIPT ERROR → 先检查脚本是否 `@tool`。
- GDScript 协程作用域：`await` 把函数编译成状态机，`if/for` 块内声明的局部变量块外不可见 → 引用块内变量的语句必须保持块内缩进（否则 `Identifier not declared`）。
- **类型化信号 `emit([])` 空字面量静默失败**（2026-08-25 实测踩坑）：信号声明 `signal rects_changed(rects: Array[Rect2i])`，槽函数签名也是类型化数组时，`emit([])` 传**未类型化空数组** → 运行时 `Cannot convert argument 1 from Array to Array`，**槽不被调用且只在 emit 时打一次 ERROR**（现象：列表/画布没清空、无脚本错误难排查）。修复：`emit([] as Array[Rect2i])`；同理任何类型化信号传空数组都必须 `as` 转换。
- **编辑器 FileSystem dock 拖放不沿祖先链冒泡**（2026-08-25 实测踩坑）：`_can_drop_data`/`_drop_data` 只询问**鼠标下最顶层的 Control**，父级 Control 不会收到——拖放接收必须实现在预览区实际铺满的控件（CanvasView）上，用信号转发给业务层（`drop_requested(path)`），而非主视图 root。data 格式：`{"type": "files", "files": PackedStringArray}`；扩展名判断 `f.get_extension().to_lower()`；系统文件拖放（`Window.files_dropped`）是绝对路径，编辑器插件场景别用。
- **CanvasItem.draw_multiline_string 参数顺序**（4.6，2026-08-25 踩坑）：第 7 参数是 `max_lines`(int)，**第 8 才是 `modulate`(Color)**——直接把 Color 放第 7 位报 `Cannot pass Color as int`。传颜色必须占位 `-1` 给 max_lines。
- **画布 fit 时序**：`set_texture` 后控件尺寸可能为 0（布局未完成），立即 `fit()` 会失效（`size.x<=0` 直接 return）→ `_pending_fit` 标记 + `NOTIFICATION_RESIZED` 时执行。
- `_ready` 里同步跑全量测试会阻塞首帧渲染 → 场景显示慢；改为协程分帧。
- **画布工具模式（MOVE/SELECT/CROP，PS 工具栏风格）**：ToolBar = 模式按钮组（ButtonGroup 单选）+ 缩放控件；CanvasView `enum Tool {MOVE, SELECT, CROP}`：
  - MOVE：左键拖拽平移（中键任何模式均可平移）；SELECT：点击单选（`pick_rect_at` 命中测试，位移 < CLICK_THRESHOLD 视为点击）+ 画矩形框选多个（`intersects`）+ 右键清空，选中高亮黄色；CROP：画矩形裁切（青色，`selection_drawn` 供导出选中）
  - 点击 vs 拖拽：按下-松开位移 < 5px 视为点击；拖拽矩形屏幕→世界：线性变换 `Rect2(s2w(tl), s2w(br)-s2w(tl))`
  - 测试可直接注入内部变量（`canvas.set("_drag_start", ...)` + 调 `_on_drag_select()`/`_finish_crop()`）做 headless 断言
- **项目数据 Resource（.tres，uid 关联）**：`ui/sps_data.gd`（`class_name SpriteSplitterData extends Resource`）——@export 存常用参数/导出位置/项目名/`sprites: Array[Rect2i]`（**兼容 meta.json rects**）；`SpsController` 维护 `data/data_path`：`load_image` 时 `ResourceLoader.get_resource_uid(path)`（⚠️ **返回 int**，转文本用 `ResourceUID.id_to_text(id)`，判空用 `ResourceUID.INVALID_ID`）→ 文件名 = `uid://...` 去前缀 → `res://sps_data/<tag>.tres`；存在则 `load` 恢复（发 `data_loaded` 信号 → 侧栏填参数/项目名/导出），否则 `SpriteSplitterData.new()` 初始化（项目名自动=素材名）并落盘；`save_project(name, options, out_dir, export_mode)` 写盘 + `_refresh_filesystem()`；切分/导入 meta/去背景时同步 `data.sprites`（内存）。侧栏顶部 ProjectRow：项目名 LineEdit + 保存按钮。
- **项目注册表（SpriteSplitterRegistry）**：`ui/sps_registry.gd`（`class_name SpriteSplitterRegistry extends Resource`，**必须 `@tool`**——非 @tool 的 Resource load 后是 placeholder，调方法报 `Attempt to call a method on a placeholder instance`）；`entries: Array[String]` 存已注册 data 路径；默认注册表 `res://sps_data/registry.tres`（controller `_ensure_registry`：不存在则 new+save，再 `_scan_registry` 扫目录补登记）；`_register_data(path)` 在新建（`_load_or_create_data` 落盘）与 `save_project` 成功时自动调用（去重 + 持久化 + `registry_updated` 信号）；UI 主视图右侧下半「项目注册表」卡片：ItemList 显示 `项目名 (文件名)`，`set_item_metadata` 存路径，`item_selected` → `controller.load_registry_entry(path)`（校验 + 统一走 `apply_data`）。
- **主屏幕窗口主题化（header/footer/side 分层）**：独立 workspace（非 dock）需给 TopBar/BottomBar/SidePanel 可视化背景 + padding + 分隔线，**跟随编辑器主题**：`_apply_theme()` 里 `EditorInterface.get_editor_theme()` 取 `dark_color_1`（面板 bg）/`separator`（分隔线）色，构造 StyleBoxFlat（`bg_color` + `content_margin_*` padding + `set_border_width(SIDE_*, 1)` 分隔线），`add_theme_stylebox_override("panel", sb)`；非编辑器模式回退内置深灰；`EditorInterface.get_base_control().theme_changed` 连接刷新；**⚠️ 结构改动后 @onready 路径同步更新**（TopBar 包进 PanelContainer 后 get_node("Main/TopBar/...") 变 "Main/TopBarPanel/TopBar/...")；⚠️ 4.6 里 `MARGIN_*` 不是全局常量，用 `SIDE_LEFT/TOP/RIGHT/BOTTOM`。
- **导出文件后资产库不刷新**：编辑器内用 FileAccess/GDExtension 写盘的文件不会自动出现在 FileSystem dock，需主动触发重扫：`EditorInterface.get_resource_filesystem().scan_sources()`（增量，批量导出用；参考 limboai 同款调用）；仅编辑器模式调用（`if not Engine.is_editor_hint(): return`），运行模式直接跳过；`scan_sources()` 异步生效，文件会稍后出现。
- **OptionButton 选项用代码 `add_item()` 重建，不要用 tscn 序列化**（2026-08-25 实测）：Godot 4.6 下 tscn 的 `item_N/text` **只恢复 item_count、文本全空**（`get_item_text()` 返回 ''，`selected=-1`），下拉框显示空白且无报错；`_ready` 里 `clear()` + 遍历常量数组 `add_item()` + `select(0)`，编辑器/运行模式都可靠。诊断脚本：实例化场景后打印 `get_item_text(i)`。
- **画布框选/红框 = 世界坐标（图片像素）**：CanvasView `_gui_input` 里左键框选、中键平移、右键清除、滚轮以鼠标为锚缩放、双击 fit；**触控板手势（macOS，仅捏合缩放）**：`InputEventMagnifyGesture`（双指捏合 → `zoom_at(pos, factor)`）；**双指拖动 PanGesture 已按用户要求移除（2026-08-25）**；图片与红框同一变换绘制 → 天然像素对齐，无视图逆映射（旧 overlay+TextureRect 方案红框错位源于手动 scale/offset 与 KEEP_ASPECT_CENTERED 不一致）。
- **EditorPlugin 移除方法名是 `remove_control_from_docks`（复数 s）**：`remove_control_from_dock` 不存在，check-only/编辑器加载报 `Function not found in base self`（headless classdb 实测 + 官方文档确认）。
- **RefCounted 禁止手动 `free()`**：`SpriteSplitter.new()` 返回 RefCounted，`_exit_tree` 里 `_splitter.free()` 会报 `Can't free a RefCounted object`；置 `null` 由引用计数释放。
- **meta.json 导入**：格式与 CLI 同源 `{image,width,height,sprites:[{x,y,width,height}]}`，解析时跳过 `w<=0||h<=0` 条目；`FileAccess.get_file_as_string` + `JSON.parse_string`。
