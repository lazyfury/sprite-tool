#pragma once

#include "sprite_rect.hpp"
#include <string>
#include <vector>

namespace sps {

// 一次切分的结果：所有精灵的包围盒（已按原图坐标）
struct SplitResult {
    std::vector<SpriteRect> sprites;

    // 橡皮擦 mask：与 sprites 平行对齐（同索引）。
    // 每个元素是黑白图文件路径（白=保留/黑=透明），空串 = 该 sprite 无 mask。
    // 仅从 meta.json 加载时填充；算法切分结果为全空串。
    std::vector<std::string> mask_paths;

    // Auto 模式诊断（mode=Auto 时填充；其它模式保持零值）。
    // 供 CLI --format json / UI 展示「检测到 N 个组件 / 布局 / 切割策略 / 置信度」。
    int auto_mode = -1;              // 0=Components 1=Grid 2=ComponentsInGrid；-1=非 auto
    double auto_confidence = 0;      // grid 可信度（0~1）
    int auto_raw_components = 0;     // CCL 原始组件数（过滤前）
    int auto_filtered_components = 0;
    int auto_merged_components = 0;
    int auto_grid_columns = 0;       // 检测到的网格列数
    int auto_grid_rows = 0;
    int auto_grid_cell_w = 0;        // 检测到的 cell 尺寸（未检测到网格 = 0）
    int auto_grid_cell_h = 0;
    int auto_occupied_cells = 0;     // 组件实际占据的 cell 数
    int auto_cells_with_multi = 0;   // 多于 1 个组件的 cell 数
};

}  // namespace sps
