# Sprite Splitter — 项目规划（agent.md）

> 本文档是项目级智能体工作指南：目标、架构、里程碑与验证标准。
> 开发前先读本文件 §5 验证标准与 §6 开发规范；任务状态以 `todo.md` 为准。

## 1. 项目概述

- **名称**：Sprite Splitter（雪碧图智能切割 / Sprite Sheet Analyzer）
- **定位**：核心产品是 **Sprite Sheet Analyzer**（分析出 sprite rects），而非单纯的图像切割器；支持导出 PNG / JSON 元数据 / Godot AtlasTexture 资源；支持自动检测 + 手动框选（meta.json 往返）
- **技术路线**：**C++20 核心算法库（零外部依赖、不依赖 Godot）+ CLI frontend + Godot GDExtension 薄封装**（Phase 5）
- **当前状态**：M1–M3.5 已完成（CLI 子命令化 + 机器可读输出，88 用例 / 347 断言全绿）；M4/M5 搁置
- **产品形态演进**：
  ```
  sprite-splitter
  ├── CLI            ← Phase 1-2 先做
  ├── Godot Plugin   ← Phase 5（GDExtension，EditorPlugin）
  └── macOS GUI      ← 未来可选
  ```
  底层全部复用同一套 C++ Core。

## 2. 技术调研结论（2026-08 实测，已确认可行性）

### 2.1 本机环境

| 项 | 状态 | 备注 |
|---|---|---|
| macOS | darwin-arm64 | Apple Silicon |
| Godot | **4.6.2.stable**（`/Applications/Godot.app`） | 目标引擎版本 |
| 编译器 | Apple clang 21.0.0（Xcode CLT） | 完整支持 C++20/23 |
| git | 2.50.1 | — |
| Homebrew | 6.0.18 | 可装 cmake/ninja |
| **cmake / ninja** | ✅ 已安装（brew） | 构建系统，M1 起已用 |

### 2.2 网络与依赖获取策略

- `api.github.com` 快（~0.35s）；`github.com` 慢（~10s）；`raw.githubusercontent.com` 抖动（时好时坏）；代理 7890 未运行
- **结论：所有第三方依赖 vendoring 进 `third_party/`**（源码入库，构建不依赖网络），经 `api.github.com` 下载一次后入库
- 已实测可获取：
  - `stb_image.h` (v2.30, 283KB) / `stb_image_write.h` (71KB) — 图片读写
  - `catch_amalgamated.hpp` (548KB) — 测试框架单头版
  - `nlohmann/json.hpp` 单头版 — JSON 导出（可选，M2 再用）
  - CLI11 — multi-header（可选；CLI 参数不多时也可手写解析，M1 先手写）

### 2.3 godot-cpp / GDExtension 可行性

- godot-cpp master（10.x, beta）支持 `api_version` 参数（4.3+ 均可），或 `custom_api_file` 指定本机 `godot --dump-extension-api` 生成的 api 文件
- 稳定分支：`godot-4.5-stable`；GDExtension 兼容性：**低版本扩展可在高版本 Godot 运行**（4.5 扩展可在 4.6 运行）
- ⚠️ **godot-cpp 官方构建系统是 SCons，不是 CMake** —— Phase 5 时 godot 层用 SCons 构建动态库，或评估第三方 cmake 支持
- 结论：**Godot 4.6 + GDExtension 路线可行**，Phase 5 再实施（当前机器有 Godot 4.6.2 可直接验证）

### 2.4 魔棒背景清理调研（M3 补充，⏸ 搁置 — 待 UI 阶段再评估）

现有 `--remove-background`（四角均值取色 + RGB 曼哈顿距离 + 四边播种 flood fill）的根因短板：**背景色来源不可控**（只能四角自动或手填单色）→ 前景占满四角 / 渐变阴影 / 多区域背景等场景失效。
**结论**：魔棒不做交互 UI（明确排除点击预览），以 `--seed X,Y`（可重复）参数化加入现有 `background_mask` 的 BFS 起始集合并参与取色，最小侵入补齐短板；无 `--seed` 时行为逐字节兼容。
详细优缺点对比与设计见 `docs/magic-wand.md`。

## 3. 技术栈与目录规划

| 模块 | 技术 |
|---|---|
| 核心语言 | C++20 |
| 构建 | CMake（+ ninja） |
| 图片读取/写入 | stb_image / stb_image_write（vendored） |
| 图像处理 | 自研（Mask / CCL / morphology / grid） |
| JSON | nlohmann/json 单头（vendored，M2+） |
| 测试 | Catch2 单头版（vendored） |
| CLI | 手写参数解析（M1，参数少）；CLI11 备选 |
| Godot 集成 | godot-cpp GDExtension（Phase 5，SCons） |
| SIMD/多线程 | 后期优化（AVX2/NEON、std::execution），不在一版 |

**明确不做**：第一版不引入 OpenCV、不引入 ONNX/AI（作为后续可选阶段，见 §4 M4）。

