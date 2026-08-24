# Sprite Splitter — 项目审查与重构指南

> 状态：审查完成，重构建议稿（未实施）。决策点见 §6，需用户确认后按 §5 分阶段执行。
> 范围：CLI 架构重构（开源解析库 + split/remover 解耦 + 测试自动化）。core 算法层不改语义。

---

## 1. 审查报告

### 1.1 架构现状

```
sprite-split (cli/main.cpp, 1322 行)
├── core/sps_core        纯算法静态库（零网络依赖，零 stdio）
│   ├── image / mask / segmentation / model / analyzer / export
│   └── BackgroundRemover 抽象 + Color 后端 + 注册表/工厂
├── extra/sps_bg_remote  Remote 后端（httplib，网络依赖收敛在此，main 入口注册）
├── examples/rembg-api   Python FastAPI + rembg 独立服务
└── tests/               Catch2 单元测试（88 用例 / 347 断言，全绿）
```

### 1.2 优点（保持不动）

| 项 | 说明 |
|---|---|
| 分层正确 | core（零依赖）→ extra（网络）→ cli，依赖方向单向；`BackgroundRemover` 抽象让网络实现收敛在 extra，core 可独立测试 |
| 可插拔后端 | **Strategy + Factory + Registry** 模式落地完整：Color 内置注册，Remote 由 `sps_bg_remote::register_backend()` 注入，新增后端 core 零改动 |
| 管道友好输出 | `--format json`：stdout 只含结果对象、进度/警告走 stderr，已实测可 `\| jq` |
| 测试覆盖 | 88 用例 / 347 断言，分模块（image/mask/components/grid/background/...），含回退、自适应阈值等边界 |
| 依赖策略 | third_party vendored（stb / catch2 / nlohmann / httplib），构建零网络，与本机网络实测一致 |

### 1.3 问题点（重构对象）

**P1 — cli/main.cpp 单文件巨兽（1322 行）**：参数解析、帮助文本、输出抽象、业务逻辑、6 个子命令全部耦合在一个匿名 namespace。
新增一个 flag 需要改 **6 处**：`flag_table()` 注册 + `apply_*` 函数 + 命令的 `allowed` 白名单 + 帮助文本常量 + `validate_split_opts` +（可能）文档。跨命令 flag（如 `--edge-clean` 在 5 个命令重复声明）靠手工复制，易漂移。

**P2 — 手写参数解析样板**：`flag_table`（22 项）+ `parse_args` + 每命令 `allowed` set + `parse_int`/`sscanf` 手工校验。agent.md §2.2 早已标注 "CLI11 备选"，M1 时参数少选择手写，现在 22 个 flag + 6 命令已超出手写维护的舒适区。

**P3 — split 与背景移除职责纠缠**：`split` 内嵌 8 个背景相关 flag（`--remove-background/--background-threshold/--edge-clean/--bg-color/--contract/--bg-backend/--bg-url`），参数面膨胀；`apply_background_cleanup` 被 4 个命令复用，并携带 **keep_alpha（软边）与二值 mask（硬边）双语义**。
SKILL.md 自己也标注了有意的语义差异："`remove-background` 命令的 remote 路径保留软边 alpha；`split`/`sheet` 仍需二值 mask 做 CCL"。**这个差异正是耦合的代价**——一旦解耦（先去背景、后切分），差异自然消失。

**P4 — 缺少 CLI 端到端测试**：88 用例全部是 core 单元测试；CLI 验证依赖 SKILL.md 里的手工 shell 命令，不可自动回归。`--format json` 已经为自动化铺好了路，但没有脚本化。

**P5 — help/校验与解析逻辑双维护**：6 份帮助文本常量（`kSplitHelp` 等）+ 解析逻辑各自维护，`split --help` 与 `sheet --help` 的检测段几乎重复。

**P6 — 文档验证预期与真实行为存在歧义（实测）**：SKILL.md 称 `split grid8_sheet.png --mode grid --cell-size 8` 应得 64 个 sprite。实测：

