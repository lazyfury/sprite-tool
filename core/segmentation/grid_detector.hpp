#pragma once

#include "mask/mask.hpp"
#include "model/sprite_rect.hpp"

#include <vector>

namespace sps {

// Grid Detection：把前景 mask 按等尺寸 cell 划分，统计每个 cell 的前景像素，
// 非空 cell 生成一个 SpriteRect（跳过空白 cell）。
// cell 尺寸由调用方指定（SplitOptions::grid_cell_size）。
std::vector<SpriteRect> grid_detect(const Mask& mask, int cell_size);

// ---- Auto Grid Detection：假设→打分→验证→回退 ----
// 核心思路：不找「有没有网格线」，而是寻找能最好解释整张图前景分布的周期。

// 一个网格候选：周期（cell 尺寸）、偏移、各分量得分
struct GridCandidate {
    int period_x = 0;   // cell 宽（X 方向周期）
    int period_y = 0;   // cell 高（Y 方向周期）
    int offset_x = 0;   // 网格起点偏移（使组件对齐 cell 中心）
    int offset_y = 0;

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

// 完整检测流程：投影+自相关 → 候选周期 → offset 搜索 → 多维评分 → 选优
// min_cell/max_cell 限定周期搜索范围（默认 4 ~ min(W,H)/2）
GridDetection detect_grid(const Mask& mask, int min_cell = 4, int max_cell = 0);

// 兼容接口：返回最佳 cell 尺寸（非网格时 0），供 splitter 回退用
int auto_detect_grid_size(const Mask& mask, int min_component_size = 4);

}  // namespace sps
