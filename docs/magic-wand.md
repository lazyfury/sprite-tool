# Magic Wand 魔棒背景清理 — 技术调研与设计（M3 补充）

> 状态：⏸ **搁置**（规划完成，暂不实现；待 UI 阶段再评估）｜里程碑归属：M3 补充功能（不新建里程碑、不新建子命令）
> 形态：**非交互**。不做点击式 UI / 预览；通过 CLI 参数 `--seed X,Y`（可重复）指定背景点，
> 算法从种子点 flood fill 出背景 mask，复用现有 `background_mask` / `Mask` / `morphology` 管线。

## 1. 现有 `--remove-background` 算法剖析

### 1.1 算法链（`core/segmentation/background.cpp`）

```
四角 4 像素 RGB 均值 → 背景色估计 bg
        ↓
RGB 曼哈顿距离 color_distance(a, b) = |dr| + |dg| + |db|   （≤ threshold 判为背景）
        ↓
从四条边缘播种 → BFS flood fill（4-connectivity，向内扩散）
        ↓
背景 Mask（true = 背景）→ make_background_transparent 把背景 alpha 置 0
```

### 1.2 优点

| # | 优点 | 说明 |
|---|---|---|
| 1 | 全自动、零参数可用 | 默认 `--background-threshold 12` 即可处理白/黑/纯色均匀底 |
| 2 | 线性复杂度 | O(W×H) 单遍 BFS + 无额外内存，性能无忧 |
| 3 | flood fill 保证连通性 | 前景内部的同色区域（如角色徽章里的白点）不会被误删 |
| 4 | 已有完整验证 | 白底/黑底/纯色底 3 组单测 + 端到端用例（grid8/merge_sheet） |
| 5 | 与 split 管线天然集成 | 输出 Mask → CCL / Grid / Auto / merge 全兼容，无需新接线 |

### 1.3 缺点

| # | 缺点 | 典型场景 | 影响 |
|---|---|---|---|
| 1 | **背景色估计依赖四角** | 前景占满四角（角色贴边、角落有装饰物）；背景渐变/阴影/暗角 | 四角均值被污染 → 背景色估计错 → flood fill 断裂或误删 |
| 2 | **曼哈顿距离 ≠ 感知距离** | 同一背景上的阴影/高光区；JPEG 压缩噪点；抗锯齿边缘色带 | 亮度差异即判为不同色：扩散提前断裂，残留背景碎片；阈值小删不净、大则误删前景同色系 |
| 3 | **单阈值 + 单色背景假设** | 背景非均匀（同色系渐变、多区域不同底色） | 一个 threshold 无法两全；`--bg-color` 只能给"一个色"，救不了渐变/多区域 |
| 4 | **二值 mask 边缘硬化** | 所有素材导出 | 边缘 1px 抗锯齿像素要么全删（锯齿缺口）要么全留（背景色 halo/色边） |
| 5 | **同色前景穿透** | 白角色在白底、白色高光 | flood fill 沿同色像素穿过，误删前景内部 |
| 6 | **无纠错入口** | 四角采样结果不可信时 | 只能整体调 threshold 或手填 `--bg-color`，无法"指定这里才是背景" |

> 缺点 1、3、6 本质是同一个根因：**背景色来源不可控**（只能四角自动或手填单色）。
> 魔棒种子正是为补齐这一点而设计——用户指定"真实背景在哪"，取色与扩散起点都更可控。

## 2. 魔棒种子方案设计

### 2.1 核心思路（最小侵入）

魔棒 ≠ 新算法，而是给现有 `background_mask` 增加**用户指定的种子起点**：

- **无 `--seed`**：行为与现在完全一致（四边播种，向后兼容，旧用例零回归）
- **有 `--seed X,Y`**：种子点加入 BFS 起始集合（与四边播种取并集，两者互补不冲突）；
  背景色估计优先从种子像素均值采样（若未指定 `--bg-color`）

这样"种子来源"从「只能四边」变为「四边 ∪ 用户点」，flood fill / 距离 / mask 管线全部复用。

### 2.2 API 扩展（`core/segmentation/background.hpp`）

