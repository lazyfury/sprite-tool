---
name: sprite-splitter
description: Sprite Splitter CLI 使用规范（切雪碧图 / 精灵提取 / 图片分析）。需要切分 Sprite Sheet、提取精灵、去背景、手动框选、从 meta.json 重切、重排 sprite sheet 时使用。涵盖子命令、完整参数、推荐工作流、常见陷阱与验证方法。
---

> 💡 **AI 代理获取本规范**：直接运行 `sprite-split --prompt` 可输出本文档全文
> （构建时嵌入，与 SKILL.md 同步；无需知道文件路径）。

# Sprite Splitter CLI 使用指南

项目工具：`sprite-split`（C++20，CLI11 子命令，构建产物 `build/sprite-split`）。用于从 Sprite Sheet 中检测/切分精灵，导出 PNG + JSON 元数据。

**命令解耦原则**：背景移除与切分是**两个独立命令**——`remove-background` 只做背景移除（输出整图透明 PNG），`split`/`sheet` 只接受透明图做 alpha 切分。通过管道/JSON 桥接组合。

**子命令模式**：`sprite-split <command> [args]`，命令间互斥、共享 flag 白名单校验。

| 子命令 | 作用 |
|---|---|
| `info <input>` | 分析图片 + 两步推荐（不切分） |
| `remove-background <input> [flags]` | 去背景，**整图**导出透明 PNG（`--stdout` 可直出管道） |
| `split <input> [flags]` | 透明图检测 + 切分导出（components/grid/auto） |
| `manual <input> [flags]` | 交互画框 + 切分导出（始终写 meta.json） |
| `from-json <input> <meta.json>` | 从 meta.json 加载 rects 直接切图 |
| `sheet <input> --cols N [flags]` | 重排为规整 sprite sheet（支持 `--from-json`） |

> 所有命令 `input` 支持 `-`（从 stdin 读 PNG）。`manual` 除外（其框输入也走 stdin）。

## 快速开始

```bash
# 构建（改动 C++ 代码后）
cmake --build build -j

# 0) 先分析图片，获取推荐参数（不切分；输出两步工作流建议）
build/sprite-split info input.png

# 1) 透明背景素材：直接切分
build/sprite-split split input.png --output out/sprites --json

# 2) 白底无透明通道素材：先去背景（透明 PNG），再切分 —— 两命令
build/sprite-split remove-background sheet.png --output tmp
build/sprite-split split tmp/sheet_transparent.png --output out/sprites --json

# 2.5) 一行式真管道（--stdout 输出 PNG 二进制，split 从 stdin '-' 读）
build/sprite-split remove-background sheet.png --stdout | \
  build/sprite-split split - --output out/sprites --json

# 3) 网格表：--mode grid --cell-size N（8 的倍数常用）
build/sprite-split remove-background grid.png --stdout | \
  build/sprite-split split - --mode grid --cell-size 8 --output out/sprites

# 4) 角色被间隙拆开：--merge-distance N 合并
build/sprite-split remove-background char.png --stdout | \
  build/sprite-split split - --merge-distance 3 --output out/sprites

# 5) 手动画框 / 从 meta.json 重切
build/sprite-split manual input.png --output out/sprites
build/sprite-split from-json input.png out/sprites/meta.json --output out/sprites

# 6) 重排 sprite sheet（from-json 或自动检测；输入透明图）
build/sprite-split sheet input.png --cols 8 --from-json out/sprites/meta.json --output sheet
build/sprite-split sheet input.png --cols 8 --mode grid --cell-size 8 --output sheet

# 7) 机器可读输出（管道友好）：--format json，stdout 只含结果对象，进度/日志走 stderr
build/sprite-split info input.png --format json | jq '.recommended'
build/sprite-split split input.png --output out/sprites --format json | jq '.count'

# 8) 只需一张透明图（不切分）：整图去背景，输出 <stem>_transparent.png
build/sprite-split remove-background photo.png --output out/sprites
build/sprite-split remove-background photo.png --bg-backend remote --format json | jq '.output'
```

## 完整参数

### remove-background（去背景，整图透明导出，不切分）

| 参数 | 默认 | 说明 |
|---|---|---|
| `--output DIR` | `./out/sprites` | 输出目录（自动创建），文件名 `<stem>_transparent.png` |
| `--background-threshold N` | `12` | 背景色距离阈值下限（RGB 曼哈顿距离）。背景自身有压缩/渐变噪声时，有效阈值会自动放大到 `max(N, 噪声自适应值)` |
| `--edge-clean N` | `3` | 边缘过渡色清扫圈数（物体边缘压缩/AA 残色；0 = 关） |
| `--bg-color R,G,B` | 自动 | 手动指定背景色（环带采样失效时用，与 threshold 构成颜色区间） |
| `--bg-backend MODE` | color | 背景清理后端：`color`（纯算法，默认）\ `remote`（HTTP 调用远程 AI 服务，如 `examples/rembg-api`） |
| `--bg-url URL` | `http://127.0.0.1:8000` | remote 后端服务 base URL；服务不可达 → `warning:` + 自动回退 color（零回归兜底） |
| `--stdout` | 关 | **PNG 二进制直出 stdout**（真管道，与 `--format json` 互斥）；进度/日志走 stderr |

