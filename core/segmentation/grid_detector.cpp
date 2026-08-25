#include "segmentation/grid_detector.hpp"

#include "segmentation/connected_components.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace sps {

namespace {

// cell 非空的最小前景像素数（Auto 评分路径；Grid 模式显式切割仍用 >=1）
constexpr int kMinOpaqueCellPixels = 16;

// floor 除法（b > 0）：C++ 整数除法向零截断，负被除数需向下取整
int floor_div(int a, int b) {
    const int q = a / b;
    const int r = a % b;
    return (r != 0 && a < 0) ? q - 1 : q;
}

// 统计 (x0,y0)-(x1,y1) 区域内的前景像素（边界自动 clamp 到图像）
int cell_foreground_count(const Mask& mask, int x0, int y0, int x1, int y1) {
    x0 = std::max(x0, 0);
    y0 = std::max(y0, 0);
    x1 = std::min(x1, mask.width());
    y1 = std::min(y1, mask.height());
    int count = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            if (mask.get(x, y)) ++count;
        }
    }
    return count;
}

// 网格几何统计：覆盖行列数、前景占用 cell 数、平均前景占比
struct GridStats {
    int rows = 0;
    int columns = 0;
    int occupied = 0;
    double avg_foreground_ratio = 0;
};

GridStats grid_stats(const Mask& mask, int pw, int ph, int ox, int oy, int min_opaque) {
    GridStats st;
    const int w = mask.width();
    const int h = mask.height();
    if (pw <= 0 || ph <= 0 || w <= 0 || h <= 0) return st;
    const int k0 = floor_div(-ox, pw);
    const int k1 = floor_div(w - 1 - ox, pw);
    const int l0 = floor_div(-oy, ph);
    const int l1 = floor_div(h - 1 - oy, ph);
    st.columns = k1 - k0 + 1;
    st.rows = l1 - l0 + 1;
    long total_fg = 0;
    long covered = 0;
    for (int ky = l0; ky <= l1; ++ky) {
        for (int kx = k0; kx <= k1; ++kx) {
            const int x0 = ox + kx * pw;
            const int y0 = oy + ky * ph;
            const int x1 = std::min(x0 + pw, w);
            const int y1 = std::min(y0 + ph, h);
            const int cx0 = std::max(x0, 0);
            const int cy0 = std::max(y0, 0);
            if (cx0 >= x1 || cy0 >= y1) continue;
            const int fg = cell_foreground_count(mask, x0, y0, x1, y1);
            total_fg += fg;
            covered += static_cast<long>(x1 - cx0) * (y1 - cy0);
            if (fg >= min_opaque) ++st.occupied;
        }
    }
    st.avg_foreground_ratio = covered > 0 ? static_cast<double>(total_fg) / covered : 0.0;
    return st;
}

// 计算一行投影（水平方向时按列统计，否则按行），返回投影数组
std::vector<double> make_projection(const Mask& mask, bool horizontal) {
    const int w = mask.width();
    const int h = mask.height();
    const int len = horizontal ? w : h;
    const int other = horizontal ? h : w;
    std::vector<double> proj(len, 0.0);
    for (int i = 0; i < len; ++i) {
        double sum = 0;
        for (int j = 0; j < other; ++j) {
            sum += horizontal ? (mask.get(i, j) ? 1 : 0) : (mask.get(j, i) ? 1 : 0);
        }
        proj[i] = sum;
    }
    return proj;
}

// Pearson 自相关：C(d) = Σ (p[i]-μ)(p[i+d]-μ) / Σ (p[i]-μ)²
// C(0)=1，周期处出现峰值。O(N×L)。
std::vector<double> autocorrelation(const std::vector<double>& p, int max_lag) {
    const int n = static_cast<int>(p.size());
    std::vector<double> out(static_cast<std::size_t>(max_lag) + 1, 0.0);
    if (n < 2) return out;

    double mean = 0;
    for (double v : p) mean += v;
    mean /= n;

    double denom = 0;
    for (double v : p) denom += (v - mean) * (v - mean);
    if (denom < 1e-9) return out;  // 无变化（全 0 或全同）

    for (int d = 1; d <= max_lag; ++d) {
        double num = 0;
        for (int i = 0; i + d < n; ++i) {
            num += (p[i] - mean) * (p[i + d] - mean);
        }
        out[static_cast<std::size_t>(d)] = num / denom;
    }
    return out;
}