```
原图直接 split --mode grid              → 64（白底不透明，全图非透明 → 检出 8×8 格子）
去背景后 split --mode grid（新旧一致）   → 5（只保留 5 个前景物体的非透明区域）
旧工作流 split --remove-background --mode grid → 5（与新管道 golden 一致 ✅）
```

即 "64" 只对**未去背景**路径成立；去背景后 grid 检测基于 alpha 统计，结果为前景组件数。
两种语义各自自洽，但文档未区分，易误导验证。解耦后 `split` 恒处理透明图，此歧义自然消除。
这也印证解耦的价值：grid 检测的输入语义（alpha vs 全图不透明）被 --remove-background 搞混了。

### 1.4 设计模式现状盘点

| 模式 | 现状 | 评价 |
|---|---|---|
| Strategy | BackgroundRemover 后端（Color/Remote） | ✅ 已用，好 |
| Factory + Registry | `register_background_remover` / `create_background_remover` | ✅ 已用，好（extra 注入，core 零依赖） |
| Template Method | `process_transparent` 默认实现（二值 mask 回放），Remote override | ✅ 已用，好 |
| Command | main() 里 `if-else` 分派 | ⚠️ 手写，语义弱（无独立类/回调） |
| Builder | 22 个 `apply_*` 手工赋值 CliOpts | ⚠️ 手写，样板多 |
| Pipeline | 无（背景清理在 split 内部完成） | ❌ 待引入（remover → split 阶段组合） |

---

## 2. 重构目标架构

```
sprite-split（CLI11 子命令，每个命令一个独立 .cpp）
│
├── remove-background <input>       只做背景移除 → 整图透明 PNG
│      （保留 Remote AI 软边 alpha；含 --stdout 可选，管道友好）
│
├── split <input>                   只做检测 + 切分（输入=透明图，无背景 flag）
│      （alpha 切分 / --mode / --merge-distance / --contract(alpha 收缩) / 导出）
│
├── info <input>                    分析 + 推荐参数（可选背景分析 flag）
├── manual <input>                  手动框选
├── from-json <input> <meta.json>   按 meta 重切
└── sheet <input> --cols N          重排 sprite sheet

推荐工作流变为两个命令的管道：
  remove-background sheet.png --output tmp --format json
  → split tmp/sheet_transparent.png --mode grid --cell-size 8 --output sprites --format json
```

### 2.1 职责边界

| 命令 | 职责 | 保留 flag | 移除 flag |
|---|---|---|---|
| `remove-background` | 背景移除、整图透明导出 | `--output/--background-threshold/--edge-clean/--bg-color/--bg-backend/--bg-url/--format/-q` | — |
| `split` | 透明图 alpha 检测 + 切分导出 | `--mode/--alpha-threshold/--min-width/--min-height/--merge-distance/--cell-size/--contract/--output/--json/--json-only/--gen-masks/--erase-tl` | **`--remove-background/--background-threshold/--edge-clean/--bg-color/--bg-backend/--bg-url`** |
| `info` | 分析 + 推荐 | `--format/-q` | 背景分析 flag 收敛为内部行为（见 §3.3） |
| `manual/from-json/sheet` | 框选/重切/重排 | 不变 | 同 split 移除背景 flag 家族 |

**语义收益**：解耦后 `split` 输入恒为透明图 → 切分统一走 alpha 判定，Remote AI 软边 alpha 直接参与 CCL（`--alpha-threshold` 生效），**不再需要二值 mask 桥接**，SKILL.md 标注的"有意的语义差异"消失。

---

## 3. 分阶段实施计划

### Phase 1 — CLI 引入开源解析库（推荐 CLI11）

**选型对比**：

| 库 | 子命令支持 | 自动 help | 类型/范围校验 | 单头可 vendored | 维护活跃 |
|---|---|---|---|---|---|
| **CLI11** | ✅ 一等公民（`add_subcommand`） | ✅ | ✅（`CLI::Range/IsMember/PositiveNumber`） | ✅（CLI11.hpp 单头 ~500KB） | ✅ MIT |
| cxxopts | ⚠️ 仅 `positional`，子命令弱 | ⚠️ 半自动 | 弱 | ✅（更小） | 一般 |
| lyra | ⚠️ 组合式，子命令需手拼 | 弱 | 一般 | ✅ | 一般 |

