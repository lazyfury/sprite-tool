#include "export/sheet.hpp"

#include "mask/mask_io.hpp"

#include <algorithm>
#include <stdexcept>

namespace sps {

std::vector<Image> crop_sprites(const Image& source,
                                const std::vector<SpriteRect>& rects,
                                const std::vector<std::vector<uint8_t>>& masks) {
    std::vector<Image> out;
    out.reserve(rects.size());
    for (std::size_t i = 0; i < rects.size(); ++i) {
        const auto& r = rects[i];
        Image cropped = source.cropped(r.x, r.y, r.width, r.height);
        if (i < masks.size() && !masks[i].empty()) {
            apply_mask(cropped, masks[i]);
        }
        out.push_back(std::move(cropped));
    }
    return out;
}

Image repack_sheet(const std::vector<Image>& sprites, int cols, int padding,
                   std::vector<SpriteRect>& out_rects) {
    out_rects.clear();
    if (sprites.empty()) return Image();
    if (cols <= 0) cols = 1;

    // 格尺寸 = 最大精灵尺寸 + padding
    int cell_w = 0, cell_h = 0;
    for (const auto& s : sprites) {
        cell_w = std::max(cell_w, s.width());
        cell_h = std::max(cell_h, s.height());
    }
    cell_w += padding * 2;
    cell_h += padding * 2;

    const int rows = (static_cast<int>(sprites.size()) + cols - 1) / cols;
    Image sheet(static_cast<int>(cols) * cell_w, rows * cell_h, 0);

    for (std::size_t i = 0; i < sprites.size(); ++i) {
        const auto& s = sprites[i];
        const int cell_x = static_cast<int>(i % cols) * cell_w;
        const int cell_y = static_cast<int>(i / cols) * cell_h;
        const int ox = cell_x + padding + (cell_w - padding * 2 - s.width()) / 2;
        const int oy = cell_y + padding + (cell_h - padding * 2 - s.height()) / 2;

        // 逐像素拷贝（RGBA 直接覆盖；sheet 初始全透明）
        for (int y = 0; y < s.height(); ++y) {
            for (int x = 0; x < s.width(); ++x) {
                const Pixel p = s.at(x, y);
                Pixel& dst = sheet.at(ox + x, oy + y);
                dst = p;
            }
        }
        out_rects.push_back({ox, oy, s.width(), s.height()});
    }
    return sheet;
}

}  // namespace sps
