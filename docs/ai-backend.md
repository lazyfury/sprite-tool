# AI 背景清理后端 — 技术调研与设计（--remove-background 可选后端 / M4）

> 状态：⚠️ 部分落地 —— `BackgroundRemover` 抽象已实现（core）+ `Remote` 后端（extra/bg_remote）已实现；
> **ONNX/AI 后端仍搁置**（决策点待用户确认，见 §9）
> 目标：给现有 `--remove-background` 增加一个**可选的深度学习后端**，替换背景 mask 的生成方式，
> 其余管线（mask → CCL/Grid/merge → 导出）全部复用，纯算法后端保持默认、零回归。

## 1. 接入点分析（为什么这个方案很顺）

现有背景清理链路（`cli/main.cpp` `apply_background_cleanup`，split/manual/from-json/sheet 共用）：

```
BackgroundOptions → background_mask(image, bg) → Mask(背景) → make_background_transparent → 透明 PNG
```

- AI 后端只需替换 `background_mask` 这一个调用点：输出同样语义的 **Mask（true=背景）**，下游全兼容
- 无需新子命令、无需改动 split/from-json/sheet 的导出逻辑
- M4 早已预留 `BackgroundRemover` 抽象（`virtual Mask process(const Image&)`）——本方案即该抽象的落地

## 2. 架构设计（已落地：Color + Remote）

```
CliOpts --bg-backend color|remote
        │
        ▼
BackgroundRemover (抽象接口，core/segmentation/background_remover.hpp)  ← 已落地
   ├── ColorBackgroundRemover            ← core 内置（四角采样 + flood fill，默认，零回归）
   └── RemoteBackgroundRemover           ← extra/bg_remote（httplib，网络依赖收敛在此）
        │
        ▼
Mask(背景) → 现有管线（CCL / Grid / Auto / merge / 导出）
```

后端通过**注册表/工厂**接入：core 默认注册 `Color`；`extra/bg_remote` 提供
`sps::bg_remote::register_backend()`（CLI main 入口调用），注册 `Remote`。
新增后端（如 ONNX）只需实现同一接口并注册，core 零改动。

> **⚠️ 关键语义**：后端 mask 必须**真正参与切分**，而不只是导出透明化。
> `split_image(image, options, bg_mask)` 接受外部背景 mask：`--bg-backend remote` 时
> CLI 把 AI mask 传给 split_image（覆盖内部 color 计算），CCL/Grid/contract 全部
> 基于 AI mask 执行，导出 PNG 的 alpha 与切分边界同源。修复前 remote 只改 alpha、
> rect 与 color 完全一致（AI 对切分零影响）——见 cli/main.cpp `apply_background_cleanup`。

```cpp
// core/segmentation/background_remover.hpp（已落地，见代码注释）
struct BackgroundRemoverOptions {
    BackgroundOptions color{};   // 纯算法参数（threshold / bg_color / edge_passes）
    std::string remote_url;      // remote：服务 base URL
};

class BackgroundRemover {
public:
    virtual ~BackgroundRemover() = default;
    virtual Mask process(const Image&) const = 0;   // 返回 true=背景
};
// register_background_remover(kind, factory) / create_background_remover(kind, opts)
```

**降级回退**（CLI 层捕获异常）：
- `--bg-backend ai`：模型缺失 / ORT 初始化失败 / 推理异常 → `warning:` + 自动回退 color（或 `--bg-backend ai --bg-no-fallback` 强制报错，便于 CI 发现）
- `--bg-backend auto`（默认建议值）：有模型文件 → ai，否则 color

## 3. CLI 参数设计（非交互）

```
--bg-backend color|ai|auto   背景清理后端（默认 color；auto = 有模型则 ai）
--bg-model PATH              模型路径（默认 models/isnet-general-use.onnx）
--bg-threshold 0.5           AI 概率图二值化阈值（默认 0.5）
```

与现有 `--bg-color` / `--background-threshold` 平级，属于 remove-background 参数组。

## 4. 关键技术调研（2026-08 实测）

### 4.1 ONNX Runtime

| 项 | 结论 |
|---|---|
| 最新版 | **v1.29.0**（api.github.com 实测） |
| macOS arm64 预编译 | ✅ `onnxruntime-osx-arm64-1.29.0.tgz` **39.7 MB** 官方存在 |
| 后端选择 | CPU EP 即可（Apple Silicon 无 CUDA；CoreML EP 可选加速，但需 .mlmodel，先不做） |
| 构建集成 | 编译期 option `SPS_ENABLE_ONNX` + 链接 ORT（CMake） |
| 依赖获取 | ⚠️ 40MB 二进制：入 `third_party/` 会让 git 膨胀；FetchContent 构建期下载违背「构建不依赖网络」原则 → **需用户决策** |

### 4.2 模型（rembg 生态，24.4k stars）

| 模型 | 大小 | 适用 | 512px CPU 推理量级 |
|---|---|---|---|
| isnet-general-use | ~176 MB | 通用（照片/自然图） | 200–600ms |
| isnet-anime | ~43 MB | 动漫/插画 | ~100–300ms |
| u2netp | ~4.7 MB | 轻量通用 | ~50–150ms |
| silueta | ~42 MB | u2net 蒸馏 | ~100–300ms |

- 均输出**单通道前景概率图**（0~1），经 sigmoid → 阈值 → 前景 mask → 取反为背景 mask
- 输入需 resize 到模型固定尺寸（通常 320~1024）→ normalize → CHW float32
- 模型外置 `models/` **不入库**，支持 `--bg-model` 自定义路径

