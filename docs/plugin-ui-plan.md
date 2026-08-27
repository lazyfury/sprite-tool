# sprite-tool 插件 UI 设计与交互规划

> 状态：规划定稿，M5.1（独立场景 MVP）实施中
> 关联：`godot/project/addons/sprite_tool/ui/`（场景）、`godot/src/sprite_splitter.cpp`（GDExtension API）、`docs/` 其余设计文档
> 里程碑：M5.1 独立场景测试 → M5.2 挂载编辑器（dock/窗口）

## 1. 目标与约束

- **目标**：为 Sprite Splitter 提供图形界面——选图 → 分析 → 切分 → 预览 → 导出，覆盖 CLI 的 `info / split / from-json / sheet` 主要能力，并新增 AtlasTexture 运行时消费路径（不切图）。
- **约束**：
  - GDExtension 暴露的 API 即能力上限：`split / analyze / crop / export_sprite / split_and_export / export_metadata`（无 sheet 重排，M5.1 不提供打包导出）。
  - **插件 tscn 先不挂载到编辑器**（4.6.2 编辑器模式有 `EditorHelp::_gen_extensions_docs` 崩溃 bug，且 EditorPlugin GUI 无法 headless 验证）→ M5.1 用**独立可运行场景**验证全部交互，验证通过后再挂 EditorPlugin dock。
  - **编码约定（项目强制）**：
    1. 编辑 Godot 节点时，子层级一律用 `/` 表示（`get_node("Main/Content/Side/SplitBtn")`），不写 get_node 链式或数组路径。
    2. 写 GDScript 时**避免类型推断**：`var` 一律显式类型标注（`var img: Image = ...`），不使用 `:=`（`const` 因语言限制除外）。

## 2. 能力清单（UI 映射）

| 能力 | GDExtension API | UI 控件 | 说明 |
|---|---|---|---|
| 加载素材 | `Image.load_from_file` | 文件对话框 + 预览 | PNG/JPG，显示尺寸 |
| 分析 | `analyze` | 「分析」按钮 + 信息标签 | 组件数 / 建议 min 尺寸 / 前景占比 |
| 切分 | `split`（mode + 参数） | 模式下拉 + 参数 SpinBox + 「切分」按钮 | auto / components / grid |
| 区域预览 | split 返回的 rects | 预览叠加层（_draw 描边） | 可开关 |
| 导出 PNG | `split_and_export` | 导出模式下拉 + 「导出」 | 写 N 个 PNG + meta.json |
| 仅元数据 | `export_metadata` | 同上 | 不切图，只写 meta.json |
| AtlasTexture | 引擎 `AtlasTexture` + `ResourceSaver` | 同上 | 不切图，生成 .tres 资源（已验证可行） |
| 单张裁剪 | `crop / export_sprite` | 预览框选（M5.2） | 后续 |

## 3. UI 布局设计

整体：**单窗口 Control 场景**，三区结构（顶部工具条 / 中部预览+参数 / 底部导出条）。

```
SpriteSplitterUI (Control)                      [脚本 sprite_splitter_ui.gd]
└── Main (VBoxContainer)                        anchors 全屏
    ├── TopBar (HBoxContainer)                  标题 + 打开文件
    │   ├── FileButton (Button)                 "打开素材表..."
    │   └── FileLabel (Label)                   当前文件路径 / 尺寸
    ├── Content (HSplitContainer)               size_flags_vertical = expand
    │   ├── PreviewPanel (PanelContainer)       预览区
    │   │   └── Preview (Control)               custom_minimum_size 480x480
    │   │       ├── Texture (TextureRect)       原图，stretch keep aspect centered
    │   │       └── Overlay (Control)           [脚本 overlay.gd] 切分矩形描边
    │   └── SidePanel (PanelContainer)          参数区
    │       └── Side (VBoxContainer)
    │           ├── ModeOption (OptionButton)   auto / components / grid
    │           ├── ParamGrid (GridContainer)   columns=2
    │           │   ├── MinW (SpinBox)          最小宽
    │           │   ├── MinH (SpinBox)          最小高
    │           │   ├── CellSize (SpinBox)      格子尺寸（grid）
    │           │   ├── MergeDist (SpinBox)     合并距离（components）
    │           │   └── AlphaThr (SpinBox)      alpha 阈值
    │           ├── BgRemove (CheckButton)      "去背景（白底素材）"
    │           ├── AnalyzeBtn (Button)         "分析"
    │           ├── InfoLabel (Label)           分析结果（autowrap）
    │           ├── SplitBtn (Button)           "切分"
    │           └── CountLabel (Label)          切分结果数量 / 错误
    └── BottomBar (HBoxContainer)               导出条
        ├── ExportModeOption (OptionButton)     切 PNG / 仅 meta.json / AtlasTexture
        ├── OutDir (LineEdit)                   输出目录 res://out_sprites/ui
        ├── ExportBtn (Button)                  "导出"
        └── StatusLabel (Label)                 导出状态 / 进度
```

