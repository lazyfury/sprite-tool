# sprite-tool — 项目规划（agent.md）

> 本文档是项目级智能体工作指南：目标、架构、里程碑与验证标准。
> 开发前先读本文件 §5 验证标准与 §6 开发规范；任务状态以 `todo.md` 为准。

## 1. 项目概述

- **项目名**：sprite-tool；**产品名**：Sprite Splitter（雪碧图智能切割 / Sprite Sheet Analyzer）
- **定位**：核心产品是 **Sprite Sheet Analyzer**（分析出 sprite rects），而非单纯的图像切割器；支持导出 PNG / JSON 元数据 / Godot AtlasTexture 资源；支持自动检测 + 手动框选（meta.json 往返）
- **技术路线**：**C++20 核心算法库（零外部依赖、不依赖 Godot）+ CLI frontend + Godot GDExtension 薄封装**（Phase 5）
- **当前状态**：M1–M3.5 已完成（CLI 子命令化 + 机器可读输出，92 用例 / 2418 断言全绿；auto 回退 components 时自动滤噪）；M4/M5 搁置
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
| Godot | **4.7.2.stable**（`/Applications/Godot.app`；⚠️ 2026-08-27 实测本机已从 4.6.2 升级到 4.7.2，原 4.6.2 记录作废） | 目标引擎版本 |
| 编译器 | Apple clang 21.0.0（Xcode CLT） | 完整支持 C++20/23 |
| git | 2.50.1 | — |
| Homebrew | 6.0.18 | 可装 cmake/ninja |
| **cmake / ninja** | ✅ 已安装（brew） | 构建系统，M1 起已用 |

### 2.2 网络与依赖获取策略

- `api.github.com` 快（~0.35s）；`github.com` 慢（~10s）；`raw.githubusercontent.com` 抖动（时好时坏）；代理 7890 未运行
- **结论：所有第三方依赖 vendoring 进 `third_party/`**（源码入库，构建不依赖网络），经 `api.github.com` 下载一次后入库
  - **例外：godot-cpp 不入库**——GDExtension 构建依赖，用 **git submodule**（gitlink 指针 + .gitmodules），参考 limboai 的"源码库不存实体"；本环境初始化走 api.github.com tarball + `git update-index --cacheinfo 160000`（github.com git clone 在代理下不通）
- 已实测可获取：
  - `stb_image.h` (v2.30, 283KB) / `stb_image_write.h` (71KB) — 图片读写
  - `catch_amalgamated.hpp` (548KB) — 测试框架单头版
  - `nlohmann/json.hpp` 单头版 — JSON 导出（可选，M2 再用）
  - CLI11 — v2.7.2 单头版已 vendored（M4b 起 CLI 使用）

### 2.3 godot-cpp / GDExtension 可行性（2026-08-25 更新）