输出**保持原图尺寸**，仅背景像素 alpha 置 0。**remote 后端成功时直接采用服务端透明图**（保留 AI 软边 / alpha matting 的渐变 alpha）；color 后端与回退路径走二值 mask 透明化（硬边）。JSON 结果含 `output` / `background_pixels` / `background_percent` / `bg_backend` / `bg_color`。

### split（检测 + 导出，sheet 检测路径共用同一组；输入须为透明图）

| 参数 | 默认 | 说明 |
|---|---|---|
| `--output DIR` | `./out/sprites` | 输出目录（自动创建） |
| `--alpha-threshold N` | `1` | alpha > N 视为前景 |
| `--min-width/--min-height N` | `1` | 过滤小于该尺寸的分量（滤噪点） |
| `--mode MODE` | components | `components`（连通分量）/ `grid`（网格）/ `auto` |
| `--cell-size N` | `16` | grid/auto 的格子尺寸 |
| `--merge-distance N` | `0` | 膨胀合并间距（components 模式） |
| `--json` | 关 | 写 meta.json（含每张 sprite 的 rect） |
| `--json-only` | 关 | **只导出 meta.json 不切 PNG**；无 `--output` 时 JSON 直出 stdout（纯净，供 UI 捕获） |
| `--gen-masks` | 关 | 为每个 sprite 生成全白 mask PNG（`masks/`）+ meta.json 写 `mask` 字段（UI 橡皮擦起点） |
| `--erase-tl WxH` | — | 与 `--gen-masks` 配合：每个 mask 左上角擦除 WxH（黑=擦除），随后直接切图 |

### manual / from-json（仅输出）

| 参数 | 说明 |
|---|---|
| `--output DIR` | 输出目录（默认 `./out/sprites`） |

`manual` 从 **stdin** 读框（`x y width height` 每行一个，空行或 `q` 结束），始终写 meta.json；不支持 `-` 输入。

### sheet

| 参数 | 说明 |
|---|---|
| `--cols N` | **必填**：每行精灵数 |
| `--from-json FILE` | 从 meta.json 加载 rects（应用 mask）；缺省则用下方检测 flag 自动检测 |
| 检测 flag | 同 split（`--mode/--cell-size/--merge-distance/...`） |

输出 `sheet.png` + `sheet_meta.json`（每张精灵 src/dst 坐标）。

### 通用

| 参数 | 说明 |
|---|---|
| `--format json\|text` | 默认 text。json 模式：stdout 只输出单个结果对象（`status/count/sprites/...`），进度与警告走 stderr，**管道安全**（`| jq` 直接用） |
| `-q, --quiet` | text 模式只输出汇总 |
| `--version` / `--help` | 版本 / 帮助（`sprite-split <cmd> --help` 看命令专属帮助） |
| `input` 为 `-` | 从 stdin 读 PNG（除 manual；与 `remove-background --stdout` 组合成纯管道） |

## 推荐工作流

```
1. info 分析 → 看两步推荐（--format json 可脚本化）
2. 不透明素材 → remove-background（--stdout 管道或先落盘透明图）
3. split 按推荐参数切分 → 检查输出
4. 不满意 → 手动微调：改 meta.json 或用 manual 重画框
5. from-json 按手工 rect 精确重切
6. 需要整图打包 → sheet --cols N --from-json out/sprites/meta.json
```

## 常见陷阱

- **split 输入必须透明**：全不透明 PNG 直接 split 会 warning + 0 个 sprite（不再有内嵌 `--remove-background`）。先去背景再切：`remove-background x.png --stdout | split - ...`
- **背景色估错**（四角/边缘被内容占满）：自动环带采样会失效 → 手动 `--bg-color R,G,B` 指定背景色，配合 `--background-threshold` 形成颜色区间
- **纯色背景但边缘有残色边（halo）**：JPEG 压缩/抗锯齿会让物体边缘产生一圈「接近背景的过渡色」。`--edge-clean` 已默认清扫 3 圈；仍残留可提高 `--background-threshold`（下限，不会缩小自动容差）
- **精灵边缘有杂边**（清理背景后的白边/色边 halo）：提高 `--edge-clean` 圈数优先（零开销）；`--contract` 已删除（解耦时移除）
- **半透明软边**：`remove-background` remote 路径保留 AI 软边 alpha（`process_transparent`）；透明图进 `split` 后按 `--alpha-threshold` 切分，软边像素 alpha > 阈值即保留——**解耦后不再有二值 mask 桥接的语义差异**
- **噪点多**：`info` 看 component_count 与中位数面积；大量小分量 → 用 `--min-width/--min-height` 过滤（推荐值为最大精灵的 1/4）
- **`--mode auto`**：假设→打分→验证→回退。行列投影 + Pearson 自相关找候选周期 → offset 搜索对齐组件中心 → 多维评分（周期/对齐/边界/尺寸/占用）→ 谐波抑制；置信度 <0.65 或周期性 <0.25 或组件 <4 时自动**回退 components**
- **`--merge-distance` 仅 components 模式有效**，与 grid/auto 混用会报错
- **手动框越界**：`manual` 会提示跳过；`from-json` 会自动 clamp 到图像范围
- **flag 校验**：命令专属 flag 用错命令会报 "not expected"；类型/范围校验（如 `--cell-size 0`、`--mode bogus`）在 parse 阶段拦截，退出码 1
- **`--stdout` 与 `--format json` 互斥**：`--stdout` 时 stdout 是 PNG 二进制，结果摘要走 stderr
- 切完建议抽查输出 PNG 四角 alpha（去背景素材应为 0）与内容覆盖率