```cpp
struct BackgroundOptions {
    int threshold = 12;
    bool has_bg_color = false;
    Pixel bg_color{};

    // 新增：魔棒种子点（背景位置，可多个）。空 = 保持旧行为（仅四边播种）。
    std::vector<PixelCoord> seeds;
};
```

`background_mask` 内部改动点：

1. **种子来源**：BFS 起始集合 = `seeds` ∪ 四条边（seeds 空则仅四边）
2. **背景色估计优先级**：`--bg-color`（显式）> seeds 像素均值（有 seeds 时）> 四角均值（现状）
3. 越界种子忽略 + 返回统计（供 CLI 提示）；种子与估计背景色距离远超 threshold 时警告"疑似点在前景上"

### 2.3 CLI 参数（非交互）

```
--seed X,Y           可重复；指定背景位置（魔棒种子）。要求与 --remove-background 配对
                      （校验与现有 --bg-color 一致：单独出现报错提示）
--bg-color R,G,B     已有：显式背景色（优先级高于种子采样）
--background-threshold N  已有：颜色距离阈值
```

生效子命令：`split` / `sheet`（detection flags 组）与 `from-json`（已有 remove-background 组）。

### 2.4 示例

```bash
# 前景占满四角 → 四角取色失效，改由种子取色 + 扩散
build/sprite-split split char.png --remove-background --seed 300,5 --seed 5,400 --output ./out/sprites

# 种子 + 显式阈值，导出透明 + JSON
build/sprite-split split sheet.png --remove-background --seed 10,10 --background-threshold 20 \
  --output ./out/sprites --json
```

## 3. 边界与回退

| 场景 | 行为 |
|---|---|
| 无 `--seed` | 与现状逐字节一致（旧测试零回归） |
| 种子越界 | 忽略该种子并 warn，其余种子继续 |
| 种子点在前景上 | warn（种子颜色与估计背景色距离 >> threshold），不中止 |
| 多区域背景 | 每个区域一个 `--seed`，BFS 并集一次处理 |
| `--seed` 未配 `--remove-background` | 报错（与 `--bg-color` 现有校验一致） |

## 4. 可选增强（本次不做，记录备选）

| 项 | 方案 | 复用 | 优先级 |
|---|---|---|---|
| 去边缘 halo | `--bg-erode N`：背景 mask 膨胀 N 圈后导出（吞掉边缘残留背景色） | 现有 `dilate` | P2 |
| 感知颜色距离 | `--color-space rgb\|hsv\|lab`，HSV 可分别调 H/S/V 容差 | 需新增 hsv/lab 转换 | P2 |
| 羽化边缘 | feather：灰度 mask + 模糊，半透明过渡 | 需新增 | P3 |
| 交互式预览 | 鼠标悬停实时候选 mask + shader 高亮 | 依赖 Godot 插件（M5） | 明确不做 |

## 5. 测试与验收

新增 `tests/test_background.cpp` 用例：

- [ ] 前景占满四角 → `--seed` 指定背景点能正确清理（四角均值失效场景）
- [ ] 多区域不同底色背景 → 多种子一次清理干净
- [ ] 种子 + 四边播种并集：边缘背景 + 内部背景区同时覆盖
- [ ] 无 seeds 时结果与改造前完全一致（回归）
- [ ] 种子越界忽略 + warn；种子在前景上 warn（不中止）
- [ ] `--seed` 未配 `--remove-background` → CLI 报错

**验收**：
1. 旧 88 用例 / 347 断言全绿（零回归）
2. 上述新用例全绿
3. `split --remove-background --seed x,y` 端到端：前景占角素材正确切分、导出透明 PNG + meta.json

## 6. 任务清单（同步至 todo.md M3 补充节）

1. `core/segmentation/background.hpp/cpp`：`BackgroundOptions::seeds` + BFS 起始集合扩展 + 种子取色优先级
2. `core/model/split_options.hpp` + `cli/main.cpp`：`--seed` 解析（可重复）、校验、`apply_background_transparency` 透传
3. `tests/test_background.cpp`：新增上述用例
4. `SKILL.md`：补 `--seed` 用法与陷阱
5. 文档同步：todo.md / agent.md / README.md 状态更新
