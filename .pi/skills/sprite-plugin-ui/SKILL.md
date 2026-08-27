---
name: sprite-plugin-ui
description: Sprite Splitter 插件 UI 开发规范——Godot 插件图形界面（选图/分析/切分/预览/导出）的设计与实现，含独立场景测试工作流（不挂载编辑器，headless 可回归）。开发/修改 godot/project/addons/sprite_splitter/ui/ 下的场景、或在 Godot 中为工具类插件做 UI 时使用。
---

# Sprite Splitter 插件 UI 开发规范

> 配套：`docs/plugin-ui-plan.md`（设计规划，含节点树/交互状态机）、`sprite-splitter` skill（CLI 语义）、`godot-gdextension` skill（C++ 侧）。
> 里程碑：M5.1 独立场景 ✅；M5.2 挂载编辑器 ✅；M5.3 主屏幕+侧栏双挂载 + controller 重构 ✅；M5.4 统一输出根目录 + 生成资源注册表 + 清理窗口 ✅；sheet 打包可选。

## 0. 现状与定位

- UI 拆四件（M5.3 起）：`ui/sps_controller.gd`（RefCounted 业务控制器）+ `ui/sprite_splitter_main.tscn`（主屏幕画布）+ `ui/sprite_splitter_side.tscn`（dock 操作面板）+ `ui/sprite_splitter_edit_dock.tscn`（切片编辑 dock，2026-08-27）+ `ui/canvas_view.gd`（画布）；测试入口 `ui/test_harness.tscn`。
- **M5.1 独立场景**：不挂载编辑器（当时 4.6.2 编辑器模式有 `EditorHelp::_gen_extensions_docs` 崩溃 bug）→ 独立场景验证 + headless 回归。
- **M5.2 挂载编辑器**：EditorPlugin + Tools 菜单 + 框选/导入 meta/导出。
- **M5.3 双挂载 + Controller**：主屏幕全屏画布（2D｜3D｜Sprite Splitter 标签）+ 侧栏 dock（DOCK_SLOT_RIGHT_BL）操作面板；两视图共享 SpsController 信号桥接跨区域交互（dock 按钮 → 画布响应）。**回答"能否交互"：能——同一 EditorPlugin 可同时挂 main_screen + dock，共享 controller 即交互。**
- 能力上限 = GDExtension API：`split / analyze / remove_background / crop / export_sprite / split_and_export / export_metadata`（无 sheet 打包，M5.3 需 C++ 补 API）。
- **去背景 = 独立按钮**：C++ 补 `SpriteSplitter.remove_background(image, {background_threshold}) -> Image`（core `BackgroundRemover::process_transparent` 整图透明）；UI 点击后**写盘为新 PNG**（`<输出根目录>/<原名stem>_transparent.png`，与 CLI remove-background 同名；输出根目录 = ProjectSettings `sprite_splitter/out_root`，默认 `res://out_sprites`）并更新项目数据源（2026-08-25 起）：`source_image` 立即更新，编辑器模式触发扫描导入 → 等新 PNG `.uid` 文件/uid 缓存就绪 → 更新 `sheet_uid` + `source_texture` + **data_path 迁移**（`<旧uid>.tres` → `<新uid>.tres`，registry 换条目、删旧文件、`data_path_changed` 刷新选中）；运行模式（headless）无导入流程仅更新路径。参数面板「背景阈值」SpinBox（默认 12）。

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

## 2. UI 结构（M5.3 起：主屏幕 + 双 dock 三挂载，共享 controller）

