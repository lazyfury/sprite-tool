#include "segmentation/background.hpp"

#include "image/pixel.hpp"

#include <cstdlib>
#include <queue>
#include <utility>

namespace sps {

namespace {

// 背景色估计：四角像素均值
Pixel estimate_background_color(const Image& image) {
    const int w = image.width();
    const int h = image.height();
    const Pixel tl = image.at(0, 0);
    const Pixel tr = image.at(w - 1, 0);
    const Pixel bl = image.at(0, h - 1);
    const Pixel br = image.at(w - 1, h - 1);

    Pixel bg;
    bg.r = static_cast<uint8_t>((tl.r + tr.r + bl.r + br.r) / 4);
    bg.g = static_cast<uint8_t>((tl.g + tr.g + bl.g + br.g) / 4);
    bg.b = static_cast<uint8_t>((tl.b + tr.b + bl.b + br.b) / 4);
    return bg;
}

// RGB 曼哈顿距离（通道差绝对值之和），与 threshold 同量纲
int color_distance(const Pixel& a, const Pixel& b) {
    return std::abs(static_cast<int>(a.r) - b.r) +
           std::abs(static_cast<int>(a.g) - b.g) +
           std::abs(static_cast<int>(a.b) - b.b);
}

}  // namespace

Mask background_mask(const Image& image, const BackgroundOptions& options) {
    const int w = image.width();
    const int h = image.height();
    Mask mask(w, h, false);
    if (w <= 0 || h <= 0) return mask;

    // 背景色：手动指定优先，否则四角采样
    const Pixel bg = options.has_bg_color ? options.bg_color : estimate_background_color(image);

    std::queue<std::pair<int, int>> queue;
    auto try_seed = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        if (mask.get(x, y)) return;  // 已标记
        if (color_distance(image.at(x, y), bg) <= options.threshold) {
            mask.set(x, y, true);
            queue.emplace(x, y);
        }
    };

    // 从四条边缘播种
    for (int x = 0; x < w; ++x) {
        try_seed(x, 0);
        try_seed(x, h - 1);
    }
    for (int y = 0; y < h; ++y) {
        try_seed(0, y);
        try_seed(w - 1, y);
    }

    // BFS：向内部扩展，与背景色接近的像素继续标记为背景
    while (!queue.empty()) {
        const auto [x, y] = queue.front();
        queue.pop();
        try_seed(x - 1, y);
        try_seed(x + 1, y);
        try_seed(x, y - 1);
        try_seed(x, y + 1);
    }
    return mask;
}

void make_background_transparent(Image& image, const Mask& background) {
    const int w = image.width();
    const int h = image.height();
    if (w != background.width() || h != background.height()) {
        throw std::invalid_argument(
            "sps: make_background_transparent: image/mask size mismatch");
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (background.get(x, y)) {
                image.at(x, y).a = 0;  // 背景像素 alpha 置 0
            }
        }
    }
}

}  // namespace sps