```
sprite-tool/
├── agent.md            # 本文档：项目规划
├── todo.md             # 任务看板（唯一，实时更新）
├── README.md           # 项目简介
├── CMakeLists.txt      # M1 创建
│
├── core/               # C++ 核心算法库（零依赖，不 import Godot）
│   ├── image/          #   image.hpp/cpp、pixel.hpp（RGBA）
│   ├── mask/           #   mask.hpp/cpp、morphology.cpp（膨胀/腐蚀）、
│   │                   #   mask_io.cpp（橡皮擦 mask 读写）
│   ├── segmentation/   #   connected_components.cpp、grid_detector.cpp、
│   │                   #   background.cpp、splitter.cpp
│   ├── model/          #   sprite_rect.hpp、split_options.hpp、split_result.hpp
│   ├── (根)            #   analyzer.hpp/cpp（ImageStats 统计 + 参数推荐）
│   └── export/         #   png_exporter.cpp、json_exporter.cpp、sheet.cpp（重排）
│
├── cli/                # CLI frontend（main.cpp，依赖 core）
├── godot/              # GDExtension（Phase 5）
│   ├── src/            #   sprite_splitter.cpp/hpp、register_types.cpp
│   ├── project/        #   测试用 Godot 工程
│   └── godot-cpp/      #   submodule（Phase 5 引入）
├── tests/              # Catch2 单测（image/mask/components/grid/splitter/background/analyzer/mask_io/sheet）
├── third_party/        # vendored 依赖（stb/、catch2/、json/）
└── docs/               # 设计文档（ADR 等）
```

**分层原则**：`core/` 不依赖 Godot 类型、不依赖 stdio/网络；CLI 与 Godot 只是 frontend，只做数据转换。

## 4. 核心模块与里程碑

### 4.1 核心 API 设计（先行定义，M1 实现）

```cpp
// core/model/sprite_rect.hpp
struct SpriteRect { int x, y, width, height; };

// core/model/split_options.hpp
struct SplitOptions {
    int  alpha_threshold = 1;
    bool remove_background = false;
    int  padding = 0;
    int  min_width = 1, min_height = 1;
    bool merge_nearby = false;
    int  merge_distance = 0;
    enum class DetectionMode { ConnectedComponents, Grid, Auto };
    DetectionMode mode = DetectionMode::ConnectedComponents;
    int grid_cell_size = 16;          // Grid 模式
};

// core/model/split_result.hpp
struct SplitResult { std::vector<SpriteRect> sprites; };

// core/segmentation/splitter.hpp
SplitResult split_image(const Image& image, const SplitOptions& options);
```

**Mask 是一等公民**：算法链路 `Image → Mask → Components → Rects → Crop`，便于以后接入 Color/FloodFill/AI 各种 mask 来源。

### 4.2 里程碑

#### M1 — C++ Core + CLI 可用工具（先做，无 UI）
- [x] 环境准备：`brew install cmake ninja`（须用户确认）；CMake 工程骨架 + 目录规范
- [x] vendoring：stb_image / stb_image_write / catch_amalgamated / nlohmann-json 入库 `third_party/`
- [x] `Image`：PNG 读取（stb）、RGBA 像素访问、Crop、Padding
- [x] `Mask`：alpha > threshold 判定
- [x] `Connected Components`：two-pass labeling（含并查集 union-find）→ bounding box
- [x] `Splitter::split_image` 主流程
- [x] `PNG Exporter`：裁剪输出
- [x] CLI：`sprite-split input.png [--alpha-threshold N] [--padding N] [--output DIR] [--json]`（flat 形态；M3.5 重构为子命令）
- [x] 单测：image/mask/components/splitter 全绿

**验收**：`sprite-split input.png` 能正确切出全部 sprite；`--json` 输出元数据；Catch2 测试全绿；`core/` 零 Godot/stdio 依赖。

#### M2 — 像素游戏优化模式
- [x] Grid Detection：按 cell_size 统计非透明像素，生成格子 rect
- [x] Auto 模式：投影+自相关找周期 → offset 对齐 → 周期/对齐/边界/尺寸/占用多维评分 → 谐波抑制；低置信回退 components
- [x] 最小尺寸过滤（min_width / min_height）
- [x] Merge Distance：附近分量合并（膨胀 mask → CCL → 原 mask 重算精确 bbox）
- [x] Morphology：mask 膨胀/腐蚀（曼哈顿距离，与 merge_distance 语义一致）
- [x] JSON 导出（nlohmann/json 3.12.0，vendored）
- [x] 单测：grid / morphology / json 全绿

**验收**：16×16 角色表、有间隙的素材表均能正确识别；网格模式对空白 cell 正确跳过。

#### M3 — 背景清理（纯算法，无 AI）
- [x] 四角颜色采样 → 背景色估计 → color distance mask
- [x] Flood Fill（四角向内，容差阈值）背景 mask
- [x] 接入 `split_image` 的 `remove_background` 选项（+ `--bg-color R,G,B` 手动背景色覆盖采样）
- [x] 收缩导出 `--contract`（检测后向内收缩 N px）
- [x] 图片分析 `--info`（core/analyzer：alpha/背景/分量统计 + 参数推荐）
- [x] 橡皮擦工作流（core/mask/mask_io + `--gen-masks`/`--erase-tl`：白=保留 / 黑=透明）
- [x] sprite sheet 重排（core/export/sheet：按列排布，每格居中）
- [x] 单测：白底/黑底/纯色底用例（+ test_analyzer / test_mask_io / test_sheet）
- [ ] 魔棒补充：`--seed X,Y`（可重复）指定背景点，加入 BFS 起始集合 + 参与取色（⏸ 搁置，待 UI 阶段再评估；方案见 `docs/magic-wand.md`）

