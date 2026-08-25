#include "segmentation/background.hpp"

#include "image/pixel.hpp"
#include "mask/morphology.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <queue>
#include <utility>
#include <vector>

namespace sps {

namespace {

// 颜色距离（RGB 曼哈顿距离），与 threshold 同量纲
int color_distance(const Pixel& a, const Pixel& b) {
    return std::abs(static_cast<int>(a.r) - b.r) +
           std::abs(static_cast<int>(a.g) - b.g) +
           std::abs(static_cast<int>(a.b) - b.b);
}

// 外圈环带采样（宽 ring_width px）：作为背景统计样本。
// 相比旧实现的「仅四角 4 像素」，环带样本多、且对贴边前景更稳健（中位数抗离群）。
std::vector<Pixel> collect_border_ring(const Image& image, int ring_width) {
    const int w = image.width();
    const int h = image.height();
    std::vector<Pixel> ring;
    ring.reserve(static_cast<std::size_t>(w + h) * 2 * ring_width);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (x < ring_width || y < ring_width || x >= w - ring_width ||
                y >= h - ring_width) {
                ring.push_back(image.at(x, y));
            }
        }
    }
    return ring;
}

// 每通道中位数：稳健背景参考色（不受少量贴边前景/噪点影响）
Pixel median_color(std::vector<Pixel> ring) {
    if (ring.empty()) return Pixel{};
    std::vector<int> r, g, b;
    r.reserve(ring.size());
    g.reserve(ring.size());
    b.reserve(ring.size());
    for (const auto& p : ring) {
        r.push_back(p.r);
        g.push_back(p.g);
        b.push_back(p.b);
    }
    std::sort(r.begin(), r.end());
    std::sort(g.begin(), g.end());
    std::sort(b.begin(), b.end());
    Pixel out;
    out.r = static_cast<uint8_t>(r[r.size() / 2]);
    out.g = static_cast<uint8_t>(g[g.size() / 2]);
    out.b = static_cast<uint8_t>(b[b.size() / 2]);
    return out;
}

// 背景噪声水平：环带中与参考色距离 <= gate 的像素（排除贴边前景），
// 每通道标准差之和。σ_sum 越大说明背景越「不标准」（压缩噪点/轻微渐变）。
double background_sigma_sum(const std::vector<Pixel>& ring, const Pixel& bg, int gate) {
    double mr = 0, mg = 0, mb = 0;
    long n = 0;
    for (const auto& p : ring) {
        if (color_distance(p, bg) <= gate) {
            mr += p.r;
            mg += p.g;
            mb += p.b;
            ++n;
        }
    }
    if (n < 2) return 0.0;  // 样本不足 → 视为纯色，退回用户阈值
    mr /= n;
    mg /= n;
    mb /= n;
    double vr = 0, vg = 0, vb = 0;
    for (const auto& p : ring) {
        if (color_distance(p, bg) <= gate) {
            vr += (p.r - mr) * (p.r - mr);
            vg += (p.g - mg) * (p.g - mg);
            vb += (p.b - mb) * (p.b - mb);
        }
    }
    return std::sqrt(vr / n) + std::sqrt(vg / n) + std::sqrt(vb / n);
}

}  // namespace

BackgroundEstimate estimate_background(const Image& image, bool has_bg_color, Pixel bg_color) {
    BackgroundEstimate est;
    if (image.empty()) return est;

    constexpr int kRingWidth = 2;
    constexpr int kNoiseGate = 24;  // 「接近参考色」的门限：排除贴边前景，包含压缩噪声

    const std::vector<Pixel> ring = collect_border_ring(image, kRingWidth);
    est.color = has_bg_color ? bg_color : median_color(ring);
    est.sigma_sum = background_sigma_sum(ring, est.color, kNoiseGate);
    return est;
}

