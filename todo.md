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
- [x] `remove-background` 子命令：去背景整图透明导出（不切分；复用 BackgroundRemover 管线 + `used_remote` 实际后端上报；输出 `<stem>_transparent.png`；`--background-threshold < 0` 参数校验）

**M3.5 验收（已达成）**：`sprite-split info input.png --format json | jq '.components'` 链路可用；六命令 help 齐全、flag 校验正确。

## M4 — AI 分割（可选，后期）

> 方案细化见 `docs/ai-backend.md`（调研完成，决策点待确认：ORT 依赖获取方式 / 默认模型 / --bg-backend 默认值）

- [x] **Remote AI 后端（已落地，替代 ONNX 内嵌路线）**：`--bg-backend remote --bg-url URL` HTTP 调用
      `examples/rembg-api`（Python FastAPI + rembg）→ 透明 PNG；失败 warning + 回退纯算法
- [x] **BackgroundRemover 抽象接口（已落地）**：`core/segmentation/background_remover.hpp`（
      `virtual Mask process(const Image&)` + 注册表/工厂）；`Color` 后端内置 core，
      `Remote` 后端在 `extra/bg_remote`（`sps_bg_remote` 库，CLI main 入口注册）；
      统一 mask 语义 → remote 下 `--contract` 同样生效
- [x] **remote mask 接入切分（修复）**：`split_image(image, opts, bg_mask)` 接受外部背景 mask，
      `--bg-backend remote` 时 AI mask 替代内部 color 计算驱动 CCL/Grid（修复前只改导出 alpha，
      rect 与 color 完全一致，AI 对切分零影响）
- [ ] ONNX Runtime 集成 + 模型外置 `models/`（rembg/isnet/custom）
- [ ] 失败回退纯算法（remote 后端已实现，ONNX 路线待定）

## M5 — Godot GDExtension 插件

- [x] godot-cpp 引入（✅ 完成：**git submodule** `godot/godot-cpp`，**godot-4.5-stable** @ e83fd09（gitlink 指针，源码不入库，参考 limboai）；github.com git clone 本环境代理下不通（502/Empty reply）→ api.github.com tarball 解压 + `git update-index --cacheinfo 160000,<sha>` 建立指针；CMake 子目录方式构建）
- [x] godot 层构建配置（✅ 完成：`godot/CMakeLists.txt` 消费端模板 + core 源码直接编入动态库；**GODOTCPP_DISABLE_EXCEPTIONS=OFF 必须显式设**——godot-cpp 的 -fno-exceptions 是 PUBLIC 传播会覆盖消费者目标；macOS 产物带 `.arm64` 后缀；产物落盘 `project/addons/sprite_splitter/bin/<platform>/`）
- [x] 数据转换层（✅ 完成：`godot/src/conversion.cpp`：godot::Image ↔ sps::Image（RGBA8 深拷贝、非 RGBA8 重建副本后 convert 不污染调用方）、SpriteRect ↔ Rect2i）
- [x] SpriteSplitter 类（✅ 完成：RefCounted，GDScript 可调 `split/analyze/crop/export_sprite/split_and_export`，options 字典与 CLI 参数对齐；core 异常在边界 try/catch 不泄漏）
- [x] **addons 规范布局**（✅ 完成：`project/addons/sprite_splitter/`：plugin.cfg + editor_plugin.gd（@tool EditorPlugin，Tools 菜单 → 选 PNG → 自动切分导出）+ bin/（.gdextension + 动态库）；.gdextension 路径改 res://addons/...）
- [ ] EditorPlugin GUI 验证（editor_plugin.gd 骨架已写、结构规范，但 **4.6.2 编辑器模式 bug 阻塞 headless 验证**，需 GUI 编辑器实测：打开工程 → Plugins 启用 Sprite Splitter → Tools 菜单切分）
- [x] Godot 4.6.2 本机加载验证（✅ 完成：无头冒烟 24 断言全 PASS、退出码 0；断言值 = CLI golden；addons 布局下扩展正常加载）
- [x] AtlasTexture 测试（✅ 完成：main.gd 第 10/11 步——读 `sprites/meta.json`（80 区域，复制自 out_sprites/big/meta.json）在大图上直接建 80 个 AtlasTexture，**不切图**、零文件输出；校验 region/越界/重叠 + `get_image()` 与切图产物 sprite_01.png 像素级一致；前 3 个挂 Sprite2D 演示；第 11 步 ResourceSaver 落盘 3 个 `.tres`（atlas 用 load() 导入纹理引用）重载校验一致；无头冒烟全 PASS）
- [x] 插件 UI 独立场景（M5.1，✅ 完成：`addons/sprite_splitter/ui/sprite_splitter_ui.tscn` + `.gd` + `overlay.gd`，**不挂载编辑器**；选图/分析（自动填建议参数）/切分/预览叠加描边/导出三模式（切 PNG｜仅 meta.json｜AtlasTexture .tres）；`SPS_UI_TEST=1` headless 自动回归全 PASS（64 精灵：64 PNG + 64 tres + meta.json）；编码约定：节点路径 `/` 层级 + var 显式类型不用 `:=`；规划见 `docs/plugin-ui-plan.md`）
- [ ] M5.2 挂载编辑器（EditorPlugin dock/bottom panel 复用 UI 场景 + 手动框选 crop/export_sprite + 从已有 meta.json 导入 rects + sheet 打包导出需 GDExtension 补 API）