```
editor_plugin.gd（EditorPlugin，三挂载）
├── SpsController（ui/sps_controller.gd，RefCounted，全部业务逻辑+信号）
├── 主屏幕 SpriteSplitterMain（ui/sprite_splitter_main.tscn）  ← get_editor_main_screen()
│   └── MainVBox（Header 打开素材+地址 / PreviewArea（CanvasView + ToolBarMargin
│       （ToolBarRow = ToolBar 工具 + ZoomGroup 缩放）））
├── dock SpriteSplitterSide（ui/sprite_splitter_side.tscn）    ← DOCK_SLOT_RIGHT_BL
│   └── VBox：TabContainer（切分/分析/去背景/导出，各页 ScrollContainer）/ 状态栏
└── dock SpriteSplitterEditDock（ui/sprite_splitter_edit_dock.tscn） ← DOCK_SLOT_RIGHT_BR
    └── 切片编辑面板（单选切片编辑名称/几何/锁定/忽略，多选占位提示）
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

## 3.5 UI 插桩（位置 + 主题影响数据分析）

- **脚本**：`ui/ui_probe.gd`（RefCounted 工具类，纯只读，不挂节点）。`UiProbeScript.write_report(root, "res://out_probe/ui_probe.json")` 遍历 root 下全部 Control，记录：
  - 位置：pos/size/gpos/grect（Vector2/Rect2 已转数组，纯 JSON）/锚点/offset/size_flags/可见性
  - 影响数据：实际生效主题值 `theme_eff`（font 资源名+路径/font_size/line_spacing/font_color/panel StyleBoxFlat 的 bg/圆角/边距）+ `overrides`（被代码或 tscn 覆盖的 key，分 constant/color/font/font_size/stylebox 五类）
  - Label 特化：text/line_count/对齐/autowrap/overrun/clip；ItemList/Button 特化
- **触发**：`SPS_UI_PROBE=1` 或 `--sps-ui-probe`（test_harness `_auto_probe`：先 load_image+split 造数据 → 等 8 帧 → 写 JSON → quit）。输出 `res://out_probe/ui_probe.json`。
- **⚠️ headless 默认窗口 64x64**：直接跑插桩布局全塌（HSplitContainer/预览区/列表 0 高，位置数据全废）→ `_auto_probe` 必须先 `get_window().size = Vector2i(1280, 800)` 再等布局帧。
- **⚠️ headless 无真实字体**：`get_theme_font("font").resource_name` 为空（内置默认字体无资源名）→ 插桩查不了具体字体文件，只能查字号/行距/颜色等数值。布局/字体观感差异类问题仍需 GUI（F6）验证。
- **⚠️ Godot 4.6 无 `get_theme_*_override_list()` API**（`has_method` 判断也不存在）→ override 收集用 `has_theme_constant/color/font/font_size/stylebox_override(key)` 逐个查。
- **测试 harness holder 锚点坑**（2026-08-25 修复）：test_harness.tscn 的 SideHolder 曾写 `offset_left = 0.7`（**0.7 像素**）意图是 70% 分栏，应为 `anchor_left = 0.7`——否则 SideHolder 盖住 MainHolder 全宽重叠，兄弟重叠插桩一眼可见。

## 4. 导出能力

| 模式 | 输出 | 实现 |
|---|---|---|
| 切 PNG | `<out>/*.png` + meta.json | `split_and_export(image, opts, dir)` |
| 仅 meta.json | `<out>/meta.json` | `export_metadata(image, rects, name, path)` |
| AtlasTexture .tres | `<out>/atlas_*.tres` | 遍历 rects 建 AtlasTexture，`ResourceSaver.save` |
| **导出选中（2026-08-27 修正语义）** | PNG：`<原名>_NN_x_y.png`；TRES：`atlas_sel_NN.tres` | `export_selected(out_dir, mode)`——**数据 selected 驱动**（画布 SELECT 框选/列表多选，单选多选均支持），忽略 `ignored` 项；**仅 PNG / AtlasTexture 两种模式，meta.json 不支持**（side 按导出模式禁用「导出选中」按钮 `_refresh_export_sel_btn` + controller `_` 分支兜底提示；用户明确不要 meta 导出选中）。⚠️ **原实现错用 CROP 裁切框 `selection`**（提示"先用裁切工具框选"，与用户选中心智不符→"导出选中没用"）；⚠️ **export_selected 尾部不要加多余 `await _wait_frame()`**——会让 `exporting` 复位慢一帧，连续调用被 `if exporting: return` 挡掉；子函数内部已各自让帧，match 后立即复位

**AtlasTexture 关键点**：atlas 必须用**导入管线纹理**（`load(res://...)`），动态 `ImageTexture` 无法内联进 .tres 文本资源；素材在项目外时该项导出不可用（`ProjectSettings.localize_path` 判断，开头非 `res://` 则禁用）。