// 从自相关找局部峰值（周期候选）：C(d) 局部最大且超过阈值
std::vector<int> find_peaks(const std::vector<double>& corr, int min_d, int max_d,
                            double threshold) {
    std::vector<int> peaks;
    for (int d = min_d; d <= max_d; ++d) {
        const double c = corr[static_cast<std::size_t>(d)];
        const double prev = corr[static_cast<std::size_t>(d - 1)];
        const double next =
            d + 1 <= max_d ? corr[static_cast<std::size_t>(d + 1)] : c - 1.0;
        if (c >= threshold && c >= prev && c > next) {
            peaks.push_back(d);
        }
    }
    return peaks;
}

// 谐波抑制：若 candidate 是更小周期 p 的整数倍（candidate = k*p）且 p 得分更高，
// 说明 candidate 是 p 的谐波 → 标记应降权。
// 简化：对候选列表按「峰值强度」过滤，保留局部最强的非谐波周期。
struct PeriodCandidate {
    int period;
    double strength;
};

std::vector<PeriodCandidate> pick_periods(const std::vector<int>& peaks,
                                          const std::vector<double>& corr, int max_periods) {
    std::vector<PeriodCandidate> cands;
    for (int p : peaks) {
        cands.push_back({p, corr[static_cast<std::size_t>(p)]});
    }
    // 按强度降序
    std::sort(cands.begin(), cands.end(),
              [](const PeriodCandidate& a, const PeriodCandidate& b) {
                  return a.strength > b.strength;
              });

    // 谐波抑制：保留强周期，若后续候选约等于已保留周期的整数倍（±5%）→ 跳过
    std::vector<PeriodCandidate> out;
    for (const auto& c : cands) {
        bool harmonic = false;
        for (const auto& keep : out) {
            if (keep.period <= 0) continue;
            // c ≈ k * keep.period（k>=2），允许 5% 偏差（真实素材周期非精确整数倍）
            const double ratio = static_cast<double>(c.period) / keep.period;
            const double nearest_int = std::round(ratio);
            if (nearest_int >= 2.0 &&
                std::abs(ratio - nearest_int) / nearest_int <= 0.05) {
                harmonic = true;
                break;
            }
        }
        if (!harmonic) {
            out.push_back(c);
        }
        if (static_cast<int>(out.size()) >= max_periods) break;
    }
    // 按 period 升序返回
    std::sort(out.begin(), out.end(),
              [](const PeriodCandidate& a, const PeriodCandidate& b) {
                  return a.period < b.period;
              });
    return out;
}

// 对给定周期，搜索最佳偏移：使组件中心尽可能落在 cell 中心。
// 返回该偏移下的对齐得分（0~1）。
struct AlignmentResult {
    int offset = 0;
    double score = 0.0;
};

// 组件列表：中心坐标 + bbox
struct Comp {
    int cx, cy;
    SpriteRect bounds;
};

// 搜索一个方向的偏移（center 坐标数组，周期 period）
AlignmentResult best_offset(const std::vector<int>& centers, int period) {
    AlignmentResult best{0, -1.0};
    if (centers.empty() || period <= 0) return best;

    for (int off = 0; off < period; ++off) {
        double sum = 0;
        // 组件中心到最近 cell 中心的距离（归一化到 cell 半径）
        for (int c : centers) {
            // cell 中心位置：off + period/2 + k*period
            const double cell_half = period / 2.0;
            const double first_center = off + period / 2.0;
            double diff = std::abs(c - first_center);
            diff = std::fmod(diff, static_cast<double>(period));
            if (diff > cell_half) diff = period - diff;
            // 对齐得分：距中心越近越高
            sum += 1.0 - diff / cell_half;
        }
        const double score = sum / centers.size();
        if (score > best.score) {
            best = {off, score};
        }
    }
    // 修正：模周期对齐存在多个等价解（组件中心 c 与 c+period 对齐得分相同）。
    // 平移整数个周期，使第一个 cell 中心对齐最小组件中心——几何合理，
    // 避免稀疏组件（如 4 个 10x10 块在 16px 网格上）被映射进错误的 cell 实例
    // （全部挤进同一个 cell，导致 Component→Cell mapping 失效）。
    int min_c = centers.front();
    for (int c : centers) min_c = std::min(min_c, c);
    const double first_center = best.offset + period / 2.0;
    const int shift = static_cast<int>(std::round((min_c - first_center) / period));
    best.offset += shift * period;
    // 负偏移产生的图外 cell 由 grid_detect/mapping 的 floor_div + clamp 处理
    return best;
}