**M5 调研结论（2026-08-25，已入 SKILL.md §2.5/§7/§8）**：
- **GDExtension 加载机制**：运行时只读 `res://.godot/extension_list.cfg`（每行一个 .gdextension 路径，由编辑器全项目扫描生成，含 addons/）；不扫描 res://。删 .godot 后直接运行扩展不加载 → 必须先编辑器导入或手动写列表
- **4.6.2 编辑器模式崩溃**：带 GDExtension 的项目 `--import`/`-e --quit` 退出时 `EditorHelp::_gen_extensions_docs` 段错误（DocTools::generate 遍历扩展类时 Main::cleanup 已析构）。与 reloadable 无关（--doctool 正常、无扩展对照正常）。**规避：无扩展两步法导入**（临时移走 .gdextension → --import → 恢复 + 手动写 extension_list.cfg）
- EditorPlugin 规范：plugin.cfg（[plugin] name/description/author/version/script）+ `@tool extends EditorPlugin` 脚本；默认未启用，Plugins 列表勾选；插件内 GDScript 都要 @tool

**M5 实测坑（已补入 SKILL.md §4/§5.1/§7/§8）**：
- godot-cpp `GODOTCPP_DISABLE_EXCEPTIONS` 默认 ON 且以 `PUBLIC` 传播 `-fno-exceptions` → 消费 core（用异常）必须配置 `-DGODOTCPP_DISABLE_EXCEPTIONS=OFF`
- macOS 产物名带架构：`libxxx.macos.template_debug.arm64.dylib`，`.gdextension` 路径须含 `.arm64`
- 4.6.2 编辑器模式崩溃（EditorHelp 扩展文档生成 bug）→ 两步法导入规避（见上）

---

## M4b — CLI 解耦重构（已落地）

- [x] **CLI11 开源解析库**：`third_party/cli11/CLI/CLI.hpp`（v2.7.2，vendored）；替换手写 flag_table/parse_args/帮助文本，自动 help/类型校验（Range/IsMember/PositiveNumber）
- [x] **split 与 remove-background 分离**：split/sheet/manual/from-json 删除全部背景 flag；remove-background 独立（含全部背景 flag）
- [x] **删除 --contract**（core SplitOptions/splitter + CLI + 4 个测试用例）
- [x] **--stdout 真管道**：remove-background 输出 PNG 二进制到 stdout（与 --format json 互斥）；全命令 input 支持 `-`（stdin 读 PNG）
- [x] 两管道（JSON 桥接 / --stdout）产物**逐字节一致**（golden 实测）
- [x] 单测全绿（92 用例）；SKILL.md 验证方法改为管道/JSON 断言（全部实测通过）

**M4b 验收（已达成）**：`remove-background sheet.png --stdout | sprite-split split - --mode grid --cell-size 8` 链路可用；
旧工作流（split --remove-background）语义等价拆解为两命令（同一算法路径，grid8 均 5 组件）。
设计文档：`docs/refactoring-guide.md`。

## 进行中

（当前：M1–M4b 全部完成；M5 核心链路 + addons 规范布局完成，EditorPlugin 骨架待 GUI 验证；M4 ONNX 内嵌搁置）
