---
name: sprite-splitter
description: Sprite Splitter CLI 使用规范（切雪碧图 / 精灵提取 / 图片分析）。需要切分 Sprite Sheet、提取精灵、去背景、手动框选、从 meta.json 重切、重排 sprite sheet 时使用。涵盖子命令、完整参数、推荐工作流、常见陷阱与验证方法。
---

# Sprite Splitter CLI 使用指南

项目工具：`sprite-split`（C++20，构建产物 `build/sprite-split`）。用于从 Sprite Sheet 中检测/切分精灵，导出 PNG + JSON 元数据。

**子命令模式**：`sprite-split <command> [args]`，命令间互斥、共享 flag 白名单校验。

| 子命令 | 作用 |
|---|---|
| `info <input>` | 分析图片 + 推荐参数（不切分） |
| `split <input> [flags]` | 自动检测 + 切分导出（components/grid/auto） |
| `manual <input> [flags]` | 交互画框 + 切分导出（始终写 meta.json） |
| `from-json <input> <meta.json>` | 从 meta.json 加载 rects 直接切图 |
| `sheet <input> --cols N [flags]` | 重排为规整 sprite sheet（支持 `--from-json`） |
| `remove-background <input> [flags]` | 去背景，**整图**导出透明 PNG（不切分） |

## 快速开始

```bash
# 构建（改动 C++ 代码后）
cmake --build build -j

# 0) 先分析图片，获取推荐参数（不切分）
build/sprite-split info input.png

# 1) 按推荐参数切分（透明背景素材）
build/sprite-split split input.png --output sprites --json

# 2) 白底无透明通道素材：加 --remove-background（背景变透明）
build/sprite-split split sheet.png --remove-background --output sprites --json

# 3) 网格表：--mode grid --cell-size N（8 的倍数常用）
build/sprite-split split sheet.png --remove-background --mode grid --cell-size 8 --output sprites

# 4) 角色被间隙拆开：--merge-distance N 合并
build/sprite-split split char.png --merge-distance 3 --output sprites

# 5) 手动画框 / 从 meta.json 重切
build/sprite-split manual input.png --output sprites
build/sprite-split from-json input.png sprites/meta.json --output sprites

# 6) 重排 sprite sheet（from-json 或自动检测）
build/sprite-split sheet input.png --cols 8 --from-json sprites/meta.json --output sheet
build/sprite-split sheet grid.png --cols 8 --mode grid --cell-size 8 --output sheet

# 7) 机器可读输出（管道友好）：--format json，stdout 只含结果对象，进度/日志走 stderr
build/sprite-split info input.png --format json | jq '.recommended'
build/sprite-split split input.png --output sprites --format json | jq '.count'

# 8) 只需一张透明图（不切分）：整图去背景，输出 <stem>_transparent.png
build/sprite-split remove-background photo.png --output sprites
build/sprite-split remove-background photo.png --bg-backend remote --format json | jq '.output'
```

## 完整参数

### split（检测 + 导出，sheet 检测路径共用同一组）

| 参数 | 默认 | 说明 |
|---|---|---|
| `--output DIR` | `./sprites` | 输出目录（自动创建） |
| `--alpha-threshold N` | `1` | alpha > N 视为前景（透明素材） |
| `--min-width/--min-height N` | `1` | 过滤小于该尺寸的分量（滤噪点） |
| `--remove-background` | 关 | 环带采样 + flood fill 去背景，**导出透明 PNG** |
| `--background-threshold N` | `12` | 背景色距离阈值下限（RGB 曼哈顿距离）。背景自身有压缩/渐变噪声时，有效阈值会自动放大到 `max(N, 噪声自适应值)` |
| `--bg-color R,G,B` | 自动 | 手动指定背景色（环带采样失效时用，与 threshold 构成颜色区间） |
| `--bg-backend MODE` | color | 背景清理后端：`color`（纯算法，默认）\ `remote`（HTTP 调用远程 AI 服务，如 `examples/rembg-api`） |
| `--bg-url URL` | `http://127.0.0.1:8000` | remote 后端服务 base URL；服务不可达 → `warning:` + 自动回退 color（零回归兕底） |
| `--contract N` | `0` | **自由选区收缩**（须与 `--remove-background` 同用，仅 components 系模式）：对前景轮廓向内腐蚀 N 圈后重算包围盒，剪切清理背景产生的边缘毛边（halo）。只切轮廓、不切贴边内容 |
| `--mode MODE` | components | `components`（连通分量）/ `grid`（网格）/ `auto` |
| `--cell-size N` | `16` | grid/auto 的格子尺寸 |
| `--merge-distance N` | `0` | 膨胀合并间距（components 模式） |
| `--json` | 关 | 写 meta.json（含每张 sprite 的 rect） |
| `--json-only` | 关 | **只导出 meta.json 不切 PNG**；无 `--output` 时 JSON 直出 stdout（纯净，供 UI 捕获） |
| `--gen-masks` | 关 | 为每个 sprite 生成全白 mask PNG（`masks/`）+ meta.json 写 `mask` 字段（UI 橡皮擦起点） |
| `--erase-tl WxH` | — | 与 `--gen-masks` 配合：每个 mask 左上角擦除 WxH（黑=擦除），随后直接切图 |