## 4.1 输出根目录 + 生成资源注册表 + 清理窗口（M5.4，2026-08-27）

- **输出根目录（统一 out 目录，父目录项目设置可配）**：ProjectSettings `sprite_splitter/out_root`（默认 `res://out_sprites`，`_ensure_out_root_setting` 在 controller `_init` 注册 + `add_property_info` 使项目设置面板可见）；`get_out_root()/set_out_root(v, persist)`——**persist=true 且编辑器模式才 `ProjectSettings.save()` 写 project.godot**（headless 仅内存，测试注入不污染）；`default_out_dir(tag)` = `<root>/<tag>`（项目数据 out_dir 新建默认值，旧 `res://out_sprites/ui/<uid>` 作废）；**所有生成资源统一落根目录下**：新建 data 默认 out_dir、去背景 PNG（`_write_transparent_png`）、导出/去背景/sheet 空目录 fallback、Tools 快捷入口（原 `res://sprites`）；侧栏导出 tab「输出根目录」行（LineEdit + 默认按钮：`text_changed` 仅内存生效、`text_submitted`/`focus_exited`/默认按钮持久化，**全局设置不 mark_dirty**，`set_controller` 时 `_refresh_out_root()` 填充且不被 `data_loaded` 覆盖）。
- **生成资源注册表（SpriteOutputRegistry）**：`ui/sps_output_registry.gd`（@tool Resource，`<data_dir>/output_registry.tres`，测试注入目录自动隔离）；**与 SpriteSplitterRegistry 职责分离**（后者登记项目配置 .tres，前者登记生成产物）；`entries: Array[Dictionary]` = `{path, kind, project, created_at, size}`，kind：png/meta/tres/sheet；`register` 去重 + 更新时间/大小、`remove`、`purge_missing` 自愈；controller `register_output_file(path, kind)` 统一出口（`_export_png/_export_meta/_export_tres/_export_selected_*/_write_transparent_png/export_sheet(_from_files)/Tools 快捷入口` 写盘成功后自动登记 + 持久化）。
- **清理窗口（cleanup_window.gd/.tscn）**：main Header「清理资源」按钮打开独立顶级窗口（复用单例，仿 sheet_builder_window：编辑器根 `add_child`、`popup_centered`、关闭仅 hide、每次打开 `refresh()`）；ItemList 多选列出注册资源（`🔴 占用 / 🟢 可清理` + 类型/大小/时间，tooltip 完整路径，`set_item_metadata` 存 path）；「清理选中」「清理全部未占用」+ ConfirmationDialog 确认（`ok_button_text`），`cleanup_outputs(paths, force=false)`：**占用项拒绝**（返回 `{deleted, refused, missing}`），force=true 强制删；删除后注册表移除 + 资产库重扫。
- **占用判定（check_outputs_usage / is_output_occupied → _scan_project_refs）**：① 插件自身——当前 `image_res_path`、项目数据 `source_image`；② 项目文本资源（`.tscn/.tres/.gd/.json/.cfg`，递归 res:// 排除 `.godot` 目录与 project.godot）内容出现该资源路径或 uid（`<path>.uid` 文件读 uid / `ResourceLoader.get_resource_uid`），**生成资源自身（含 output_registry.tres——内容含全部登记路径）互相引用不算占用**（self_paths 排除集）。⚠️ **测试脚本（.gd）里的完整路径字面量会被误判为"项目引用"**——断言/代码里路径拆串拼接（`var p = dir + "/" + "sprite_01.png"`）规避。

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
  - **选中高亮工具无关（2026-08-27 起）**：`_draw` 里选中高亮由数据 `_sprites[i].selected` 驱动，SELECT/EDIT/MOVE 三模式均渲染（`_tool` 限制只排除 CROP）——MOVE 平移视图时选中数据仍在，保持体验一致；测试无法直接断言 draw 内容，改 `_draw` 条件不影响回归断言
  - 点击 vs 拖拽：按下-松开位移 < 5px 视为点击；拖拽矩形屏幕→世界：线性变换 `Rect2(s2w(tl), s2w(br)-s2w(tl))`
  - 测试可直接注入内部变量（`canvas.set("_drag_start", ...)` + 调 `_on_drag_select()`/`_finish_crop()`）做 headless 断言
  - **切片编辑 dock（2026-08-27，独立面板非弹窗）**：`ui/sprite_splitter_edit_dock.tscn/.gd` 独立 dock 面板，`editor_plugin.gd` `add_control_to_dock(DOCK_SLOT_RIGHT_BR, _edit_ui)` 与 side（RIGHT_BL）并列停靠，可拖拽换位。表单：uid 只读 + 名称 LineEdit + X/Y/宽/高 SpinBox + 锁定/忽略 CheckBox + 保存按钮；**几何分组卡片与 side 一致（2026-08-27）**：位置/尺寸两个折叠卡片（PanelContainer + ▾ header 26px flat toggle + body，`_cards` 数组统一 `_make_card` 主题化，`_setup_fold_header` 同 side）。联动：列表双击（`item_activated`）/右键菜单「编辑…」（SPRITE_MENU_EDIT）→ `SpsController.request_edit_sprite(index)`（**多选守卫**：selected>1 拒绝提示单选；否则收敛单选写 selected + `sprites_changed.emit`）→ `edit_sprite_requested.emit` → dock 填充表单；dock 另监听 `sprites_changed` 实时同步（画布/列表单选 → 表单填充；多选/无选 → EditHint 占位）。保存走 `SpsController.update_sprite_fields(index, fields)`（一次批量提交名称/几何/锁定/忽略，几何 clamp 图内，锁定项几何保持原值、空名保持原名，一次 `_sync_rects`+`mark_dirty`）；锁定勾选时几何 SpinBox `editable=false`。⚠️ **tscn 层级变更必须同步 @onready 路径**（漏改对 null 赋值报 `Invalid assignment ... null instance`）。⚠️ 新 tscn 的 ext_resource 可先 path-only（不带 uid），编辑器导入自动生成 .uid。旧弹窗方案（AcceptDialog/ConfirmationDialog）已移除——AcceptDialog 没有 `cancel_button_text`（ConfirmationDialog 才有）。