Mask background_mask(const Image& image, const BackgroundOptions& options) {
    constexpr double kStrictSigma = 4.0;  // 主 flood fill 自适应阈值系数
    constexpr double kRelaxSigma = 9.0;   // 边缘过渡清扫容差系数（偏色比纯噪声更宽）

    const int w = image.width();
    const int h = image.height();
    Mask mask(w, h, false);
    if (w <= 0 || h <= 0) return mask;

    // ---- 背景参考色 + 噪声水平 ----
    // 优先级：手动指定色 > 魔棒种子点色 > 环带中位数（自动估计）
    const std::vector<Pixel> ring = collect_border_ring(image, 2);
    const bool has_seed = options.seed_x >= 0 && options.seed_y >= 0 &&
                          options.seed_x < w && options.seed_y < h;
    const Pixel bg = options.has_bg_color ? options.bg_color
            : (has_seed ? image.at(options.seed_x, options.seed_y)
                        : median_color(ring));
    const double sigma_sum = background_sigma_sum(ring, bg, 24);

    // ---- 自适应阈值 ----
    // 纯色背景 σ≈0 → 阈值保持用户给定（默认 12）；有压缩/渐变噪声 → 自动放大，
    // 避免「背景接近物体边缘时的偏色超过固定阈值导致 flood fill 断裂」。
    const int threshold = std::max(options.threshold,
                                   static_cast<int>(std::ceil(kStrictSigma * sigma_sum)));
    // 边缘过渡清扫容差：物体边缘因压缩/抗锯齿产生的偏色比纯背景噪声更宽，
    // 用更大容差 + 有限轮数从背景边界向内补吃（只吃薄过渡带，不深入物体内部）。
    const int relaxed = std::max(threshold,
                                 static_cast<int>(std::ceil(kRelaxSigma * sigma_sum)));

    // ---- BFS flood fill（魔棒种子点 或 四边播种，向内扩散）----
    std::queue<std::pair<int, int>> queue;
    auto try_seed = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        if (mask.get(x, y)) return;  // 已标记
        if (color_distance(image.at(x, y), bg) <= threshold) {
            mask.set(x, y, true);
            queue.emplace(x, y);
        }
    };
    if (has_seed) {
        try_seed(options.seed_x, options.seed_y);   // 魔棒：单点播种
    } else {
        for (int x = 0; x < w; ++x) {
            try_seed(x, 0);
            try_seed(x, h - 1);
        }
        for (int y = 0; y < h; ++y) {
            try_seed(0, y);
            try_seed(w - 1, y);
        }
    }
    while (!queue.empty()) {
        const auto [x, y] = queue.front();
        queue.pop();
        try_seed(x - 1, y);
        try_seed(x + 1, y);
        try_seed(x, y - 1);
        try_seed(x, y + 1);
    }

    // ---- 边缘过渡色清扫 ----
    // 有限轮数地把「贴近背景边界、且颜色在放宽容差内」的前景补判为背景，
    // 专门清理物体边缘那一圈因压缩/AA 造成的过渡相近色（色边 halo）。
    const int edge_passes = std::max(0, options.edge_passes);
    for (int pass = 0; pass < edge_passes; ++pass) {
        std::vector<std::pair<int, int>> to_add;
        to_add.reserve(static_cast<std::size_t>(w) * h / 16);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                if (mask.get(x, y)) continue;
                if (color_distance(image.at(x, y), bg) > relaxed) continue;
                const bool adj = (x > 0 && mask.get(x - 1, y)) ||
                                 (x + 1 < w && mask.get(x + 1, y)) ||
                                 (y > 0 && mask.get(x, y - 1)) ||
                                 (y + 1 < h && mask.get(x, y + 1));
                if (adj) to_add.emplace_back(x, y);
            }
        }
        if (to_add.empty()) break;
        for (const auto& [x, y] : to_add) mask.set(x, y, true);
    }

    // ---- 收缩（背景膨胀 shrink px：消弱主体边缘）----
    // 背景向 4 邻菱形膨胀 → 吃掉主体边缘一圈残余（halo/色边/抗锯齿脏边），
    // 默认算法清不干净时加大 shrink 让边缘更彻底。
    if (options.shrink > 0) {
        mask = dilate(mask, options.shrink);
    }
    return mask;
}


// 羽化：二值背景 mask → 灰度 AlphaMask（255=背景）。radius px 内做 box blur
// 迭代（3x3 均值 ≈ 高斯近似），边缘产生 0-255 渐变（软边 alpha）。
AlphaMask feather_mask(const Mask& background, int radius) {
    const int w = background.width();
    const int h = background.height();
    AlphaMask out(w, h, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            out.set(x, y, background.get(x, y) ? 255 : 0);
        }
    }
    for (int pass = 0; pass < radius; ++pass) {
        AlphaMask blurred(w, h, 0);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int sum = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        // 图外视为背景（255）：图边像素不被羽化拉低（背景延伸到图外）
                        if (x + dx < 0 || y + dy < 0 || x + dx >= w || y + dy >= h) {
                            sum += 255;
                        } else {
                            sum += out.get(x + dx, y + dy);
                        }
                    }
                }
                blurred.set(x, y, static_cast<uint8_t>(sum / 9));
            }
        }
        out = blurred;
    }
    return out;
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

void make_background_transparent(Image& image, const AlphaMask& background) {
    const int w = image.width();
    const int h = image.height();
    if (w != background.width() || h != background.height()) {
        throw std::invalid_argument(
            "sps: make_background_transparent: image/alpha-mask size mismatch");
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint8_t bgd = background.get(x, y);
            if (bgd == 0) continue;
            Pixel& p = image.at(x, y);
            // 软边：背景密度越高 alpha 越低（边缘半透明过渡）
            p.a = static_cast<uint8_t>((p.a * (255 - bgd)) / 255);
        }
    }
}

}  // namespace sps
