#pragma once

#include "image/pixel.hpp"

namespace sps {

// 检测模式
enum class DetectionMode {
    ConnectedComponents,  // 连通分量（M1）
    Grid,                 // 网格（M2）
    Auto,                 // 自动（M2）
};

// 切分选项
struct SplitOptions {
    // 前景判定：alpha > alpha_threshold 视为前景
    int alpha_threshold = 1;

    // 背景清理：从四角 flood fill 判定背景
    bool remove_background = false;
    // remove_background=true 时的颜色距离阈值（RGB 曼哈顿距离）
    int background_threshold = 12;
    // remove_background=true 时手动指定背景色（可选；不设则四角采样）
    bool has_bg_color = false;
    Pixel bg_color{};

    // 检测出 sprite 后向内收缩的像素数（类似 PS 收缩/去杂边），默认 0=不收缩
    int contract = 0;

    // 每个精灵向外扩展的透明边距（像素）
    int padding = 0;

    // 过滤：小于该尺寸的连通分量被丢弃
    int min_width = 1;
    int min_height = 1;

    // 邻近合并（M2 实现）：分量间距 <= merge_distance 时合并
    bool merge_nearby = false;
    int merge_distance = 0;

    DetectionMode mode = DetectionMode::ConnectedComponents;

    // Grid 模式下的格子尺寸（M2）
    int grid_cell_size = 16;
};

}  // namespace sps