- godot-cpp master（10.x, beta）独立于 Godot 版本号，要求显式 `api_version`（支持 4.3+ 含 4.6），或 `custom_api_file` 指定本机 `godot --dump-extension-api` 生成的 api 文件
- 稳定分支：`godot-4.5-stable`（旧绑定体系）；GDExtension 兼容性：**低版本扩展可在高版本 Godot 运行**（4.5 扩展可在 4.6 运行）
- ✅ **godot-cpp 现已官方支持 CMake**（现代化重构，二级但活跃维护）——本机无 scons，Phase 5 用 **CMake** 构建动态库（选项前缀 `GODOTCPP_`，见 `.pi/skills/godot-gdextension/SKILL.md` §5.1）
- ⚠️ 官方文档 C++ 章节 4.6 起迁移到 `tutorials/scripting/cpp/`（旧 `gdextension/` 路径 404）；docs 站有反爬，rst 源走 godot-docs GitHub raw
- 结论：**Godot 4.6 + GDExtension 路线可行**，Phase 5 再实施（当前机器有 Godot 4.7.2 可直接验证）

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
| CLI | CLI11 单头版（vendored `third_party/cli11`，M4b 起）；子命令 + 类型/范围校验 + 自动 help |
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
│   │                   #   background.cpp、background_remover.cpp（接口+工厂）、splitter.cpp
│   ├── model/          #   sprite_rect.hpp、split_options.hpp、split_result.hpp
│   ├── (根)            #   analyzer.hpp/cpp（ImageStats 统计 + 参数推荐）
│   └── export/         #   png_exporter.cpp（含内存编码 encode_png）、json_exporter.cpp、sheet.cpp（重排）
│
├── extra/              # 可选后端库（挂在 core 接口上，core 保持零网络）
│   └── bg_remote/      #   bg_remote.cpp：Remote 背景后端（httplib，网络依赖只在此）
│
├── cli/                # CLI frontend（main.cpp，依赖 core + extra）
├── godot/              # GDExtension（Phase 5）
│   ├── src/            #   sprite_splitter.cpp/hpp、register_types.cpp
│   ├── project/        #   测试用 Godot 工程
│   └── godot-cpp/      #   submodule（Phase 5 引入）
├── tests/              # Catch2 单测（image/mask/components/grid/splitter/background/analyzer/mask_io/sheet）
├── third_party/        # vendored 依赖（stb/、catch2/、json/）
└── docs/               # 设计文档（ADR 等）
```

**分层原则**：`core/` 不依赖 Godot 类型、不依赖 stdio/网络（零网络依赖的纯算法库）；
网络相关实现放 `extra/`（挂在 core 接口上，如 `bg_remote`）；CLI 与 Godot 只是 frontend，只做数据转换。

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
SplitResult split_image(const Image& image, const SplitOptions& options,
                        const Mask* bg_mask = nullptr);  // 外部背景 mask（remote/AI）
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
- [x] 子命令化：`info` / `split` / `manual` / `from-json` / `sheet` / `remove-background`（命令互斥、共享 flag 白名单校验）
- [x] `--format json`：stdout 只含结果对象、进度走 stderr（管道友好，可 jq）
- [x] `-q` 静默（text 模式仅摘要）与 `--version`
- [x] 全量回归：92 用例 / 2418 断言全绿

**验收**：`sprite-split info input.png --format json | jq '.components'` 链路可用；五命令 help 齐全、flag 校验正确。

#### M4 — AI 分割（可选，后期）
- [x] `BackgroundRemover` 抽象接口已落地（`core/segmentation/background_remover.hpp`：`virtual Mask process(const Image&)` + 注册表/工厂）：`Color` 后端内置在 core，`Remote` 后端在 `extra/bg_remote`（CLI main 入口 `register_backend()` 注册）；统一 mask 语义，remote 下 `--contract` 同样可用
- [ ] ONNX Runtime 集成（同一接口挂新后端即可，方案见 `docs/ai-backend.md`）
- [ ] 模型文件外置 `models/`（rembg/isnet/custom），核心不绑定模型

#### M5 — Godot GDExtension 插件
- [x] godot-cpp 引入（**git submodule** `godot/godot-cpp`，godot-4.5-stable @ e83fd09，gitlink 指针不入库；本环境 github.com git clone 不通 → api.github.com tarball + `git update-index --cacheinfo 160000` 建指针，2026-08-25 实测）
- [x] 数据转换层：godot::Image ↔ core Image；SpriteRect → Rect2i（`godot/src/conversion.cpp`）
- [x] `SpriteSplitter` 类（RefCounted，GDScript 可调 `split/analyze/crop/export_sprite/split_and_export`，`godot/src/sprite_splitter.cpp`）
- [x] addons 规范布局（`project/addons/sprite_tool/`：plugin.cfg + editor_plugin.gd（@tool EditorPlugin）+ bin/（.gdextension + 动态库））
- [ ] EditorPlugin GUI 验证（骨架完成；4.6.2 编辑器模式 bug 阻塞 headless 验证，需 GUI 实测：Plugins 启用 → Tools 菜单切分）
- [x] 用本机 Godot 4.6.2 加载验证（无头冒烟 24 断言全 PASS；`.gdextension` 配置各平台动态库；addons 布局下加载正常）

**验收**：Godot 编辑器内可对素材表一键切分，生成 `res://sprites/xxx_01.png` 或 atlas 资源。（核心类、导出链路、addons 布局已通；EditorPlugin 待 GUI 验证）

**M5 调研结论（2026-08-25）**：
- GDExtension 加载机制：运行时只读 `res://.godot/extension_list.cfg`（编辑器扫描 *.gdextension 生成，含 addons/），不扫描 res://；删缓存后须编辑器导入或手动写列表（SKILL.md §2.5）
- 4.6.2 编辑器模式崩溃（EditorHelp 扩展文档生成 bug）：带扩展 `--import`/`-e` 退出时崩，与 reloadable 无关；规避 = 无扩展两步法导入（SKILL.md §7）。**✅ 4.7.2 已修复**（2026-08-27 实测 `--editor --quit-after` 带扩展正常退出，插件加载无错误）