- **自动保存（2026-08-27，防抖限流 + 保存锁 + 倒计时提示）**：`mark_dirty()` 触发防抖调度（`autosave_delay=3.0s` 默认，停止修改后保存一次）——`_schedule_autosave` 记录 deadline + 单例协程 `_run_autosave_loop` 等到期 `_save_now`；连续修改只合并为一次保存（限流）。**倒计时**：loop 每秒 emit `autosave_countdown(seconds_left)`（ceil 剩余秒 3→2→1，0=完成/取消）→ side 状态栏显示「⏳ N 秒后自动保存」（0 时只清 ⏳ 文本，不覆盖 ✅/⚠）。**保存锁防竞争**：`_saving` 防重入 + `_save_queued`（保存中又脏 → 完成后补存一次）。**参数统一来源 `_save_pending`**：side 参数变化（`_on_dirty_signal` → `sync_save_params`）把 project_name/options/out_dir/export_mode 最新值同步进 controller，手动 `save_project` 与自动保存共用（未同步过 fallback data 现值）——避免自动保存用旧参数。**挂起**：`remove_background`（迁移 data_path）期间 `suspend_autosave`/`resume_autosave`（失败路径也要 resume；**suspend 首层清 deadline + emit(0) 取消进行中倒计时**），避免写竞争旧路径。保存成功 `_flush_save` 清 deadline + `_mark_clean` + `data_saved`；`autosave_state_changed(saving)` 信号 → side 禁用保存按钮。⚠️ harness 默认 `autosave_enabled=false`（现有断言依赖 is_dirty 状态），自动保存测试段单独开启 + `autosave_delay=0.1` 加速；**GDScript lambda 捕获局部变量做计数器不可靠**（闭包拷贝），计数用成员变量 + 命名槽函数。
- **切片分组（2026-08-27）**：SpriteItem 增 `group` 字段（空 = 未分组，`_make_sprite`/`_sprites_from_any` 兜底，`update_sprite_fields` 支持写入，允许清空）；controller `get_groups()`（去重按出现顺序）+ `select_group(group)`（选中该组全部切片替换当前选择，UI 状态不 mark_dirty，sprites_changed 同步）+ `set_group_for_selected(group)`（**多选批量设分组**：全部 selected 项 group 设为同一值，空=清除，`_sync_rects`+mark_dirty）+ `select_all()` + `remove_selected_sprites() -> int`（**批量删除选中**，过滤保留未选中，显式循环转 Array[Dictionary]——`filter` 返回未类型化 Array 不能直接赋值类型化变量；同步 data.sprites/rects/dirty/count）；主视图切片数据卡 GroupRow（GroupOption 下拉「**全部**+未分组+各分组」+「按组选择」+「删除选中」（确认弹窗）按钮）；**编辑面板三分支**：标题「切片数据」，单选→完整表单（含分组 LineEdit）、多选→批量分组表单（BatchHint + BatchRow[已有分组下拉→填充输入 + 分组输入 + 保存]）、无选→占位；`_selected_count()` 区分多选/无选。⚠️ OptionButton 选项代码重建（tscn item_N/text 不恢复，沿用项目约定）。
- **侧栏「分析」tab 基础信息（2026-08-27，只读）**：InfoCard 折叠卡片（与侧栏卡片一致）5 行只读 Label：UID（data.sheet_uid，无则 ResourceUID 反查）/ 路径（image_res_path）/ 尺寸（宽×高）/ 纹理（res:// 已导入 vs 外部内存图）/ 项目数据路径（data_path）；`side._refresh_info_panel()` 由 `image_loaded`（开/关素材）+ `data_loaded`（配置就绪补 uid）触发；关闭素材回 "-"。⚠️ `_cards` 数组/harness 卡片数断言随卡片增删同步（现 7 张）。
- **项目数据 Resource（.tres，uid 关联）**：`ui/sps_data.gd`（`class_name SpriteSplitterData extends Resource`）——@export 存常用参数/导出位置/项目名/`sprites: Array[Rect2i]`（**兼容 meta.json rects**）；`SpsController` 维护 `data/data_path`：`load_image` 时 `ResourceLoader.get_resource_uid(path)`（⚠️ **返回 int**，转文本用 `ResourceUID.id_to_text(id)`，判空用 `ResourceUID.INVALID_ID`）→ 文件名 = `uid://...` 去前缀 → `res://sps_data/<tag>.tres`；存在则 `load` 恢复（发 `data_loaded` 信号 → 侧栏填参数/项目名/导出），否则 `SpriteSplitterData.new()` 初始化（项目名自动=素材名）并落盘；`save_project(name, options, out_dir, export_mode)` 写盘 + `_refresh_filesystem()`；切分/导入 meta/去背景时同步 `data.sprites`（内存）。侧栏顶部 ProjectRow：项目名 LineEdit + 保存按钮。**反向查询 `ResourceUID.get_id_path(id)`（uid→当前路径，Godot 4.4+）**：源文件更名后 uid 保留、路径失效 → 配置加载时 `source_image` 失效就靠它反查当前有效路径（`_resolve_source_path`），并就地修复 `d.source_image`（2026-08-25）。
- **`remove_theme_stylebox_override` 会删掉 tscn 里定义的 override**（2026-08-25 实测踩坑）：reg_item 根 PanelContainer 的透明面板在 tscn 里写的是 `theme_override_styles/panel`，与代码 `add_theme_stylebox_override` 存在**同一张映射**；`set_selected(false)` 调 `remove_theme_stylebox_override("panel")` 会把 tscn 定义的透明面板一并删掉 → 取消选中后回落成默认主题暗面板（`0.1,0.1,0.1,0.6`），列表里所有未选中条目背景变暗、选中高亮被淹没（现象："选中模式无效"；但 `selected` 布尔正常，test_harness 只断言布尔 → 全绿漏检）。修复：**永不 remove**——首次 `get_theme_stylebox("panel").duplicate()` 缓存 tscn 面板，之后每次 `duplicate` 只改 `bg_color` 再 `add_theme_stylebox_override`（未选中=透明、选中=SEL_BG），tscn 定义永不丢。⚠️ 验证样式用 `get_theme_stylebox`（override 优先），**没有 `get_theme_stylebox_override` 这个 API**（只有 has/add/remove 三个）。
- **代码主题化会覆盖 tscn 手动样式 + 折叠 header 双三角（2026-08-27 用户手动编辑后踩坑）**：① `_apply_theme` 的 `add_theme_stylebox_override("panel", ...)` 与 tscn `theme_override_styles/panel` 同映射 → 用户在 tscn 手动改的背景不生效（被代码覆盖）。修复：先 `has_theme_stylebox_override("panel")` 判断，tscn 已定义则跳过（用户手动样式优先，代码只兜底）。side / edit dock 均已加。② 用户在 tscn 把 header text 写成 `▾ 分析`，`_setup_fold_header` 又加 `▾ ` → 双三角。修复：初始也 `trim_prefix("▾ ").trim_prefix("▸ ")` 再拼（幂等）。⚠️ tscn 手动改样式是常见操作，两处都按此模式写。
- **`extends SceneTree` 探针脚本报 SCRIPT ERROR 后不 `quit` 会挂起**：脚本错误后主循环继续空转 → 进程被系统 SIGKILL（退出码 137）且 stdout 缓冲丢失（表现为"无输出 + 137"）；每次探测前保证 `quit(0)` 可达，先修脚本错误再跑。
- **源文件重命名自动修复（编辑器模式，2026-08-25）**：素材重命名/移动会触发 Godot 重导入流程，绑定 `EditorInterface.get_resource_filesystem()` 信号自动修复注册表里失效的 `source_image`：`resources_reimported`（带 PackedStringArray 参数）与 `filesystem_changed`（无参）连到同一槽，槽签名 `_res: Variant = null` 兼容（Godot 允许槽参数少于信号）；回调里 `_repair_stale_registry_paths()` 扫 registry.entries，`source_image` 失效 → `_resolve_source_path`（uid 反查）修复 + 同步 `source_texture` + `ResourceSaver.save`，有修复则 `registry_updated.emit()` 重建列表刷缩略图。**必须 `Engine.is_editor_hint()` 守卫**——运行模式/headless 无 EditorInterface，直接跳过（测试只能验证 repair 逻辑本身，信号绑定需 GUI 编辑器实测）。连接时机：main 的 `set_controller`（在树内、编辑器已就绪）。
- **HSeparator 的 `separator` 主题项是 StyleBox（非 Color）**（2026-08-25 实测）：4.6 绘制走 `get_theme_stylebox("separator")`，设分隔线颜色用 `theme_override_styles/separator = StyleBoxLine(color=...)`（tscn 里直接改，编辑器会自动转成 sub_resource StyleBoxLine）；`theme_override_colors/separator` 只是旧 fallback——代码设色：`StyleBoxLine.new()` + `add_theme_stylebox_override("separator", sb)`。
- **源纹理可序列化进 Resource .tres 的前提**（2026-08-25）：项目内导入素材 `load()` 得 CompressedTexture2D → `@export var source_texture: Texture2D` 存 .tres 是 ext_resource 引用（注册表缩略图直接用它，免每次 load+缩放）；**外部素材/运行时 ImageTexture（去背景等内存图）不能序列化**——保存前必须置 null（controller `_sync_source_texture()` 按 `source_image` 是否 res:// 决定存/置 null；旧 .tres 无该字段时缩略图自动回退路径加载，兼容）。
- **项目注册表（SpriteSplitterRegistry）**：`ui/sps_registry.gd`（`class_name SpriteSplitterRegistry extends Resource`，**必须 `@tool`**——非 @tool 的 Resource load 后是 placeholder，调方法报 `Attempt to call a method on a placeholder instance`）；`entries: Array[String]` 存已注册 data 路径；默认注册表 `res://sps_data/registry.tres`（controller `_ensure_registry`：不存在则 new+save，再 `_scan_registry` 扫目录补登记）；`_register_data(path)` 在新建（`_load_or_create_data` 落盘）与 `save_project` 成功时自动调用（去重 + 持久化 + `registry_updated` 信号）；UI 主视图右侧下半「项目注册表」卡片：ItemList 显示 `项目名 (文件名)`，`set_item_metadata` 存路径，`item_selected` → `controller.load_registry_entry(path)`（校验 + 统一走 `apply_data`）。
- **主屏幕窗口主题化（header/footer/side 分层）**：独立 workspace（非 dock）需给 TopBar/BottomBar/SidePanel 可视化背景 + padding + 分隔线，**跟随编辑器主题**：`_apply_theme()` 里 `EditorInterface.get_editor_theme()` 取 `dark_color_1`（面板 bg）/`separator`（分隔线）色，构造 StyleBoxFlat（`bg_color` + `content_margin_*` padding + `set_border_width(SIDE_*, 1)` 分隔线），`add_theme_stylebox_override("panel", sb)`；非编辑器模式回退内置深灰；`EditorInterface.get_base_control().theme_changed` 连接刷新；**⚠️ 结构改动后 @onready 路径同步更新**（TopBar 包进 PanelContainer 后 get_node("Main/TopBar/...") 变 "Main/TopBarPanel/TopBar/...")；⚠️ 4.6 里 `MARGIN_*` 不是全局常量，用 `SIDE_LEFT/TOP/RIGHT/BOTTOM`。
- **导出文件后资产库不刷新**：编辑器内用 FileAccess/GDExtension 写盘的文件不会自动出现在 FileSystem dock，需主动触发重扫：`EditorInterface.get_resource_filesystem().scan_sources()`（增量，批量导出用；参考 limboai 同款调用）；仅编辑器模式调用（`if not Engine.is_editor_hint(): return`），运行模式直接跳过；`scan_sources()` 异步生效，文件会稍后出现。
- **OptionButton 选项用代码 `add_item()` 重建，不要用 tscn 序列化**（2026-08-25 实测）：Godot 4.6 下 tscn 的 `item_N/text` **只恢复 item_count、文本全空**（`get_item_text()` 返回 ''，`selected=-1`），下拉框显示空白且无报错；`_ready` 里 `clear()` + 遍历常量数组 `add_item()` + `select(0)`，编辑器/运行模式都可靠。诊断脚本：实例化场景后打印 `get_item_text(i)`。
- **画布框选/红框 = 世界坐标（图片像素）**：CanvasView `_gui_input` 里左键框选、中键平移、右键清除、滚轮以鼠标为锚缩放、双击 fit；**触控板手势（macOS，仅捏合缩放）**：`InputEventMagnifyGesture`（双指捏合 → `zoom_at(pos, factor)`）；**双指拖动 PanGesture 已按用户要求移除（2026-08-25）**；图片与红框同一变换绘制 → 天然像素对齐，无视图逆映射（旧 overlay+TextureRect 方案红框错位源于手动 scale/offset 与 KEEP_ASPECT_CENTERED 不一致）。
- **EditorPlugin 移除方法名是 `remove_control_from_docks`（复数 s）**：`remove_control_from_dock` 不存在，check-only/编辑器加载报 `Function not found in base self`（headless classdb 实测 + 官方文档确认）。
- **RefCounted 禁止手动 `free()`**：`SpriteSplitter.new()` 返回 RefCounted，`_exit_tree` 里 `_splitter.free()` 会报 `Can't free a RefCounted object`；置 `null` 由引用计数释放。
- **meta.json 导入**：格式与 CLI 同源 `{image,width,height,sprites:[{x,y,width,height}]}`，解析时跳过 `w<=0||h<=0` 条目；`FileAccess.get_file_as_string` + `JSON.parse_string`。
- **手写 tscn 根节点必须带 `script = ExtResource(...)` 行**（2026-08-27 实测踩坑）：只声明 `[ext_resource type="Script"]` 而根节点漏挂 → 场景 `load()` 成功、`instantiate()` 后 `get_script()==null`（无任何报错），UI 全静默失效；排查用探针脚本 `load(tscn).instantiate()` 打印 `get_script()`（对照同目录已知正常场景）。新 tscn 的 ext_resource 带 `uid="uid://..."`（从 `.uid` 文件拷，--import 自动生成）+ 场景头 `uid`（自造 `uid://c0spscln001` 风格 11 字符可）。
