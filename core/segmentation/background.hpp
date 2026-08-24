#pragma once

#include "image/image.hpp"
#include "mask/mask.hpp"

namespace sps {

// 背景检测选项
struct BackgroundOptions {
    // 颜色距离阈值（RGB 各通道差的绝对值之和）：
    // 与背景色距离 <= threshold 视为背景
    int threshold = 12;

    // 手动指定背景色（可选）。未设置时自动用四角像素均值估计。
    // 适用于四角被内容占满、或想限定精确颜色区间的场景。
    bool has_bg_color = false;
    Pixel bg_color{};
};

// 四角颜色采样估计背景色 + flood fill 从边缘向内填充背景 mask。
// 返回的 mask 中 true = 背景，false = 前景。
Mask background_mask(const Image& image, const BackgroundOptions& options = {});

// 将背景像素的 alpha 通道置 0（用于导出透明 PNG）。
// 输入 image 会被原地修改；background 为 background_mask 的输出。
void make_background_transparent(Image& image, const Mask& background);

}  // namespace sps