**结论：CLI11 v2.4.x**（子命令支持最完善，正是本项目需要的形态）。下载走 `api.github.com/repos/CLIUtils/CLI11/releases`（本机实测 200 可达），vendored 到 `third_party/cli11/CLI11.hpp`，遵循现有零网络构建策略。

**落地形态**（替换 P1/P2/P5 全部样板）：

```cpp
// cli/main.cpp（瘦身到 ~100 行：组装 + 分派）
CLI::App app{"sprite-split"};
app.set_version_flag("--version", kVersion);

auto* rb = app.add_subcommand("remove-background", "remove bg, export transparent PNG");
// ... 每命令一个文件：cli/commands/remove_background.cpp 等（Command 模式）
auto* sp = app.add_subcommand("split", "detect + split transparent image");
sp->add_option("input", input)->required()->check(CLI::ExistingFile);
sp->add_option("--mode", mode)->check(CLI::IsMember({"components", "grid", "auto"}));
sp->add_option("--cell-size", cell)->check(CLI::Range(1, 4096));
// CLI11 自动：help/--version/类型校验/required/子命令互斥
app.require_subcommand(1);
app.parse(argc, argv);
// 分派：map<命令, 处理器>（Command 模式）
```

**要点**：
- 每个子命令一个 `.cpp`（`cli/commands/`），公共参数入 `cli/options.hpp`（`OutputOpts`：`--format/-q/--output`）
- `Out`（json/text 输出抽象）抽到 `cli/output.hpp` 保留——这是管道友好的核心，不动
- 校验逻辑（现 `validate_split_opts`）用 CLI11 `check()` 表达，删除手工白名单
- 行为兼容：`--help`/`--version`/退出码 0/1/2 语义保持不变

**验证**：`split --help` 与 `sheet --help` 自动生成且检测段一致（单一来源）；六命令 flag 校验回归；`ctest` 全绿。

### Phase 2 — split 与 remove-background 解耦

**2.1 命令面调整**（§2.1 表格）：split 删除背景 flag 家族；remove-background 保留；`--contract` 语义改为 **"对 alpha 前景轮廓向内腐蚀 N 圈后重算 bbox"**（输入已是透明图，用 alpha 生成 mask，不依赖背景移除）。`edge-clean` 归 remove-background（它清理的是背景过渡色，与背景移除同生命周期）。

**2.2 管道组合**（推荐先落地 JSON 桥接，零协议改动）：

```bash
# 方式 A：JSON 桥接（本阶段即可用，推荐）
build/sprite-split remove-background sheet.png --output tmp --format json
build/sprite-split split tmp/sheet_transparent.png --mode grid --cell-size 8 --output sprites --format json

# 一行式
out=$(build/sprite-split remove-background sheet.png --output tmp --format json | jq -r '.output')
build/sprite-split split "$out" --mode grid --cell-size 8 --output sprites
```

**2.3 （可选增强）真 stdout 管道**：remove-background 加 `--stdout`（PNG 二进制写 stdout，日志全走 stderr）；split 支持从 `-`（stdin）读图（stb_image 已支持内存解码，`Image::load_png` 增加 `load_png_stream`）。此增强引入二进制流协议，测试略复杂，**建议 Phase 3 之后评估**。

**2.4 兼容策略**：`--remove-background` 作为 **deprecated 隐藏 flag** 保留一个版本周期（内部走两步：先 remove-background 再 split），帮助文本标注弃用；下一个破坏性版本移除。避免一次性破坏现有 skill/脚本。

**验证**：旧工作流（`split x.png --remove-background`）与新的两命令管道产出**逐字节一致**（golden 对比；已实测 grid8_sheet 新旧均为 5，成立）；remote 后端软边 alpha 在 split 中按 alpha 切分（新增 E2E 断言）。
**同时修正 P6 文档歧义**：SKILL.md "64 个" 标注为未去背景路径的预期，去背景路径预期为前景组件数。