## 5. 验证标准（每模块提交前）

1. **构建通过**：`cmake -B build && cmake --build build -j` 无 error/warning（核心库 + CLI）
2. **测试全绿**：`ctest` 通过；核心算法有对应单测（image/mask/components/grid/splitter/background/analyzer/mask_io/sheet）
3. **算法正确性**：CCL 对隔离分量、U 形连通、全图单分量、空图等边界用例正确
4. **core/ 零污染**：core 头文件不 include Godot/stdio/网络头；网络类实现收敛在 `extra/`；CLI/Godot 层只做转换
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

### 6.1 Skill 使用规范

项目 skill 以 **SKILL.md 单文件**形式维护，两份副本通过软连接同步：

| skill | 内容 | 何时读取 |
|---|---|---|
| `sprite-splitter` | CLI 参数/推荐工作流/陷阱/验证方法 | **调用 `sprite-split` CLI 前必读** |
| `godot-gdextension` | Godot 4.x GDExtension 制作（类注册/构建/数据转换） | 开发 `godot/`（M5）时读取 |
| `sprite-plugin-ui` | 插件 UI 开发（布局/交互/独立场景测试/编码约定） | 开发 `godot/project/addons/sprite_tool/ui/` 时读取 |

**Godot/GDScript 编码约定（强制）**：
- 节点路径一律用 `/` 表示子层级（`get_node("Main/Content/SidePanel/Side/SplitBtn")`），不链式 get_node；**例外：易随结构变动失效的关键节点（如主视图 CanvasView）用唯一名 `%` 访问**（`get_node("%CanvasView")`，2026-08-25 用户指定）
- GDScript 变量一律显式类型标注（`var x: Type = ...`），**不用 `:=`**（`const` 语言限制除外）；函数参数/返回值标注类型

- **源位置**：`.pi/skills/<name>/SKILL.md`（唯一权威副本，直接编辑这里）
- **软连接**：`.workbuddy/skills/<name>` → `../../.pi/skills/<name>`（标准项目级扫描位；修改源后软连接自动生效，勿在 `.workbuddy/skills/` 直接改）
- 读取方式：`Read .pi/skills/<name>/SKILL.md`（或经软连接路径）；godot-gdextension 内容同步可用于任何 Godot 插件开发
- 新增 skill：在 `.pi/skills/` 建目录写 SKILL.md（frontmatter 须含 `name` + `description`），再建软连接到 `.workbuddy/skills/`

> ⚠️ **WorkBuddy 专属**：`.workbuddy/skills/` 扫描位、`.workbuddy/memory/` 记忆、frontmatter（`name`/`description`）约定均为 **WorkBuddy 专属机制**，其他 agent 工具（Claude Code 等）不会识别这些路径；换工具时以 `.pi/skills/` 源文件为准（通用 SKILL.md 格式，可手动读取/迁移）。

## 7. 风险与备注

- **CCL 性能**：two-pass + union-find 为 O(W×H)，单张素材表足够；大规模批量处理再上 SIMD/并行（M2+ 优化项，不阻塞一版）
- **合并策略**：merge_distance + 形态学两种实现并存，像素游戏用形态学更稳（先膨胀合并再腐蚀回原边界）
- **Grid 自动判定**：以「非空 cell 占比」与「cell 内内容完整性」评分。✅ **已解决**：早期真实不规则素材上评分不稳定（8×8 网格被误判为 64）；已按「基于精灵间距检测」方向修复（投影+自相关找周期 + 谐波抑制 + 低置信回退 components），含 test_grid auto 用例；复杂素材上仍可显式指定 cell_size
- **godot-cpp 10.x 为 beta**：M5 时优先用 `godot-4.5-stable` 分支（稳定），需要 4.6 API 特性再评估 master；扩展 4.5 构建可在 4.6 运行
- **网络**：依赖已 vendoring，构建与开发不受网络抖动影响；下载依赖走 `api.github.com`
- **AI 阶段**：模型体积与推理延迟是本机风险，设计上 AI 只是 `BackgroundRemover` 的一个实现，可随时替换/回退