**验收**：白底 RPG 素材能自动去背景后正确切分。

#### M3.5 — CLI 子命令化 + 机器可读输出
- [x] 子命令化：`info` / `split` / `manual` / `from-json` / `sheet`（命令互斥、共享 flag 白名单校验）
- [x] `--format json`：stdout 只含结果对象、进度走 stderr（管道友好，可 jq）
- [x] `-q` 静默（text 模式仅摘要）与 `--version`
- [x] 全量回归：88 用例 / 347 断言全绿

**验收**：`sprite-split info input.png --format json | jq '.components'` 链路可用；五命令 help 齐全、flag 校验正确。

#### M4 — AI 分割（可选，后期）
- [ ] ONNX Runtime 集成，`BackgroundRemover` 抽象（接口已预留：`virtual Mask process(const Image&)`）
- [ ] 模型文件外置 `models/`（rembg/isnet/custom），核心不绑定模型
- [ ] 失败时回退纯算法，不污染主流程

#### M5 — Godot GDExtension 插件
- [ ] godot-cpp submodule（`godot-4.5-stable` 或 master 10.x + api_version）
- [ ] 数据转换层：godot::Image ↔ core Image；导出 SpriteRect 数组
- [ ] `SpriteSplitter` 单例类（GDScript 可调：`SpriteSplitter.split(image, options) -> Array[Rect2i]`）
- [ ] EditorPlugin：选择图片 → 预览 → 切分 → 导出 PNG / AtlasTexture / SpriteFrames
- [ ] 用本机 Godot 4.6.2 加载验证（`.gdextension` 配置各平台动态库）

**验收**：Godot 编辑器内可对素材表一键切分，生成 `res://sprites/xxx_01.png` 或 atlas 资源。

## 5. 验证标准（每模块提交前）

1. **构建通过**：`cmake -B build && cmake --build build -j` 无 error/warning（核心库 + CLI）
2. **测试全绿**：`ctest` 通过；核心算法有对应单测（image/mask/components/grid/splitter/background/analyzer/mask_io/sheet）
3. **算法正确性**：CCL 对隔离分量、U 形连通、全图单分量、空图等边界用例正确
4. **core/ 零污染**：core 头文件不 include Godot/stdio/网络头；CLI/Godot 层只做转换
5. **确定性**：同一输入 + 同一参数 → 同一输出（便于回归测试）
6. Godot 层（M5）：Godot 启动无脚本错误，插件功能可用

## 6. 开发规范

- 语言：C++20；命名 snake_case；头文件 `#pragma once`；成员 `m_` 前缀；类/文件与职责一一对应
- **分层铁律**：`core/` 不依赖 Godot；`cli/` 与 `godot/` 只调用 `split_image()` 并做数据转换
- 每个核心模块先写测试（或同 PR 提交测试），测试用 `tests/test_<模块>.cpp`
- 第三方代码只放 `third_party/`，不修改；构建时通过 include path 引用
- 提交信息：`M1: implement connected components` 风格；关键算法附注释说明出处/思路
- **每次会话结束**：将已完成/进行中任务同步到 `todo.md`；新任务随时追加到对应里程碑
- 环境变动（安装依赖等）先说明经用户确认；大量修改先列方案
- **agent 调用 CLI 前先读 `.pi/skills/sprite-splitter/SKILL.md`**（参数/工作流/陷阱/验证方法）

## 7. 风险与备注

- **CCL 性能**：two-pass + union-find 为 O(W×H)，单张素材表足够；大规模批量处理再上 SIMD/并行（M2+ 优化项，不阻塞一版）
- **合并策略**：merge_distance + 形态学两种实现并存，像素游戏用形态学更稳（先膨胀合并再腐蚀回原边界）
- **Grid 自动判定**：以「非空 cell 占比」与「cell 内内容完整性」评分。✅ **已解决**：早期真实不规则素材上评分不稳定（8×8 网格被误判为 64）；已按「基于精灵间距检测」方向修复（投影+自相关找周期 + 谐波抑制 + 低置信回退 components），含 test_grid auto 用例；复杂素材上仍可显式指定 cell_size
- **godot-cpp 10.x 为 beta**：M5 时优先用 `godot-4.5-stable` 分支（稳定），需要 4.6 API 特性再评估 master；扩展 4.5 构建可在 4.6 运行
- **网络**：依赖已 vendoring，构建与开发不受网络抖动影响；下载依赖走 `api.github.com`
- **AI 阶段**：模型体积与推理延迟是本机风险，设计上 AI 只是 `BackgroundRemover` 的一个实现，可随时替换/回退
