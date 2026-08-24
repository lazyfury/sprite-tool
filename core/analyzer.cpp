#include "analyzer.hpp"

#include "mask/mask.hpp"
#include "segmentation/background.hpp"
#include "segmentation/connected_components.hpp"

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace sps {

namespace {

// 四角颜色彼此接近？
bool corners_uniform(const Image& image) {
    const int w = image.width();
    const int h = image.height();
    const Pixel c[4] = {image.at(0, 0), image.at(w - 1, 0), image.at(0, h - 1),
                        image.at(w - 1, h - 1)};
    for (int i = 1; i < 4; ++i) {
        const int d = std::abs(static_cast<int>(c[i].r) - c[0].r) +
                      std::abs(static_cast<int>(c[i].g) - c[0].g) +
                      std::abs(static_cast<int>(c[i].b) - c[0].b);
        if (d > 8) return false;
    }
    return true;
}

}  // namespace

ImageStats analyze_image(const Image& image, int background_threshold, bool has_bg_color,
                         Pixel bg_color) {
    ImageStats s;
    if (image.empty()) return s;

    s.width = image.width();
    s.height = image.height();
    s.total_pixels = static_cast<long>(s.width) * s.height;

    // ---- alpha 分布 ----
    for (int y = 0; y < s.height; ++y) {
        for (int x = 0; x < s.width; ++x) {
            const int a = image.at(x, y).a;
            if (a == 255) {
                ++s.opaque_pixels;
            } else if (a == 0) {
                ++s.transparent_pixels;
            } else {
                ++s.semi_pixels;
            }
        }
    }
    s.has_transparency = (s.transparent_pixels + s.semi_pixels) > 0;
    s.uniform_alpha = (s.transparent_pixels == 0 && s.semi_pixels == 0) ||
                      (s.opaque_pixels == 0);

    // ---- 背景色估计（四角均值）----
    const Pixel tl = image.at(0, 0), tr = image.at(s.width - 1, 0);
    const Pixel bl = image.at(0, s.height - 1), br = image.at(s.width - 1, s.height - 1);
    s.bg_estimate.r = static_cast<uint8_t>((tl.r + tr.r + bl.r + br.r) / 4);
    s.bg_estimate.g = static_cast<uint8_t>((tl.g + tr.g + bl.g + br.g) / 4);
    s.bg_estimate.b = static_cast<uint8_t>((tl.b + tr.b + bl.b + br.b) / 4);
    s.bg_uniform = corners_uniform(image);

    // ---- 前景占比（flood fill 背景清理）----
    BackgroundOptions bg;
    bg.threshold = background_threshold;
    bg.has_bg_color = has_bg_color;
    bg.bg_color = bg_color;
    const Mask background = background_mask(image, bg);
    long fg = 0;
    for (int y = 0; y < s.height; ++y)
        for (int x = 0; x < s.width; ++x)
            if (!background.get(x, y)) ++fg;
    s.foreground_pixels = fg;
    s.foreground_percent = static_cast<int>(
        (fg * 100 + s.total_pixels / 2) / s.total_pixels);

    // ---- 连通分量统计（alpha 或背景 mask 取前景）----
    // 无透明通道时用背景清理 mask；否则用 alpha mask
    Mask fg_mask(s.width, s.height);
    if (s.has_transparency) {
        for (int y = 0; y < s.height; ++y)
            for (int x = 0; x < s.width; ++x)
                if (image.at(x, y).a > 1) fg_mask.set(x, y, true);
    } else {
        for (int y = 0; y < s.height; ++y)
            for (int x = 0; x < s.width; ++x)
                if (!background.get(x, y)) fg_mask.set(x, y, true);
    }

    auto comps = connected_components(fg_mask);
    s.component_count = static_cast<int>(comps.size());

    if (!comps.empty()) {
        // 最大分量（按面积）
        auto by_area = comps;
        std::sort(by_area.begin(), by_area.end(),
                  [](const Component& a, const Component& b) { return a.area > b.area; });
        s.largest_component = by_area[0].bounds;
        s.largest_component_area = by_area[0].area;

        // 面积中位数
        std::vector<long> areas;
        areas.reserve(comps.size());
        for (const auto& c : comps) areas.push_back(c.area);
        std::sort(areas.begin(), areas.end());
        const std::size_t n = areas.size();
        s.median_component_area = (n % 2 == 0)
                                      ? (areas[n / 2 - 1] + areas[n / 2]) / 2.0
                                      : static_cast<double>(areas[n / 2]);

        // 推荐 min-size：最大分量边长的 1/4（clamp 2..64），
        // 用于滤除细碎小分量；分量数量少时不做过滤
        if (s.component_count >= 20) {
            s.suggested_min_width =
                std::clamp(s.largest_component.width / 4, 2, 64);
            s.suggested_min_height =
                std::clamp(s.largest_component.height / 4, 2, 64);
        } else {
            s.suggested_min_width = 1;
            s.suggested_min_height = 1;
        }
    }
    return s;
}

}  // namespace sps
