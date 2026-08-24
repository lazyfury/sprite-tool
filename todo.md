# Sprite Splitter — 任务看板

> 项目唯一任务看板。格式：按里程碑分节，`- [ ]` 未开始 / `- [x]` 已完成 / `（进行中）` 标注当前任务。
> 每次开发会话结束同步状态；与 `agent.md` §4 里程碑保持一致。

## M1 — C++ Core + CLI 可用工具

- [x] 环境准备：`brew install cmake ninja`（须用户确认后执行）
- [x] CMake 工程骨架（core 静态库 + cli 可执行 + tests，C++20）
- [x] vendoring：stb_image / stb_image_write / catch_amalgamated 入库 `third_party/`（走 api.github.com）
- [x] `core/model/`：SpriteRect / SplitOptions / SplitResult / DetectionMode
- [x] `core/image/`：Image（RGBA）、PNG 读取（stb_image）、Crop、Padding
- [x] `core/mask/`：Mask 类 + alpha threshold 判定
- [x] `core/segmentation/`：Connected Components（two-pass + union-find）→ bounding box
- [x] `core/segmentation/`：Splitter::split_image 主流程（Mask → CCL → Rects → Crop）
- [x] `core/export/`：PNG Exporter（stb_image_write）
- [x] CLI：`sprite-split input.png [--alpha-threshold N] [--padding N] [--min-width N] [--min-height N] [--output DIR] [--json] [--info] [--manual] [--from-json FILE]`（flat 形态；M3.5 重构为子命令）
- [x] 测试：test_image / test_mask / test_components / test_splitter 全绿（ctest）

**M1 验收（已达成）**：28 用例 / 132 断言全绿；`tests/fixtures/test_sheet.png` 端到端切分正确（3 精灵 + 半透明 + 噪点过滤）；core 零 Godot/stdio 依赖。

## M2 — 像素游戏优化模式

- [x] Grid Detection：按 cell_size 统计非透明像素，生成格子 rect
- [x] Auto 模式：**假设→打分→验证→回退**（投影+自相关找周期 → offset 对齐 → 周期/对齐/边界/尺寸/占用多维评分 → 谐波抑制；低置信回退 components）
- [x] 最小尺寸过滤（min_width / min_height）
- [x] Merge Distance：膨胀 mask → CCL → 原 mask 重算精确 bbox（形态学合并）
- [x] Morphology：dilate / erode（曼哈顿距离，与 merge_distance 语义一致）
- [x] JSON 导出（nlohmann/json 3.12.0，vendored）
- [x] 测试：test_grid / test_morphology / test_json / test_background 全绿

**M2/M3 验收（已达成）**：
- 88 用例 / 347 断言全绿（含 M3.5 新增 test_analyzer / test_mask_io / test_sheet）
- 标准 8×8 网格表（白底无 alpha）→ `--remove-background --mode auto` 自动检出 8px 网格并切出 5 个 cell
- 不规则图标集 → auto 自动回退 components 模式（+min-size 过滤正确切出 80 个精灵）
- 角色上下两块间隔 2px → `--merge-distance 3` 正确合并为 1 个整体（12×14）

## M3 — 背景清理（纯算法）

- [x] 四角颜色采样 → 背景色估计 → color distance mask
- [x] Flood Fill（四角向内，容差阈值）背景 mask
- [x] 接入 SplitOptions::remove_background / background_threshold
- [x] 收缩导出 `--contract`（检测后向内收缩 N px）
- [x] 图片分析 `--info`（core/analyzer：alpha/背景/分量统计 + 参数推荐）
- [x] 橡皮擦工作流 `--gen-masks` / `--erase-tl`（core/mask/mask_io：白=保留 / 黑=透明）
- [x] sprite sheet 重排（core/export/sheet：按列排布，每格居中）
- [x] 测试：白底 / 黑底 / 纯色底用例全绿（+ test_analyzer / test_mask_io / test_sheet）

**观察项（已解决）**：remove_background 导出时背景像素 alpha 置 0（`make_background_transparent`），导出的 PNG 背景透明。

**M3 补充：魔棒种子清理（--seed，⏸ 搁置 — 规划完成，待 UI 阶段再评估；方案见 `docs/magic-wand.md`）**
- [ ] `core/segmentation/background`：`BackgroundOptions::seeds` + BFS 起始集合扩展（种子 ∪ 四边）+ 种子取色优先级（bg-color > seeds 均值 > 四角均值）
- [ ] `core/model/split_options` + `cli/main.cpp`：`--seed X,Y` 可重复解析、与 `--remove-background` 配对校验、透传 `apply_background_transparency`
- [ ] 测试：前景占满四角 / 多区域不同底色 / 种子越界 warn / 种子在前景上 warn / 无 seeds 零回归
- [ ] 文档：SKILL.md 补 `--seed` 用法与陷阱（方案见 `docs/magic-wand.md`）

## M3.5 — CLI 子命令化 + 机器可读输出

- [x] 子命令化：`info` / `split` / `manual` / `from-json` / `sheet`（命令互斥、共享 flag 白名单校验）
- [x] `--format json`：stdout 只含结果对象，进度走 stderr（管道友好，可 jq）
- [x] `-q` 静默模式（text 模式仅摘要）与 `--version`
- [x] 全量回归：88 用例 / 347 断言全绿

**M3.5 验收（已达成）**：`sprite-split info input.png --format json | jq '.components'` 链路可用；五命令 help 齐全、flag 校验正确。

## M4 — AI 分割（可选，后期）

- [ ] BackgroundRemover 抽象接口（virtual Mask process(const Image&)）
- [ ] ONNX Runtime 集成 + 模型外置 `models/`（rembg/isnet/custom）
- [ ] 失败回退纯算法

## M5 — Godot GDExtension 插件

- [ ] godot-cpp submodule（优先 `godot-4.5-stable`；评估 master 10.x + api_version）
- [ ] godot 层 SCons 构建配置（.gdextension 各平台动态库）
- [ ] 数据转换层：godot::Image ↔ core Image；SpriteRect → Array[Rect2i]
- [ ] SpriteSplitter 类（GDScript 可调 `SpriteSplitter.split(image, options)`）
- [ ] EditorPlugin：预览 → 切分 → 导出 PNG / AtlasTexture / SpriteFrames
- [ ] Godot 4.6.2 本机加载验证（无脚本错误）

**M5 验收**：编辑器内一键切分，生成 `res://sprites/xxx_01.png` 或 atlas 资源。

---

## 进行中

（当前：M1–M3.5 全部完成，auto 已修复；M4/M5 搁置）
