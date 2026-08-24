#pragma once

#include "image/image.hpp"
#include "image/pixel.hpp"
#include "model/sprite_rect.hpp"

namespace sps {

// 图片基本信息统计：供 CLI --info 与参数推荐使用
struct ImageStats {
    int width = 0;
    int height = 0;

    // alpha 分布（以像素计数）
    long total_pixels = 0;
    long opaque_pixels = 0;      // a == 255
    long transparent_pixels = 0;  // a == 0
    long semi_pixels = 0;         // 0 < a < 255
    bool has_transparency = false;   // 存在透明/半透明像素
    bool uniform_alpha = false;      // 全图 alpha 一致（全不透明或全透明）

    // 背景色估计（四角均值）与一致性
    Pixel bg_estimate{};
    bool bg_uniform = false;  // 四角颜色彼此接近（差 <= 8）

    // 背景清理后的前景占比（threshold 由调用方给出）
    long foreground_pixels = 0;
    int foreground_percent = 0;  // 0-100

    // 连通分量统计（min-size=1，未过滤）
    int component_count = 0;
    SpriteRect largest_component{};  // 面积最大的分量 bbox
    long largest_component_area = 0;
    double median_component_area = 0;  // 面积中位数

    // 推荐的最小尺寸（基于分量面积分布，启发式）
    int suggested_min_width = 1;
    int suggested_min_height = 1;
};

// 分析图片。background_threshold 用于前景/背景判定（flood fill）；
// has_bg_color 时用指定背景色替代四角采样。
ImageStats analyze_image(const Image& image, int background_threshold = 12,
                         bool has_bg_color = false, Pixel bg_color = {});

}  // namespace sps
