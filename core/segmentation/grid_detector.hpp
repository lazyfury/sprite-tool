#pragma once

#include "mask/mask.hpp"
#include "model/sprite_rect.hpp"
#include "segmentation/connected_components.hpp"

#include <vector>

namespace sps {

// Grid Detection：把前景 mask 按等尺寸 cell 划分，统计每个 cell 的前景像素，
// 非空 cell 生成一个 SpriteRect（跳过空白 cell）。
// cell 尺寸与偏移由调用方指定（Grid 模式 cell_size / Auto 检测到的 period+offset）。

// 一个网格候选：周期（cell 尺寸）、偏移、布局统计、各分量得分
struct GridCandidate {
    int period_x = 0;   // cell 宽（X 方向周期）
    int period_y = 0;   // cell 高（Y 方向周期）
    int offset_x = 0;   // 网格原点偏移（cell 左上角；组件中心对齐 cell 中心）
    int offset_y = 0;

    // 布局信息（detect_grid 时填充）
    int columns = 0;            // 覆盖图像的列数
    int rows = 0;               // 覆盖图像的行数
    int component_count = 0;    // 参与 Component→Cell mapping 的组件数
    int occupied_cells = 0;     // 组件实际占据的 cell 数（去重）
    double component_coverage = 0;  // occupied_cells / component_count（每组件独占一 cell 时 = 1）
    double cell_occupancy = 0;      // 平均 cell 前景像素占比（噪声网格此值极低）

    // 每个组件的 cell 线性索引（cell_y * columns + cell_x），与 components 传入顺序一致
    std::vector<int> component_cell_indices;

    // 分量得分（0~1）
    double periodicity = 0;      // 自相关周期强度
    double alignment = 0;        // 组件中心对齐 cell 中心
    double boundary = 0;         // 组件不跨越 cell 边界
    double size_consistency = 0; // 组件尺寸与 cell 比例一致
    double occupancy = 0;        // cell 占用分布合理

    double score = 0;            // 综合加权分（0~1）
};

// 检测结果
struct GridDetection {
    std::vector<GridCandidate> candidates;  // 按 score 降序
    GridCandidate best;
    double confidence = 0;   // best.score
    bool is_grid = false;    // confidence >= kHighConfidence（或 >= kLowThreshold 且其它特征强）
};

// 按网格切分：cell (kx,ky) 覆盖 [offset_x + kx*pw, offset_x + (kx+1)*pw) × ...
// 支持非方形 cell 与任意偏移（负偏移产生的图外部分自动 clamp）。
// 前景像素 >= min_opaque 的 cell 判为非空。返回行优先的 SpriteRect。
std::vector<SpriteRect> grid_detect(const Mask& mask, int cell_width, int cell_height,
                                    int offset_x, int offset_y, int min_opaque = 1);

// 便捷重载：方形 cell、原点 (0,0)、min_opaque=1（Grid 模式 / 旧行为）
inline std::vector<SpriteRect> grid_detect(const Mask& mask, int cell_size) {
    return grid_detect(mask, cell_size, cell_size, 0, 0, 1);
}

// 完整检测流程：投影+自相关 → 候选周期 → offset 搜索 → 多维评分 → 选优
// min_cell/max_cell 限定周期搜索范围（默认 4 ~ min(W,H)/2）。
// components：可选外部精灵级组件（Auto 管线传入 detect_components 结果，避免重复 CCL）；
// 为空时内部对 mask 重新 CCL 并做大组件过滤（仅评分用）。
GridDetection detect_grid(const Mask& mask, int min_cell = 4, int max_cell = 0,
                          const std::vector<ComponentSprite>* components = nullptr);

// 兼容接口：返回最佳 cell 尺寸（非网格时 0）。仅遗留 splitter 旧 Auto 路径使用，
// 重构完成后删除（见 auto_detector）。
int auto_detect_grid_size(const Mask& mask);

}  // namespace sps
