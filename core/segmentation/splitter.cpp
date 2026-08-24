#include "segmentation/splitter.hpp"

#include "mask/mask.hpp"
#include "mask/morphology.hpp"
#include "segmentation/background.hpp"
#include "segmentation/connected_components.hpp"
#include "segmentation/grid_detector.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

namespace sps {

namespace {

// 由 alpha 通道构造前景 mask
Mask alpha_mask(const Image& image, int threshold) {
    Mask mask(image.width(), image.height());
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            mask.set(x, y, image.at(x, y).a > threshold);
        }
    }
    return mask;
}

// 由背景 mask 取反得到前景 mask（背景清理模式）
Mask invert_mask(const Mask& background) {
    Mask fg(background.width(), background.height());
    for (int y = 0; y < background.height(); ++y) {
        for (int x = 0; x < background.width(); ++x) {
            fg.set(x, y, !background.get(x, y));
        }
    }
    return fg;
}

// 应用 padding（向外扩展），并 clamp 回图像边界
SpriteRect with_padding(const SpriteRect& r, int padding, int img_w, int img_h) {
    SpriteRect out = r;
    // 以 right/bottom 计算，避免 x clamp 后 width 不收缩
    int right = r.x + r.width - 1 + padding;
    int bottom = r.y + r.height - 1 + padding;
    out.x = std::max(r.x - padding, 0);
    out.y = std::max(r.y - padding, 0);
    right = std::min(right, img_w - 1);
    bottom = std::min(bottom, img_h - 1);
    out.width = right - out.x + 1;
    out.height = bottom - out.y + 1;
    return out;
}

// 向内收缩 n 像素（类似 PS 收缩）：每边缩 n，最小 1x1
SpriteRect contract_rect(const SpriteRect& r, int n) {
    if (n <= 0) return r;
    SpriteRect out = r;
    out.x += n;
    out.y += n;
    out.width -= n * 2;
    out.height -= n * 2;
    if (out.width < 1) out.width = 1;
    if (out.height < 1) out.height = 1;
    return out;
}

// 统一的 rect 后处理：contract（收缩）→ min-size 过滤 → padding（扩展/clamp）
// 返回 true 表示通过并写入 out
bool finalize_rect(const SpriteRect& in, const SplitOptions& options, int img_w, int img_h,
                   SpriteRect& out) {
    const SpriteRect c = contract_rect(in, options.contract);
    if (c.width < options.min_width || c.height < options.min_height) return false;
    out = with_padding(c, options.padding, img_w, img_h);
    return out.width > 0 && out.height > 0;
}

}  // namespace

SplitResult split_image(const Image& image, const SplitOptions& options) {
    if (image.empty()) return {};
    if (options.alpha_threshold < 0) {
        throw std::invalid_argument("sps: alpha_threshold must be >= 0");
    }
    if (options.merge_nearby && options.merge_distance < 0) {
        throw std::invalid_argument("sps: merge_distance must be >= 0");
    }
    if (options.remove_background && options.background_threshold < 0) {
        throw std::invalid_argument("sps: background_threshold must be >= 0");
    }
    if (options.contract < 0) {
        throw std::invalid_argument("sps: contract must be >= 0");
    }

    SplitResult result;

    // ---- 统一前景 mask 管线：alpha 分割 或 背景清理 ----
    // Grid/CCL/Auto 共用同一 mask，保证各模式对同一素材语义一致
    Mask mask;
    if (options.remove_background) {
        BackgroundOptions bg;
        bg.threshold = options.background_threshold;
        bg.has_bg_color = options.has_bg_color;
        bg.bg_color = options.bg_color;
        mask = invert_mask(background_mask(image, bg));
    } else {
        mask = alpha_mask(image, options.alpha_threshold);
    }
    if (!mask.any_foreground()) return result;

    // ---- Grid / Auto 模式：直接按网格出 rects ----
    if (options.mode == DetectionMode::Grid ||
        options.mode == DetectionMode::Auto) {
        int cell = options.grid_cell_size;
        if (options.mode == DetectionMode::Auto) {
            cell = auto_detect_grid_size(mask);
            if (cell > 0) {
                // 检测到稳定网格 → 按网格切
                auto rects = grid_detect(mask, cell);
                for (const auto& r : rects) {
                    SpriteRect p;
                    if (finalize_rect(r, options, image.width(), image.height(), p)) {
                        result.sprites.push_back(p);
                    }
                }
                return result;
            }
            // 未检测到网格 → 回退到 connected components（fallthrough）
        } else {
            auto rects = grid_detect(mask, cell);
            for (const auto& r : rects) {
                SpriteRect p;
                if (finalize_rect(r, options, image.width(), image.height(), p)) {
                    result.sprites.push_back(p);
                }
            }
            return result;
        }
    }

    // ---- Merge 模式：膨胀 mask → CCL → 用原 mask 重算精确 bbox（腐蚀回原边界） ----
    if (options.merge_nearby) {
        Mask dilated = dilate(mask, options.merge_distance);
        auto dilated_comps = connected_components(dilated);
        for (const auto& c : dilated_comps) {
            // 在原 mask 上重算该分量区域的精确 bbox
            int min_x = std::numeric_limits<int>::max();
            int min_y = std::numeric_limits<int>::max();
            int max_x = -1, max_y = -1;
            const SpriteRect& b = c.bounds;
            for (int y = b.y; y < b.y + b.height; ++y) {
                for (int x = b.x; x < b.x + b.width; ++x) {
                    if (mask.get(x, y)) {
                        min_x = std::min(min_x, x);
                        min_y = std::min(min_y, y);
                        max_x = std::max(max_x, x);
                        max_y = std::max(max_y, y);
                    }
                }
            }
            if (max_x < 0) continue;  // 该区域在原 mask 无前景
            SpriteRect real;
            real.x = min_x;
            real.y = min_y;
            real.width = max_x - min_x + 1;
            real.height = max_y - min_y + 1;
            SpriteRect p;
            if (finalize_rect(real, options, image.width(), image.height(), p)) {
                result.sprites.push_back(p);
            }
        }
        return result;
    }

    // ---- 普通 CCL ----
    auto comps = connected_components(mask);

    for (const Component& c : comps) {
        SpriteRect p;
        if (finalize_rect(c.bounds, options, image.width(), image.height(), p)) {
            result.sprites.push_back(p);
        }
    }
    return result;
}

}  // namespace sps