### manual / from-json（仅输出与背景清理）

| 参数 | 说明 |
|---|---|
| `--output DIR` | 输出目录（默认 `./sprites`） |
| `--remove-background` / `--background-threshold N` / `--bg-color R,G,B` | 透明导出（与 split 相同） |

`manual` 从 **stdin** 读框（`x y width height` 每行一个，空行或 `q` 结束），始终写 meta.json。

### sheet

| 参数 | 说明 |
|---|---|
| `--cols N` | **必填**：每行精灵数 |
| `--from-json FILE` | 从 meta.json 加载 rects（应用 mask）；缺省则用下方检测 flag 自动检测 |
| 检测 flag | 同 split（`--mode/--cell-size/--remove-background/...`） |

输出 `sheet.png` + `sheet_meta.json`（每张精灵 src/dst 坐标）。

### remove-background（去背景，整图透明导出，不切分）

| 参数 | 默认 | 说明 |
|---|---|---|
| `--output DIR` | `./sprites` | 输出目录（自动创建），文件名 `<stem>_transparent.png` |
| `--background-threshold N` | `12` | 背景色距离阈值下限（同 split） |
| `--edge-clean N` | `3` | 边缘过渡色清扫圈数（同 split） |
| `--bg-color R,G,B` | 自动 | 手动指定背景色 |
| `--bg-backend MODE` | color | `color` / `remote`（同 split） |
| `--bg-url URL` | `http://127.0.0.1:8000` | remote 后端 base URL；不可达 → warning + 回退 color |

输出**保持原图尺寸**，仅背景像素 alpha 置 0。**remote 后端成功时直接采用服务端透明图**（保留 AI 软边 / alpha matting 的渐变 alpha，与 API 直连质量一致）；color 后端与回退路径走二值 mask 透明化（硬边）。JSON 结果含 `background_pixels` / `background_percent` / `bg_backend` / `bg_color`。

### 通用

| 参数 | 说明 |
|---|---|
| `--format json\|text` | 默认 text。json 模式：stdout 只输出单个结果对象（`status/count/sprites/...`），进度与警告走 stderr，**管道安全**（`| jq` 直接用） |
| `-q, --quiet` | text 模式只输出汇总 |
| `--version` / `--help` | 版本 / 帮助（`sprite-split <cmd> --help` 看命令专属帮助） |

## 推荐工作流

```
1. info 分析 → 看推荐参数（--format json 可脚本化）
2. split 按推荐参数自动切分 → 检查输出
3. 不满意 → 手动微调：改 meta.json 或用 manual 重画框
4. from-json 按手工 rect 精确重切
5. 需要整图打包 → sheet --cols N --from-json sprites/meta.json
```

## 常见陷阱

- **无透明通道素材**（全不透明 PNG）：默认 alpha 分割会把整图当 1 个 sprite。必须 `--remove-background`（或先 `info` 确认）
- **背景色估错**（四角/边缘被内容占满）：自动环带采样会失效 → 手动 `--bg-color R,G,B` 指定背景色，配合 `--background-threshold` 形成颜色区间
- **纯色背景但边缘有残色边（halo）**：JPEG 压缩/抗锯齿会让物体边缘产生一圈「接近背景的过渡色」。算法已内置自适应阈值 + 边缘过渡清扫自动清除；若仍残留，可适当提高 `--background-threshold`（它是下限，不会缩小自动容差）
- **精灵边缘有杂边**（清理背景后的白边/色边 halo）：`--contract N`（须配 `--remove-background`）按自由选区收缩模式向内腐蚀 N 圈剪切毛边；若毛边是接近背景的过渡色，优先用 `--edge-clean`（零开销）
- **半透明软边被拍平**（remote 去背景后边缘硬边、半透明=0）：`remove-background` 命令的 remote 路径**已修复**——直接采用服务端透明图保留软边 alpha（`BackgroundRemover::process_transparent`）；`split`/`sheet` 仍需二值 mask 做 CCL，软边像素按前景保留原 alpha（有意的语义差异）
- **噪点多**：`info` 看 component_count 与中位数面积；大量小分量 → 用 `--min-width/--min-height` 过滤（推荐值为最大精灵的 1/4）
- **`--mode auto`**：假设→打分→验证→回退。行列投影 + Pearson 自相关找候选周期 → offset 搜索对齐组件中心 → 多维评分（周期/对齐/边界/尺寸/占用）→ 谐波抑制；置信度 <0.65 或周期性 <0.25 或组件 <4 时自动**回退 components**
- **`--merge-distance` 仅 components 模式有效**，与 grid/auto 混用会报错
- **手动框越界**：`manual` 会提示跳过；`from-json` 会自动 clamp 到图像范围
- **flag 白名单**：把 split 专属 flag（如 `--json`）用在 info 上会报 "not valid for this command"
- 切完建议抽查输出 PNG 四角 alpha（`--remove-background` 时应为 0）与内容覆盖率