## 验证方法

> 原则：**能用 `--format json` 断言就不要肉眼看输出**。以下所有验证均可一键复制执行，
> 断言失败时命令退出非零（jq -e），可直接接进 CI / 脚本回归。进度日志走 stderr，不影响断言。

### 1. 单元测试（core 算法层）

```bash
ctest --test-dir build          # 全绿：92 用例
```

### 2. 端到端素材

- `tests/fixtures/test_sheet.png`：alpha 表（默认参数下 5 个组件：3 精灵 + 半透明组件 + 噪点组件，min-size 默认 1 不过滤）
- `tests/fixtures/grid8_sheet.png`：白底 8×8 网格（64x64，无 alpha）
- `tests/fixtures/merge_sheet.png`：间隙合并

### 3. 端到端验证（管道 / JSON 断言，可脚本化）

```bash
SPLIT=build/sprite-split; FIX=tests/fixtures

# 3.1 info 分析 + 推荐参数
$SPLIT info $FIX/grid8_sheet.png --format json | jq -e '.status == "ok" and .width == 64 and .has_transparency == false'

# 3.2 remove-background 整图透明导出（JSON 字段：output/bg_backend/background_percent）
$SPLIT remove-background $FIX/grid8_sheet.png --output /tmp/sps_check --format json | \
  jq -e '.status == "ok" and .bg_backend == "color" and .background_percent > 90'

# 3.3 推荐管道（JSON 桥接）：去背景后 grid 按 alpha 统计 → 5 个前景组件
out=$($SPLIT remove-background $FIX/grid8_sheet.png --output /tmp/sps_check --format json | jq -r '.output')
$SPLIT split "$out" --mode grid --cell-size 8 --output /tmp/sps_check/s --format json | jq -e '.count == 5'

# 3.4 真管道（--stdout + stdin '-'）：PNG 二进制直传，与 3.3 结果一致
$SPLIT remove-background $FIX/grid8_sheet.png --stdout 2>/dev/null | \
  $SPLIT split - --mode grid --cell-size 8 --output /tmp/sps_check/s2 --format json | jq -e '.count == 5'

# 3.5 sheet 重排（透明图，5 个 sprite → 8 列 1 行：128x16）
$SPLIT sheet /tmp/sps_check/grid8_sheet_transparent.png --cols 8 --mode grid --cell-size 8 \
  --output /tmp/sps_check/sheet --format json | jq -e '.status == "ok" and .width == 128 and .height == 16 and .count == 5'

# 3.6 json-only 直出 stdout（无 --output，meta 纯净可捕获；test_sheet 默认参数 5 个组件）
$SPLIT split $FIX/test_sheet.png --json-only --format json | jq -e '.json_only == true and .count == 5'

# 3.7 两个管道路径产物逐字节一致（golden）
cmp /tmp/sps_check/s/sprite_1.png /tmp/sps_check/s2/sprite_1.png && cmp /tmp/sps_check/s/sprite_2.png /tmp/sps_check/s2/sprite_2.png
```

> ⚠️ **split 不接受不透明原图**：`split grid8_sheet.png`（未去背景）会 warning + count 0。
> 必须先 `remove-background`（见 3.3/3.4）——这正是解耦后的正确用法。

### 4. 一键 E2E 脚本

```bash
# 全部断言失败即退出非零；可用于本地回归 / CI
set -e
SPLIT=build/sprite-split; FIX=tests/fixtures; TMP=/tmp/sps_e2e; rm -rf $TMP; mkdir -p $TMP
$SPLIT info $FIX/grid8_sheet.png --format json | jq -e '.components >= 1' >/dev/null
$SPLIT remove-background $FIX/grid8_sheet.png --output $TMP --format json | jq -e '.background_percent > 90' >/dev/null
$SPLIT remove-background $FIX/merge_sheet.png --stdout 2>/dev/null | \
  $SPLIT split - --merge-distance 3 --output $TMP/s --format json | jq -e '.count == 1' >/dev/null
$SPLIT remove-background $FIX/grid8_sheet.png --bg-backend remote --bg-url http://127.0.0.1:9 \
  --output $TMP --format json | jq -e '.bg_backend == "color"' >/dev/null   # 不可达 → 回退
echo "E2E OK"
```