## 4. 交互流程（状态机）

```
[IDLE] --打开文件--> [LOADED] --分析--> [ANALYZED] --切分--> [SPLIT] --导出--> [EXPORTED]
                            └-------- 参数变更 → 回到 [LOADED]（区域失效）---------┘
```

- **IDLE → LOADED**：FileDialog 选图 → 加载 Image → 更新预览与缩放映射 → 清空旧区域。
- **LOADED → ANALYZED**：「分析」→ `analyze()` → InfoLabel 显示 component_count / 建议 min / 前景占比；**自动把建议值填入 MinW/MinH**（对齐 CLI analyzer 启发式）。
- **ANALYZED/LOADED → SPLIT**：「切分」→ 按 ModeOption + 参数组 options 字典 → `split()` → 校验 rects → Overlay 描边 + CountLabel 显示数量。
- **SPLIT → EXPORTED**：「导出」→ 按 ExportModeOption 分支：
  - 切 PNG：`split_and_export()`（协程，导出中禁用按钮 + 状态刷新）
  - 仅 meta.json：`export_metadata()`，复用当前 rects（来自切分或从已有 meta.json 读入——M5.2）
  - AtlasTexture：遍历 rects 生成 `.tres`（atlas 用 `load()` 导入纹理，`ResourceSaver.save`），数量可设上限
- 任一步失败：CountLabel/StatusLabel 红字提示，不改变状态。

**切分预览坐标映射**：TextureRect 为 `STRETCH_KEEP_ASPECT_CENTERED`；Overlay 与 Texture 同尺寸，主脚本计算
`scale = min(w/img_w, h/img_h)`、`offset = (tex_size - img*scale)/2`，注入 Overlay 后 `_draw()` 逐矩形描边。

## 5. 导出能力设计

| 模式 | 输出 | 实现 | 备注 |
|---|---|---|---|
| 切 PNG | `<out>/*.png` + `meta.json` | `split_and_export` | 与 CLI 产物同源格式 |
| 仅 meta.json | `<out>/meta.json` | `export_metadata` | 不切图，供 from-json / AtlasTexture 消费 |
| AtlasTexture | `<out>/atlas_*.tres` | 引擎 API | atlas 用 `load()` 导入纹理；region = rects |

导出统一走协程（`await` 让出帧），大图导出期间 UI 不冻结（复用 main.gd 协程化经验）。

## 6. 独立场景测试策略（M5.1，不挂编辑器）

- 场景放 `addons/sprite_tool/ui/`，**运行方式**：编辑器选中 tscn 直接 F6，或命令行
  `Godot --headless --path . res://addons/sprite_tool/ui/sprite_splitter_ui.tscn`。
- **headless 自动测试**：`_ready()` 先跑 `_selftest()`（内置 sheet.png → analyze → split → 断言 64），
  检测到自动测试标记（环境变量 `SPS_UI_TEST=1` 或用户参数 `--sps-ui-test`）再跑 `_auto_test()`
  （完整导出链路 PNG/meta/tres + 断言 + quit）。这样 CI/无头可回归，GUI 交互人工验证。
- **M5.2 挂载编辑器**：新增 `PluginUiDock` 子类化场景（或 `EditorPlugin.add_control_to_dock` 包装），
  复用同一套逻辑脚本；EditorPlugin 仅负责容器与菜单入口（保持薄封装）。

## 7. 编码约定（本模块强制）

1. **节点路径 `/` 层级**：所有 `get_node()` 传完整 `/` 分隔路径；不链式 `get_node().get_node()`、不用 `%` 唯一名。
2. **显式类型**：`var x: Type = value`，禁止 `:=`（`const` 语言限制除外）；函数参数与返回值一律标注类型；
   字典字面量标注 `: Dictionary`，数组标注元素类型（`Array[Rect2i]`）。
3. 控件引用用 `@onready var _x: Type = get_node("...")`，信号用 `connect` 在 `_ready` 集中绑定。
4. 所有导出/切分重操作放协程 + `await get_tree().process_frame` 让帧。

## 8. 里程碑拆分

- **M5.1（本次）**：独立场景 MVP——选图/分析/切分/预览/导出三模式 + headless 自测全 PASS。
- **M5.2**：挂载 EditorPlugin dock/窗口 + 手动框选（crop/export_sprite）+ 从已有 meta.json 导入 rects。
- **M5.3（可选）**：sheet 打包导出（需 GDExtension 补 sheet API）→ 对齐 CLI `sheet`。

## 9. 风险与对策

| 风险 | 对策 |
|---|---|
| 4.6.2 编辑器模式崩溃（EditorHelp 文档生成） | M5.1 不挂编辑器，独立场景验证；M5.2 用两步法导入规避 |
| 大图切分/导出卡 UI | 协程分帧（已验证） |
| Overlay 与预览错位（keep aspect） | 统一 scale/offset 计算，自测断言映射函数 |
| .tres 保存依赖导入纹理 | 统一用 `load()` 获取 atlas（动态 ImageTexture 不可内联） |
