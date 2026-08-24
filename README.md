# Sprite Splitter

雪碧图智能切割工具（Sprite Sheet Analyzer）：从 Sprite Sheet 中自动检测并切分出独立精灵，导出 PNG / JSON 元数据 / Godot AtlasTexture。

**核心产品是分析器，不是单纯的切割器** —— 检测出 sprite rects 后，可自由导出为多种目标格式。

## 技术路线

```
        ┌──────────── CLI（Phase 1-2）
        │
C++ Core ──────── Godot GDExtension（Phase 5）
        │
        └──────── macOS GUI（未来）
```

- **C++20 核心算法库**（`core/`）：零第三方运行时依赖、不依赖 Godot，可独立测试与复用
- 图片读写：stb_image / stb_image_write（vendored 于 `third_party/`）
- 构建：CMake + ninja；测试：Catch2（单头版）

## 功能路线

| 阶段 | 内容 | 状态 |
|---|---|---|
| M1 | C++ Core：Image / Mask / Connected Components / Bounding Box / Crop / PNG 导出 + CLI | ✅ 完成 |
| M2 | Grid Detection / Auto / Padding / 最小尺寸 / Merge Distance / Morphology / JSON 导出 | ✅ 完成 |
| M3 | 背景清理（颜色采样 + Flood Fill + 手动背景色区间）+ 透明导出 + 收缩 --contract + 图片分析 --info + 橡皮擦 --gen-masks + sheet 重排 | ✅ 完成 |
| M3+ | Magic Wand 魔棒种子清理（--seed 指定背景点，非交互，补四角取色失效场景） | ⏸ 搁置（UI 阶段再评估） |
| M3.5 | CLI 子命令化（info/split/manual/from-json/sheet）+ `--format json` 机器可读输出 + 管道友好 | ✅ 完成 |
| M4 | AI 分割（ONNX，可选） | ⏸ 搁置 |
| M5 | Godot GDExtension 编辑器插件（导出 PNG / AtlasTexture / SpriteFrames） | ⏸ 搁置 |

> 规划细节见 [`agent.md`](agent.md)，任务状态见 [`todo.md`](todo.md)。

## 快速上手（可用）

CLI 采用**子命令模式**：`build/sprite-split <command> [args]`。

```bash
# 环境准备（一次性）
brew install cmake ninja

# 构建
cmake -B build
cmake --build build -j

# 0) 分析图片并获取推荐参数（不切分）
build/sprite-split info input.png

# 1) 透明背景 PNG：按 alpha 切分，导出 PNG + JSON
build/sprite-split split input.png --alpha-threshold 10 --padding 2 --output ./sprites --json

# 2) 白底无透明通道素材：背景清理 + 透明导出 + 网格模式（8x8）
build/sprite-split split sheet.png --remove-background --mode grid --cell-size 8 --output ./sprites

# 3) 连通分量合并（角色上下块间隔 2px）
build/sprite-split split character.png --remove-background --merge-distance 3 --output ./sprites

# 4) 手动画框：交互输入 'x y width height'，写 meta.json + 切图
build/sprite-split manual input.png --output ./sprites

# 5) 从 meta.json 加载 rects 直接切图（可先自动切分再手工编辑 meta.json）
build/sprite-split from-json input.png sprites/meta.json --output ./sprites

# 6) 仅导出 JSON（不切 PNG；无 --output 时 JSON 直出 stdout，供 UI 调用）
build/sprite-split split input.png --remove-background --json-only
build/sprite-split split input.png --remove-background --json-only --output ./sprites

# 7) 橡皮擦工作流：生成全白 mask + meta.json → UI 编辑 mask（黑=擦除）→ 重切
build/sprite-split split input.png --mode auto --gen-masks --output ./sprites
build/sprite-split from-json input.png sprites/meta.json --output sprites_out

# 8) 重排为规整 sprite sheet（--from-json 加载已有 rects，或自动检测）
build/sprite-split sheet input.png --cols 8 --from-json sprites/meta.json --output ./sheet
build/sprite-split sheet input.png --cols 8 --mode grid --cell-size 8 --output ./sheet

# 9) 机器可读输出（管道友好）：--format json，stdout 只含结果对象，进度走 stderr
build/sprite-split info input.png --format json | jq '.components'
build/sprite-split split input.png --output ./sprites --format json | jq '.count'

# 测试
ctest --test-dir build
```

> 完整参数见 `build/sprite-split --help` 与 `build/sprite-split <command> --help`（split 含 --min-width/--min-height/--background-threshold/--mode/--cell-size/--merge-distance/--json/-q/--version）
> 复杂不规则素材若 auto 评分偏低，可显式指定 `--mode grid --cell-size N`

## 项目结构

```
core/           C++ 核心算法库（image / mask / segmentation / model / analyzer / export）
cli/            CLI 入口
godot/          Godot GDExtension（Phase 5）
tests/          Catch2 单元测试
third_party/    vendored 第三方依赖（stb / catch2 / json）
docs/           设计文档
```

## 状态

**M1–M3.5 已完成**：88 用例 / 347 断言全绿；CLI 子命令化 + 机器可读 JSON 输出可用。M4（AI 分割）、M5（Godot GDExtension）搁置。