## 验证方法

> 原则：**能用 `--format json` 断言就不要肉眼看输出**。以下所有验证均可一键复制执行，
> 断言失败时命令退出非零（jq -e），可直接接进 CI / 脚本回归。进度日志走 stderr，不影响断言。

### 1. 单元测试（core 算法层）

```bash
ctest --test-dir build          # 全绿：88 用例 / 347 断言
```

### 2. 端到端素材

- `tests/fixtures/test_sheet.png`：alpha 表（默认参数下 5 个组件：3 精灵 + 半透明组件 + 噪点组件，min-size 默认 1 不过滤）
- `tests/fixtures/grid8_sheet.png`：白底 8×8 网格（64x64）
- `tests/fixtures/merge_sheet.png`：间隙合并

### 3. 端到端验证（管道 / JSON 断言，可脚本化）

```bash
SPLIT=build/sprite-split; FIX=tests/fixtures

# 3.1 info 分析 + 推荐参数
$SPLIT info $FIX/grid8_sheet.png --format json | jq -e '.status == "ok" and .width == 64 and .height == 64 and .has_transparency == false'

# 3.2 remove-background 整图透明导出（JSON 字段：output/bg_backend/background_percent）
$SPLIT remove-background $FIX/grid8_sheet.png --output /tmp/sps_check --format json | \
  jq -e '.status == "ok" and .bg_backend == "color" and .background_percent > 90'

# 3.3 split：未去背景原图 grid 检测 → 64 个格子（仅此路径预期 64）
$SPLIT split $FIX/grid8_sheet.png --mode grid --cell-size 8 --output /tmp/sps_check --format json | jq -e '.count == 64'

# 3.4 推荐管道：先 remove-background 再 split（去背景后 grid 按 alpha 统计 → 5 个前景组件）
out=$($SPLIT remove-background $FIX/grid8_sheet.png --output /tmp/sps_check --format json | jq -r '.output')
$SPLIT split "$out" --mode grid --cell-size 8 --output /tmp/sps_check/s --format json | jq -e '.count == 5'

# 3.5 sheet 重排（128x128 = 8 列 × 8 行 × 16px cell）
$SPLIT sheet $FIX/grid8_sheet.png --cols 8 --mode grid --cell-size 8 --output /tmp/sps_check/sheet --format json | \
  jq -e '.status == "ok" and .width == 128 and .height == 128 and .count == 64'

# 3.6 json-only 直出 stdout（无 --output，meta 纯净可捕获；test_sheet 默认参数 5 个组件）
$SPLIT split $FIX/test_sheet.png --json-only --format json | jq -e '.json_only == true and .count == 5'
```

> ⚠️ **grid 计数歧义**：`grid8_sheet.png` 是白底不透明图 —— 未去背景直接 grid 检测得 **64**
> （全图非透明 → 检出 8×8 格子）；先去背景再 split 得 **5**（只保留 5 个前景物体的非透明区域）。
> 两种预期都对，取决于是否经过 `remove-background` 阶段；断言时务必与目标路径一致。
> （重构指南见 `docs/refactoring-guide.md`：split 与 remover 解耦后该歧义自然消除。）

### 4. 一键 E2E 脚本

```bash
# 全部断言失败即退出非零；可用于本地回归 / CI
set -e
SPLIT=build/sprite-split; FIX=tests/fixtures; TMP=/tmp/sps_e2e; rm -rf $TMP; mkdir -p $TMP
$SPLIT info $FIX/grid8_sheet.png --format json | jq -e '.components >= 1' >/dev/null
$SPLIT remove-background $FIX/grid8_sheet.png --output $TMP --format json | jq -e '.background_percent > 90' >/dev/null
out=$($SPLIT remove-background $FIX/merge_sheet.png --output $TMP --format json | jq -r '.output')
$SPLIT split "$out" --merge-distance 3 --output $TMP/s --format json | jq -e '.count == 1' >/dev/null
echo "E2E OK"
```
