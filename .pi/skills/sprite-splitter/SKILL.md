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
```

## 完整参数

### split（检测 + 导出，sheet 检测路径共用同一组）

| 参数 | 默认 | 说明 |
|---|---|---|
| `--output DIR` | `./sprites` | 输出目录（自动创建） |
| `--alpha-threshold N` | `1` | alpha > N 视为前景（透明素材） |
| `--padding N` | `0` | 精灵向外扩展像素（须 ≥0） |
| `--min-width/--min-height N` | `1` | 过滤小于该尺寸的分量（滤噪点） |
| `--remove-background` | 关 | 四角采样 + flood fill 去背景，**导出透明 PNG** |
| `--background-threshold N` | `12` | 背景色距离阈值（RGB 曼哈顿距离） |
| `--bg-color R,G,B` | 自动 | 手动指定背景色（四角被内容占满时用，与 threshold 构成颜色区间） |
| `--contract N` | `0` | 检测后向内收缩 N 像素（去杂边/PS 收缩） |
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
- **背景色估错**（四角被内容占满）：自动四角采样会失效 → 手动 `--bg-color R,G,B` 指定背景色，配合 `--background-threshold` 形成颜色区间
- **精灵边缘有杂边**（清理背景后的白边/色边）：`--contract N` 向内收缩去边（类似 PS 收缩）
- **噪点多**：`info` 看 component_count 与中位数面积；大量小分量 → 用 `--min-width/--min-height` 过滤（推荐值为最大精灵的 1/4）
- **`--mode auto`**：假设→打分→验证→回退。行列投影 + Pearson 自相关找候选周期 → offset 搜索对齐组件中心 → 多维评分（周期/对齐/边界/尺寸/占用）→ 谐波抑制；置信度 <0.65 或周期性 <0.25 或组件 <4 时自动**回退 components**
- **`--merge-distance` 仅 components 模式有效**，与 grid/auto 混用会报错
- **手动框越界**：`manual` 会提示跳过；`from-json` 会自动 clamp 到图像范围
- **flag 白名单**：把 split 专属 flag（如 `--json`）用在 info 上会报 "not valid for this command"
- 切完建议抽查输出 PNG 四角 alpha（`--remove-background` 时应为 0）与内容覆盖率

## 验证方法

- 单元测试：`ctest --test-dir build`（全绿：88 用例 / 347 断言）
- 端到端素材：`tests/fixtures/test_sheet.png`（alpha 表）、`grid8_sheet.png`（白底 8×8 网格）、`merge_sheet.png`（间隙合并）
- 输出检查：`build/sprite-split split tests/fixtures/grid8_sheet.png --mode grid --cell-size 8 -q` 应得 64 个 sprite；`sheet` 命令：`build/sprite-split sheet tests/fixtures/grid8_sheet.png --cols 8 --mode grid --cell-size 8 -q` 应得 128x128 的 sheet.png
