#pragma once

#include "image/image.hpp"
#include "mask/mask.hpp"

namespace sps {

// 背景检测选项
struct BackgroundOptions {
    // 颜色距离阈值（RGB 各通道差的绝对值之和）：
    // 与背景色距离 <= threshold 视为背景。
    // 注意：这是「下限」——当背景自身有压缩/渐变噪声时，
    // 有效阈值会自动放大到 max(threshold, 噪声自适应值)。
    int threshold = 12;

    // 手动指定背景色（可选）。未设置时自动用外圈环带中位数估计。
    // 适用于四角被内容占满、或想限定精确颜色区间的场景。
    bool has_bg_color = false;
    Pixel bg_color{};

    // 边缘过渡色清扫圈数：物体边缘因压缩/抗锯齿产生的过渡相近色，
    // 从背景边界向内最多补吃 N 圈（1 圈 ≈ 1px）。0 = 关闭。默认 3。
    int edge_passes = 3;
};

// 背景稳健估计结果
struct BackgroundEstimate {
    Pixel color{};        // 背景参考色（环带每通道中位数，或用户指定色）
    double sigma_sum = 0;  // 背景噪声水平：每通道标准差之和（0 = 纯色无噪声）
};

// 背景稳健估计：外圈环带（2px）采样 → 每通道中位数参考色 + 噪声水平。
// has_bg_color=true 时 color 取 bg_color（噪声仍从环带接近该色的像素计算）。
BackgroundEstimate estimate_background(const Image& image, bool has_bg_color = false,
                                       Pixel bg_color = {});

// 背景 mask：稳健背景估计 + 自适应阈值 flood fill + 边缘过渡色清扫。
// 返回的 mask 中 true = 背景，false = 前景。
Mask background_mask(const Image& image, const BackgroundOptions& options = {});

// 将背景像素的 alpha 通道置 0（用于导出透明 PNG）。
// 输入 image 会被原地修改；background 为 background_mask 的输出。
void make_background_transparent(Image& image, const Mask& background);

}  // namespace sps