// 计算组件尺寸一致性：ratio = comp_size / cell_size 的变异系数（1 - cv）
// 只统计「较大组件」（>= 最大尺寸的 30%），排除文字/装饰碎片干扰
// 返回 (score, 参与统计的组件数)
double size_consistency(const std::vector<Comp>& comps, int cell_w, int cell_h) {
    if (comps.size() < 3 || cell_w <= 0 || cell_h <= 0) return 0.0;

    // 大组件过滤：宽高都 >= 最大尺寸的 30%
    int max_w = 0, max_h = 0;
    for (const auto& c : comps) {
        max_w = std::max(max_w, c.bounds.width);
        max_h = std::max(max_h, c.bounds.height);
    }
    std::vector<const Comp*> big;
    for (const auto& c : comps) {
        if (c.bounds.width * 10 >= max_w * 3 && c.bounds.height * 10 >= max_h * 3) {
            big.push_back(&c);
        }
    }
    if (big.size() < 3) return 0.0;

    double sum_rw = 0, sum_rh = 0;
    for (const auto* c : big) {
        sum_rw += static_cast<double>(c->bounds.width) / cell_w;
        sum_rh += static_cast<double>(c->bounds.height) / cell_h;
    }
    const double mw = sum_rw / big.size();
    const double mh = sum_rh / big.size();
    double var_w = 0, var_h = 0;
    for (const auto* c : big) {
        const double rw = static_cast<double>(c->bounds.width) / cell_w;
        const double rh = static_cast<double>(c->bounds.height) / cell_h;
        var_w += (rw - mw) * (rw - mw);
        var_h += (rh - mh) * (rh - mh);
    }
    var_w /= big.size();
    var_h /= big.size();
    const double cv_w = mw > 1e-9 ? std::sqrt(var_w) / mw : 0;
    const double cv_h = mh > 1e-9 ? std::sqrt(var_h) / mh : 0;
    const double cv = (cv_w + cv_h) / 2;
    return std::clamp(1.0 - cv, 0.0, 1.0);
}

}  // namespace

// 组件中心 → cell 索引（floor 语义 + clamp 到 [0, size)）。
// 与 detect_grid 内部 mapping 共用同一语义（floor_div），供 Auto 管线生成
// ComponentsInGrid rect / 排序时复现组件到 cell 的归属。
int component_cell_index(int center, int offset, int period, int size) {
    if (period <= 0) return -1;
    const int idx = floor_div(center - offset, period);
    if (idx < 0) return 0;
    if (idx >= size) return size - 1;
    return idx;
}

std::vector<SpriteRect> grid_detect(const Mask& mask, int cell_width, int cell_height,
                                    int offset_x, int offset_y, int min_opaque) {
    std::vector<SpriteRect> result;
    if (mask.empty() || cell_width <= 0 || cell_height <= 0) return result;

    const int w = mask.width();
    const int h = mask.height();
    const int k0 = floor_div(-offset_x, cell_width);
    const int k1 = floor_div(w - 1 - offset_x, cell_width);
    const int l0 = floor_div(-offset_y, cell_height);
    const int l1 = floor_div(h - 1 - offset_y, cell_height);

    for (int ky = l0; ky <= l1; ++ky) {
        for (int kx = k0; kx <= k1; ++kx) {
            const int x0 = offset_x + kx * cell_width;
            const int y0 = offset_y + ky * cell_height;
            const int x1 = std::min(x0 + cell_width, w);
            const int y1 = std::min(y0 + cell_height, h);
            const int cx0 = std::max(x0, 0);
            const int cy0 = std::max(y0, 0);
            if (cx0 >= x1 || cy0 >= y1) continue;  // cell 完全在图外
            if (cell_foreground_count(mask, x0, y0, x1, y1) < min_opaque) continue;
            SpriteRect r;
            r.x = cx0;
            r.y = cy0;
            r.width = x1 - cx0;
            r.height = y1 - cy0;
            result.push_back(r);
        }
    }
    return result;
}

