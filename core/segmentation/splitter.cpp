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

// 统一的 rect 后处理：min-size 过滤 → clamp 回图像边界
// 返回 true 表示通过并写入 out
bool finalize_rect(const SpriteRect& in, const SplitOptions& options, int img_w, int img_h,
                   SpriteRect& out) {
    if (in.width < options.min_width || in.height < options.min_height) return false;
    out = in;
    if (out.x < 0) {
        out.width += out.x;
        out.x = 0;
    }
    if (out.y < 0) {
        out.height += out.y;
        out.y = 0;
    }
    out.width = std::min(out.width, img_w - out.x);
    out.height = std::min(out.height, img_h - out.y);
    return out.width > 0 && out.height > 0;
}

}  // namespace

SplitResult split_image(const Image& image, const SplitOptions& options,
                        const Mask* bg_mask) {
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
    if (options.contract > 0 && !options.remove_background) {
        // 自由选区收缩属于背景清理流程（剪切清理产生的毛边）
        throw std::invalid_argument("sps: contract requires remove_background");
    }
    if (options.contract > 0 && options.mode == DetectionMode::Grid) {
        // 网格模式没有自由选区概念，收缩无效
        throw std::invalid_argument("sps: contract requires components-based mode");
    }

    SplitResult result;

    // ---- 统一前景 mask 管线：alpha 分割 或 背景清理 ----
    // Grid/CCL/Auto 共用同一 mask，保证各模式对同一素材语义一致
    Mask mask;
    if (options.remove_background) {
        if (bg_mask != nullptr) {
            // 外部背景 mask（remote/AI 后端）：必须与原图同尺寸
            if (bg_mask->width() != image.width() || bg_mask->height() != image.height()) {
                throw std::invalid_argument(
                    "sps: split_image: external bg_mask size mismatch");
            }
            mask = invert_mask(*bg_mask);
        } else {
            BackgroundOptions bg;
            bg.threshold = options.background_threshold;
            bg.has_bg_color = options.has_bg_color;
            bg.bg_color = options.bg_color;
            bg.edge_passes = options.edge_passes;
            mask = invert_mask(background_mask(image, bg));
        }
        // 自由选区收缩：对前景轮廓向内腐蚀 N 圈（剪切毛边）后，
        // 后续 CCL/merge 的 bbox 自动收紧到腐蚀后的轮廓
        if (options.contract > 0) mask = erode(mask, options.contract);
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
                // 检测到稳定网格：没有自由选区概念，contract 在此分支无意义
                if (options.contract > 0) {
                    throw std::invalid_argument(
                        "sps: contract requires components-based mode");
                }
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