### Phase 3 — Skill 更新 + CLI 端到端测试（管道/JSON 断言）

**3.1 SKILL.md 验证方法改为管道/JSON 断言**（本指南配套改动，见 §4）。

**3.2 新增 CLI E2E 测试**（`tests/e2e/`，bash + jq，CTest 注册）：

```bash
# tests/e2e/pipeline_test.sh 片段
out=$($SPLIT remove-background "$FIX/grid8_sheet.png" --output "$TMP" --format json)
jq -e '.status == "ok" and .bg_backend == "color" and .background_percent > 90' <<<"$out"
# → 失败即退出非零，CTest 捕获

$SPLIT split "$TMP/grid8_sheet_transparent.png" --mode grid --cell-size 8 \
       --output "$TMP/s" --format json | jq -e '.count == 64'
```

覆盖：info 推荐、remove-background 输出字段、split count、grid/merge、remote 回退（mock 服务或直接测 color）、两命令管道一致性（golden 对比）。

**3.3 info 命令收敛**：背景分析 flag 从用户面移除，`info` 内部总是做背景估计（分析语义与背景移除解耦，但保留在 info 的输出字段中），减少一个命令的参数面。

### Phase 4（可选）— Command 模式整理

子命令类化 + `std::map<std::string, CommandHandler>` 分派，`main` 只做组装。若 Phase 1 用 CLI11 的 `add_subcommand` 回调，此阶段天然完成，无需单独做。

---

## 4. 设计模式总览（重构后）

| 模式 | 位置 | 说明 |
|---|---|---|
| **Strategy** | `BackgroundRemover` 后端 | 保留（已有） |
| **Factory + Registry** | `register_background_remover` | 保留（已有） |
| **Template Method** | `process_transparent` 默认实现 | 保留（已有） |
| **Command** | 每个子命令一个处理器（CLI11 `add_subcommand` + 回调） | 引入（替代 if-else 分派） |
| **Builder** | CLI11 参数声明 → `SplitOptions` | 引入（替代 22 个 `apply_*`） |
| **Pipeline** | `remove-background \| split` 阶段组合 | 引入（背景移除与切分解耦，JSON 桥接） |
| **Facade** | core 提供 `remove_background(image, opts) -> Image` 单函数 | 引入（CLI 不再手拼 `apply_background_cleanup` 流程） |

**架构约束**：背景移除的算法实现（color flood fill / remote HTTP）永远留在 core/extra，CLI 层只做参数透传与输出；Pipeline 组合在 CLI 层（命令级别），core 不引入命令编排依赖。

---

## 5. 实施顺序与验证标准

| 阶段 | 交付 | 验证 |
|---|---|---|
| 1. CLI11 | `third_party/cli11/CLI11.hpp` + cli 重构 | 六命令 help/校验回归；88 用例全绿；`--format json` 输出不变 |
| 2. 解耦 | split 去背景 flag + 两命令管道 | 新旧工作流 golden 逐字节一致；remote 软边参与切分 |
| 3. 测试自动化 | `tests/e2e/` + SKILL.md 更新 | `ctest` 含 E2E 全绿；skill 验证命令可一键复制执行 |
| 4. 清理 | deprecated flag 移除、文档同步 | README/agent.md/todo.md 与命令面一致 |

每个阶段独立可回滚（阶段 2 的 golden 对比是回滚判据）。

---

## 6. 决策点（需用户确认后执行）

1. **解析库选型**：CLI11（推荐）vs cxxopts？
2. **破坏性变更策略**：`--remove-background` 保留 deprecated 隐藏 flag 过渡（推荐）vs 直接移除？
3. **管道形态**：先做 JSON 桥接（推荐，零协议改动）vs 同时做 `--stdout` 二进制管道？
4. **contract 归属**：留 split（对 alpha 轮廓收缩，推荐）vs 移入 remove-background？
5. **本次范围**：只出指南（当前交付）vs 按 §5 开始执行 Phase 1？
