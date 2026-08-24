#include "mask/morphology.hpp"

#include <algorithm>

namespace sps {

Mask dilate(const Mask& mask, int radius) {
    const int w = mask.width();
    const int h = mask.height();
    if (w <= 0 || h <= 0 || radius <= 0) return mask;

    Mask out(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!mask.get(x, y)) continue;
            // 曼哈顿距离膨胀：|dx| + |dy| <= radius（菱形），与 merge_distance 语义一致
            for (int dy = -radius; dy <= radius; ++dy) {
                const int yy = y + dy;
                if (yy < 0 || yy >= h) continue;
                const int span = radius - std::abs(dy);
                for (int dx = -span; dx <= span; ++dx) {
                    const int xx = x + dx;
                    if (xx >= 0 && xx < w) out.set(xx, yy, true);
                }
            }
        }
    }
    return out;
}

}  // namespace sps
