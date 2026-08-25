#include "segmentation/auto_detector.hpp"

#include <algorithm>
#include <map>
#include <utility>
#include <vector>

namespace sps {

namespace {

// 从已算好的 CCL 结果过滤（避免重复 CCL）：min_width/min_height/min_pixels
std::vector<ComponentSprite> filter_components(const std::vector<Component>& raw,
                                               int min_width, int min_height,
                                               int min_pixels) {
    std::vector<ComponentSprite> out;
    out.reserve(raw.size());
    for (const auto& c : raw) {
        if (c.bounds.width < min_width) continue;
        if (c.bounds.height < min_height) continue;
        if (c.area < min_pixels) continue;
        ComponentSprite s;
        s.bounds = c.bounds;
        s.area = c.area;
        s.cx = c.bounds.x + c.bounds.width / 2;
        s.cy = c.bounds.y + c.bounds.height / 2;
        out.push_back(s);
    }
    return out;
}

// bbox 外扩 d px
SpriteRect expand_rect(const SpriteRect& r, int d) {
    SpriteRect e;
    e.x = r.x - d;
    e.y = r.y - d;
    e.width = r.width + 2 * d;
    e.height = r.height + 2 * d;
    return e;
}

bool rect_intersects(const SpriteRect& a, const SpriteRect& b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height &&
           b.y < a.y + a.height;
}

// 组件合并：膨胀 bbox 相交即 union（O(n²)，组件数通常几十个）。合并组 bbox = 外接框，
// 面积 = 成员面积和。典型场景：角色 + 相邻武器/装饰合成一个整体。
std::vector<ComponentSprite> merge_components(std::vector<ComponentSprite> comps,
                                              int merge_distance) {
    const int n = static_cast<int>(comps.size());
    if (n < 2 || merge_distance <= 0) return comps;

    std::vector<int> parent(n);
    for (int i = 0; i < n; ++i) parent[i] = i;
    const auto find = [&](const auto& self, int x) -> int {
        if (parent[x] != x) parent[x] = self(self, parent[x]);
        return parent[x];
    };
    const auto unite = [&](int a, int b) {
        const int ra = find(find, a);
        const int rb = find(find, b);
        if (ra != rb) parent[ra] = rb;
    };

    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (rect_intersects(expand_rect(comps[i].bounds, merge_distance),
                                expand_rect(comps[j].bounds, merge_distance))) {
                unite(i, j);
            }
        }
    }

    std::vector<std::vector<int>> groups(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) groups[static_cast<std::size_t>(find(find, i))].push_back(i);

    std::vector<ComponentSprite> out;
    out.reserve(n);
    for (const auto& g : groups) {
        if (g.empty()) continue;
        ComponentSprite s;
        s.bounds = comps[static_cast<std::size_t>(g[0])].bounds;
        s.area = 0;
        for (int idx : g) {
            const auto& b = comps[static_cast<std::size_t>(idx)].bounds;
            const int max_x = std::max(s.bounds.x + s.bounds.width, b.x + b.width);
            const int max_y = std::max(s.bounds.y + s.bounds.height, b.y + b.height);
            s.bounds.x = std::min(s.bounds.x, b.x);
            s.bounds.y = std::min(s.bounds.y, b.y);
            s.bounds.width = max_x - s.bounds.x;
            s.bounds.height = max_y - s.bounds.y;
            s.area += comps[static_cast<std::size_t>(idx)].area;
        }
        s.cx = s.bounds.x + s.bounds.width / 2;
        s.cy = s.bounds.y + s.bounds.height / 2;
        out.push_back(s);
    }
    std::sort(out.begin(), out.end(),
              [](const ComponentSprite& a, const ComponentSprite& b) {
                  if (a.bounds.y != b.bounds.y) return a.bounds.y < b.bounds.y;
                  return a.bounds.x < b.bounds.x;
              });
    return out;
}

// padding 外扩后 clamp 到组件中心所在 cell 的边界（防止吃到邻居 cell 的内容）
SpriteRect clamp_to_cell(const SpriteRect& r, const GridCandidate& g) {
    if (g.period_x <= 0 || g.period_y <= 0) return r;
    const int cx = component_cell_index(r.x + r.width / 2, g.offset_x, g.period_x, g.columns);
    const int cy = component_cell_index(r.y + r.height / 2, g.offset_y, g.period_y, g.rows);
    const int cell_x0 = g.offset_x + cx * g.period_x;
    const int cell_y0 = g.offset_y + cy * g.period_y;
    SpriteRect out = r;
    out.x = std::max(out.x, cell_x0);
    out.y = std::max(out.y, cell_y0);
    out.width = std::min(out.x + out.width, cell_x0 + g.period_x) - out.x;
    out.height = std::min(out.y + out.height, cell_y0 + g.period_y) - out.y;
    return out;
}

}  // namespace

