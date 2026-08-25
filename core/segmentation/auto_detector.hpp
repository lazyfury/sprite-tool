#pragma once

#include "mask/mask.hpp"
#include "model/sprite_rect.hpp"
#include "segmentation/connected_components.hpp"
#include "segmentation/grid_detector.hpp"

#include <vector>

namespace sps {

// Auto 最终切割策略
enum class AutoSliceMode {
    Components = 0,        // 纯物体边界（Component BBox）
    Grid = 1,              // 纯网格单元（Cell）
    ComponentsInGrid = 2,  // 网格布局 + 物体边界（每 cell 主组件 BBox）
};

// 用户强制策略覆盖（UI「切割策略」；默认自动决策）
enum class SlicePolicy {
    Auto = 0,        // 自动决策（默认）
    Components = 1,  // 强制物体边界（BBox，全部组件输出）
    Grid = 2,        // 强制网格单元（Cell，即使组件不匹配）
};

// Auto 检测参数
struct AutoOptions {
    int min_width = 1;          // 组件 bbox 宽下限
    int min_height = 1;         // 组件 bbox 高下限
    int min_opaque_pixels = 16; // cell 非空的最小前景像素数（Grid 切割/评分）
    int merge_distance = 0;     // >0：组件 bbox 膨胀相交即合并（0 = 关闭）
    int min_grid_size = 4;      // 网格周期搜索下限
    int max_grid_size = 0;      // 网格周期搜索上限（0 = 自动 min(W,H)/2）
    int padding = 0;            // 最终 rect 外扩（ComponentsInGrid 时 clamp 到 cell）
    SlicePolicy slice_policy = SlicePolicy::Auto;  // 强制策略覆盖
};

// Auto 检测结果：决策 + 完整中间状态（供 UI/日志展示）
struct AutoDetection {
    AutoSliceMode mode = AutoSliceMode::Components;
    double confidence = 0.0;      // grid 可信度（未检测到网格 = 0）

    GridDetection grid;           // 网格检测完整结果（candidates/best/is_grid）
    std::vector<ComponentSprite> components;  // 过滤+合并后的精灵级组件
    std::vector<SpriteRect> rects;            // 最终结果（按 cell 行优先排序）

    // 诊断统计
    int raw_component_count = 0;
    int filtered_component_count = 0;
    int merged_component_count = 0;
    int grid_columns = 0;
    int grid_rows = 0;
    double cell_occupancy = 0.0;  // 平均 cell 前景像素占比
    int occupied_cells = 0;       // 组件占据的 cell 数
    int cells_with_single = 0;    // 恰好 1 个组件的 cell 数
    int cells_with_multi = 0;     // 多于 1 个组件的 cell 数
};

// Auto 主入口：Mask → Components（过滤/合并）→ Grid 检测 → Component→Cell 映射
// → 决策（COMPONENTS / GRID / COMPONENTS_IN_GRID）→ 生成 rects。
//
// 决策规则：
//   - 无网格或组件不匹配（cell 内多组件）→ COMPONENTS（BBox）
//   - 网格强（score>=0.8）且组件 bbox ≈ cell（>=85%）→ GRID（Cell）
//   - 网格可信且每 cell 恰好 1 个组件 → COMPONENTS_IN_GRID（主组件 BBox）
//
// alpha 阈值等 mask 生成逻辑由调用方（splitter）负责，本函数只接收前景 mask。
AutoDetection auto_detect(const Mask& mask, const AutoOptions& options);

}  // namespace sps