GridDetection detect_grid(const Mask& mask, int min_cell, int max_cell,
                          const std::vector<ComponentSprite>* components) {
    GridDetection result;
    if (mask.empty()) return result;

    const int w = mask.width();
    const int h = mask.height();
    if (max_cell <= 0) max_cell = std::min(w, h) / 2;
    max_cell = std::clamp(max_cell, min_cell, std::min(w, h) / 2);
    if (max_cell < min_cell || max_cell < 4) return result;

    // ---- 1. 投影 + 自相关 ----
    const auto proj_x = make_projection(mask, true);
    const auto proj_y = make_projection(mask, false);
    const auto corr_x = autocorrelation(proj_x, max_cell);
    const auto corr_y = autocorrelation(proj_y, max_cell);

    // ---- 2. 峰值 → 候选周期（谐波抑制） ----
    const double peak_threshold = 0.10;  // 低阈值先收，评分再筛
    const auto peaks_x = find_peaks(corr_x, min_cell, max_cell, peak_threshold);
    const auto peaks_y = find_peaks(corr_y, min_cell, max_cell, peak_threshold);
    const auto periods_x = pick_periods(peaks_x, corr_x, 5);
    const auto periods_y = pick_periods(peaks_y, corr_y, 5);
    if (periods_x.empty() || periods_y.empty()) return result;

    // ---- 3. 组件（用于对齐/边界/尺寸评分 + Component→Cell mapping）----
    // 外部组件（Auto 管线已过滤）优先；否则内部 CCL + 大组件过滤（>= 最大尺寸 30%）
    std::vector<ComponentSprite> owned;
    const std::vector<ComponentSprite>* src = components;
    if (src == nullptr || src->empty()) {
        const auto comps_all = connected_components(mask);
        int max_w = 0, max_h = 0;
        for (const auto& c : comps_all) {
            max_w = std::max(max_w, c.bounds.width);
            max_h = std::max(max_h, c.bounds.height);
        }
        owned.reserve(comps_all.size());
        for (const auto& c : comps_all) {
            if (c.bounds.width * 10 < max_w * 3 || c.bounds.height * 10 < max_h * 3) continue;
            if (c.bounds.width < min_cell / 2 || c.bounds.height < min_cell / 2) continue;
            ComponentSprite s;
            s.bounds = c.bounds;
            s.area = c.area;
            s.cx = c.bounds.x + c.bounds.width / 2;
            s.cy = c.bounds.y + c.bounds.height / 2;
            owned.push_back(s);
        }
        src = &owned;
    }
    std::vector<Comp> comps;
    comps.reserve(src->size());
    for (const auto& c : *src) {
        comps.push_back({c.cx, c.cy, c.bounds});
    }
    const int n_comps = static_cast<int>(comps.size());
    if (n_comps < 3) return result;  // 组件太少无法验证

    std::vector<int> centers_x, centers_y;
    for (const auto& c : comps) {
        centers_x.push_back(c.cx);
        centers_y.push_back(c.cy);
    }

    // ---- 4. 组合候选：X 周期 × Y 周期，各自最佳偏移 ----
    for (const auto& px : periods_x) {
        for (const auto& py : periods_y) {
            GridCandidate cand;
            cand.period_x = px.period;
            cand.period_y = py.period;

            // 偏移搜索（组件中心对齐 cell 中心）
            const auto ax = best_offset(centers_x, px.period);
            const auto ay = best_offset(centers_y, py.period);
            cand.offset_x = ax.offset;
            cand.offset_y = ay.offset;

            // ---- 5. 多维评分 ----
            // periodicity：自相关峰值强度（两方向平均）
            cand.periodicity =
                (corr_x[static_cast<std::size_t>(px.period)] +
                 corr_y[static_cast<std::size_t>(py.period)]) / 2.0;

            // alignment：组件中心到 cell 中心的对齐（两方向平均）
            cand.alignment = (ax.score + ay.score) / 2.0;

            // boundary：组件 bbox 是否完整落在单个 cell 内（不跨越任何网格线）
            double boundary_ok = 0;
            for (const auto& c : comps) {
                const int left_cell = floor_div(c.bounds.x - cand.offset_x, px.period);
                const int right_cell =
                    floor_div(c.bounds.x + c.bounds.width - 1 - cand.offset_x, px.period);
                const int top_cell = floor_div(c.bounds.y - cand.offset_y, py.period);
                const int bot_cell =
                    floor_div(c.bounds.y + c.bounds.height - 1 - cand.offset_y, py.period);
                const bool inside_one_cell =
                    (left_cell == right_cell) && (top_cell == bot_cell);
                if (inside_one_cell) ++boundary_ok;
            }
            cand.boundary = boundary_ok / n_comps;

            // size_consistency
            cand.size_consistency = size_consistency(comps, px.period, py.period);

            // ---- 网格几何统计（修正：不再 max(px,py) 压方形，用真实 period + offset）----
            const GridStats st = grid_stats(mask, px.period, py.period, cand.offset_x,
                                            cand.offset_y, kMinOpaqueCellPixels);
            cand.columns = st.columns;
            cand.rows = st.rows;
            cand.cell_occupancy = st.avg_foreground_ratio;

            // ---- Component → Cell mapping：每组件一个 cell 线性索引，统计占用 cell ----
            std::vector<int> indices;
            indices.reserve(n_comps);
            std::vector<int> sorted;
            sorted.reserve(n_comps);
            for (const auto& c : comps) {
                const int cx = component_cell_index(c.cx, cand.offset_x, px.period, st.columns);
                const int cy = component_cell_index(c.cy, cand.offset_y, py.period, st.rows);
                const int lin = cy * st.columns + cx;
                indices.push_back(lin);
                sorted.push_back(lin);
            }
            std::sort(sorted.begin(), sorted.end());
            const int unique_cells =
                static_cast<int>(std::unique(sorted.begin(), sorted.end()) - sorted.begin());
            cand.component_count = n_comps;
            cand.occupied_cells = unique_cells;
            cand.component_coverage = n_comps > 0
                                          ? static_cast<double>(unique_cells) / n_comps
                                          : 0.0;
            cand.component_cell_indices = std::move(indices);

            // ---- occupancy（修正：原「非空 cell 占比接近 0.5 最高分」对全占满的
            //      完美网格给 0 分，方向反了。改为：几何占用率高 + cell 内前景占比
            //      合理 → 高分；噪声网格 avg_fg 极低 → 降权）----
            const double total_cells = static_cast<double>(st.rows) * st.columns;
            const double occupied_ratio = total_cells > 0 ? st.occupied / total_cells : 0.0;
            cand.occupancy = occupied_ratio * std::clamp(cand.cell_occupancy * 8.0, 0.0, 1.0);

            // 综合分（权重：周期 20% / 对齐 35% / 边界 25% / 尺寸 5% / 占用 15%）
            cand.score = 0.20 * cand.periodicity + 0.35 * cand.alignment +
                         0.25 * cand.boundary + 0.05 * cand.size_consistency +
                         0.15 * cand.occupancy;

            result.candidates.push_back(std::move(cand));
        }
    }

    // 排序取最佳
    std::sort(result.candidates.begin(), result.candidates.end(),
              [](const GridCandidate& a, const GridCandidate& b) { return a.score > b.score; });
    if (result.candidates.empty()) return result;
    result.best = result.candidates.front();
    result.confidence = result.best.score;
    // 周期性硬门槛：自相关强度是网格最可靠的信号，
    // 低于 0.25 说明前景分布无真实周期（如少量不规则块），直接判非网格
    // 组件数 < 4 时样本过少，不判定网格（保守回退）
    result.is_grid = result.confidence >= 0.65 && result.best.periodicity >= 0.25 &&
                     n_comps >= 4;
    return result;
}

}  // namespace sps