AutoDetection auto_detect(const Mask& mask, const AutoOptions& options) {
    AutoDetection det;
    if (mask.empty()) return det;

    // ---- 1. 组件：CCL → 过滤 → 合并 ----
    const auto raw = connected_components(mask);
    det.raw_component_count = static_cast<int>(raw.size());
    // 用户未显式指定 min-size（均默认 1）且组件较多时，自动推导噪声过滤阈值：
    // 最大组件边长的 1/4（clamp 2..64），与 analyzer 的 suggested_min_* 同一启发式。
    // 典型场景：白底素材表去背景后产生数千个 1~2px 抗锯齿碎片。
    // 用户显式传了任一 min-size 时不干预，尊重用户选择。
    int eff_min_w = options.min_width;
    int eff_min_h = options.min_height;
    if (eff_min_w <= 1 && eff_min_h <= 1 && raw.size() >= 20) {
        const Component* largest = &raw[0];
        for (const auto& c : raw) {
            if (c.area > largest->area) largest = &c;
        }
        eff_min_w = std::clamp(largest->bounds.width / 4, 2, 64);
        eff_min_h = std::clamp(largest->bounds.height / 4, 2, 64);
    }
    // 面积下限：min_width/min_height 的 1% 但至少 16（滤掉抗锯齿碎片/噪点）
    const int min_pixels = std::max(16, eff_min_w * eff_min_h / 100);
    auto comps = filter_components(raw, eff_min_w, eff_min_h, min_pixels);
    det.filtered_component_count = static_cast<int>(comps.size());
    if (options.merge_distance > 0) {
        comps = merge_components(std::move(comps), options.merge_distance);
    }
    det.merged_component_count = static_cast<int>(comps.size());
    det.components = comps;
    if (comps.empty()) return det;  // 无精灵 → Components，rects 空

    // ---- 2. Grid 检测（复用已过滤组件，不再重复 CCL） ----
    det.grid = detect_grid(mask, options.min_grid_size, options.max_grid_size, &comps);
    det.confidence = det.grid.confidence;

    // ---- 3. 决策 ----
    if (!det.grid.is_grid) {
        det.mode = AutoSliceMode::Components;
    } else {
        const GridCandidate& g = det.grid.best;
        det.grid_columns = g.columns;
        det.grid_rows = g.rows;
        det.cell_occupancy = g.cell_occupancy;
        det.occupied_cells = g.occupied_cells;

        // per-cell 分组统计（主组件 = cell 内面积最大）
        std::map<int, int> per_cell;
        for (const auto& c : comps) {
            const int cx = component_cell_index(c.cx, g.offset_x, g.period_x, g.columns);
            const int cy = component_cell_index(c.cy, g.offset_y, g.period_y, g.rows);
            ++per_cell[cy * g.columns + cx];
        }
        for (const auto& kv : per_cell) {
            if (kv.second == 1) {
                ++det.cells_with_single;
            } else {
                ++det.cells_with_multi;
            }
        }

        // 组件 bbox ≈ cell？（宽高均 >= 85% cell 尺寸 → sprite 本身就是 cell）
        bool bbox_like_cell = true;
        for (const auto& c : comps) {
            if (c.bounds.width * 20 < g.period_x * 17 ||
                c.bounds.height * 20 < g.period_y * 17) {
                bbox_like_cell = false;
                break;
            }
        }

        if (g.score >= 0.8 && bbox_like_cell && static_cast<int>(comps.size()) >= 4) {
            det.mode = AutoSliceMode::Grid;
        } else if (det.cells_with_multi == 0 && static_cast<int>(comps.size()) >= 3) {
            det.mode = AutoSliceMode::ComponentsInGrid;
        } else {
            det.mode = AutoSliceMode::Components;
        }
    }

    // ---- 3.5 用户强制策略覆盖（UI「切割策略」；决策后、生成 rects 前） ----
    switch (options.slice_policy) {
        case SlicePolicy::Components:
            det.mode = AutoSliceMode::Components;
            break;
        case SlicePolicy::Grid:
            det.mode = AutoSliceMode::Grid;
            break;
        default:
            break;  // SlicePolicy::Auto：保持自动决策
    }

    // ---- 4. 生成 rects ----
    const GridCandidate& g = det.grid.best;
    switch (det.mode) {
        case AutoSliceMode::Grid:
            det.rects = grid_detect(mask, g.period_x, g.period_y, g.offset_x, g.offset_y,
                                    options.min_opaque_pixels);
            break;
        case AutoSliceMode::ComponentsInGrid: {
            // 每 cell 一个主组件（面积最大）→ rect（padding 后 clamp 到 cell）
            std::map<int, const ComponentSprite*> main_per_cell;
            for (const auto& c : comps) {
                const int cx = component_cell_index(c.cx, g.offset_x, g.period_x, g.columns);
                const int cy = component_cell_index(c.cy, g.offset_y, g.period_y, g.rows);
                const int lin = cy * g.columns + cx;
                auto it = main_per_cell.find(lin);
                if (it == main_per_cell.end() || c.area > it->second->area) {
                    main_per_cell[lin] = &c;
                }
            }
            // 按 cell 线性索引排序输出（等价于行优先）
            std::vector<std::pair<int, const ComponentSprite*>> items(main_per_cell.begin(),
                                                                      main_per_cell.end());
            std::sort(items.begin(), items.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
            for (const auto& [lin, c] : items) {
                (void)lin;
                SpriteRect r = c->bounds;
                if (options.padding > 0) {
                    r = clamp_to_cell(expand_rect(r, options.padding), g);
                }
                det.rects.push_back(r);
            }
            break;
        }
        case AutoSliceMode::Components:
        default:
            for (const auto& c : comps) {
                det.rects.push_back(c.bounds);
            }
            break;
    }
    return det;
}

}  // namespace sps