### 4.3 管线适配细节

```
原图 → resize(512×512) → RGB normalize → CHW float → ORT 推理
     → sigmoid → 上采样回原尺寸 → 阈值(0.5) → 前景 mask → 取反 → 背景 Mask
```

- 低分辨率 mask 上采样 → 边缘较糊：可配合现有 `erode`/`--contract` 收缩精修（P2）；后续 feather 可选
- **⚠️ 像素游戏素材与自然图模型的 mismatch**：AI 对半透明、细线、规律纹理可能误删/保留错误——这正是「AI 只是后端之一、默认仍是纯算法」的原因

## 5. 备选方案对比

| 方案 | 优点 | 缺点 | 结论 |
|---|---|---|---|
| **A. ONNX Runtime C++** | 跨平台、与 GDExtension 路线兼容、性能好 | 40MB 依赖 + 构建复杂度 | ✅ 推荐 |
| B. CoreML（macOS 专属） | 性能佳、无额外运行时 | 绑死 macOS，与 Godot 跨平台冲突 | 不作主方案 |
| C. 子进程调 Python rembg | 零 C++ 依赖、开发最快 | 违背 C++ 零依赖原则、需 Python 环境、慢 | 仅作快速验证，不落产品 |
| D. 纯算法增强（HSV/Lab/边缘） | 零依赖 | 天花板低，解决不了复杂背景 | 独立路线，与 AI 不冲突 |

## 6. 成本与风险（决策前确认）

| 项 | 成本/风险 | 缓解 |
|---|---|---|
| ORT 二进制 39.7MB | 入库 → git 膨胀；下载 → 构建依赖网络 | **决策点①**：用户选入库 / 首次构建下载 / 暂缓 |
| 模型 5~176MB | 磁盘占用（外置不入库） | 默认 u2netp(4.7MB) 起步，`--bg-model` 换大模型 |
| 开发调试 | 模型 IO 名/维度/归一化细节易踩坑 | 先用 rembg 官方转换脚本生成对齐的 ONNX 再联调 |
| 推理延迟 | CPU 单张 50~600ms | 一次性处理可接受；批处理再考虑并行（不做） |
| Godot 插件（M5） | 插件需随包分发 ORT dylib | M5 时评估；AI 是可选特性不阻塞插件主线 |
| 像素素材误删 | 细线/半透明被 AI 破坏 | 默认 color 后端；AI 由用户显式开启 |

## 7. 测试与验收

- **零回归**：默认 `color` 后端下现有 88 用例 / 347 断言全绿；`--bg-backend color` 显式路径同
- **单测（无需模型）**：`BackgroundRemover::create` 工厂 + mock remover 注入验证管线集成；回退逻辑（ai 缺失 → warn + color）
- **集成（需模型，本地手动）**：isnet-anime 在动漫素材 / u2netp 在照片素材上端到端切分；`--format json` 输出含 `bg_backend` 字段
- **验收**：`split input.png --remove-background --bg-backend ai --bg-model models/u2netp.onnx` 出正确透明 PNG；无模型时自动回退 color 且不报错（auto）

## 8. 网格检测需要 AI 模型吗？——不需要（FAQ）

**结论：grid/auto 检测不引入专门的 DL 模型，AI 只在上游（背景清理）间接提升它。**

| 论证 | 说明 |
|---|---|
| 任务本质 | `detect_grid` 输入是**前景 Mask**（已语义简化），找的是周期（cell size/offset）——自相关/FFT 是周期检测经典最优解，无可学习的感知内容 |
| 无现成模型 | GitHub 实测检索 `sprite sheet grid detection` DL 方案为零（niche 任务，无公开数据集） |
| 现有算法已达标 | 干净 mask 上 M2 验收通过（8×8 自动检出 / 不规则回退）；失败根因是上游 mask 不干净 |
| 正确架构 | 原图 → **AI 背景清理（可选）** → 干净 mask → **现有 detect_grid（传统）** → rects |

替代思路均不推荐：文档表格检测模型（Table Transformer）迁移（依赖大、未必更准）；自监督周期检测（研究级）；逐 cell 目标检测（标注成本高、参数化检测更优）。

## 9. 决策点（待用户确认）

1. ORT 依赖获取方式：a) 二进制入 `third_party/`（git 膨胀 ~40MB）b) 首次构建脚本下载（需网络）c) 暂缓，先用方案 C 验证效果
2. 默认模型：u2netp（4.7MB 轻量快）起步？还是直接 isnet-general-use（176MB 效果最好）？
3. `--bg-backend` 默认值：color（保守，零变化）还是 auto（有模型自动用）？
4. 是否本次只做「调研 + 抽象接口预留」，ORT 集成等 M5 Godot 插件阶段一起做？

## 10. 与现有文档的关系

- 对应 `agent.md` §4.2 M4（AI 分割）与 `todo.md` M4 节——本文件为 M4 的方案细化
- 与 `docs/magic-wand.md`（魔棒种子）互补：魔棒解决「手动指定背景」，AI 解决「复杂背景自动识别」，最终都汇入同一个 `BackgroundRemover` 抽象
- 与 `docs/magic-wand.md`（魔棒种子）互补：魔棒解决「手动指定背景」，AI 解决「复杂背景自动识别」，最终都汇入同一个 `BackgroundRemover` 抽象
