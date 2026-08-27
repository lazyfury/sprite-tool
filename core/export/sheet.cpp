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
                   std::vector<SpriteRect>& out_rects, int fixed_w, int fixed_h) {
    out_rects.clear();
    if (sprites.empty()) return Image();
    if (cols <= 0) cols = 1;

    // 格尺寸：显式指定（fixed_w/fixed_h > 0，padding 忽略）或自适应（最大精灵 + 2*padding）
    const bool fixed = fixed_w > 0 && fixed_h > 0;
    int cell_w = 0, cell_h = 0;
    for (const auto& s : sprites) {
        cell_w = std::max(cell_w, s.width());
        cell_h = std::max(cell_h, s.height());
    }
    if (fixed) {
        cell_w = fixed_w;
        cell_h = fixed_h;
    } else {
        cell_w += padding * 2;
        cell_h += padding * 2;
    }

    const int rows = (static_cast<int>(sprites.size()) + cols - 1) / cols;
    Image sheet(static_cast<int>(cols) * cell_w, rows * cell_h, 0);

    for (std::size_t i = 0; i < sprites.size(); ++i) {
        const auto& s = sprites[i];
        const int cell_x = static_cast<int>(i % cols) * cell_w;
        const int cell_y = static_cast<int>(i / cols) * cell_h;
        // 居中放置（固定格时 padding 并入居中，不再额外留边）
        const int ox = cell_x + (cell_w - s.width()) / 2;
        const int oy = cell_y + (cell_h - s.height()) / 2;
        // 可见区域：精灵与格子的交集（固定格且精灵超格时裁剪；自适应无裁剪等价原尺寸）
        const int vx0 = std::max(cell_x, ox);
        const int vy0 = std::max(cell_y, oy);
        const int vx1 = std::min(cell_x + cell_w, ox + s.width());
        const int vy1 = std::min(cell_y + cell_h, oy + s.height());
        if (vx1 > vx0 && vy1 > vy0) {
            for (int y = vy0; y < vy1; ++y) {
                for (int x = vx0; x < vx1; ++x) {
                    const Pixel p = s.at(x - ox, y - oy);
                    Pixel& dst = sheet.at(x, y);
                    dst = p;
                }
            }
            out_rects.push_back({vx0, vy0, vx1 - vx0, vy1 - vy0});
        } else {
            out_rects.push_back({ox, oy, 0, 0});   // 完全不可见（格>0 时理论不发生）
        }
    }
    return sheet;
}

}  // namespace sps
