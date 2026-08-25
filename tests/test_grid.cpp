#include "mask/mask.hpp"
#include "segmentation/auto_detector.hpp"
#include "segmentation/grid_detector.hpp"
#include "segmentation/splitter.hpp"

#include <catch_amalgamated.hpp>

using namespace sps;

namespace {

// 构造 WxH 前景 mask：在 (x,y) 放不透明块
Mask mask_with_blocks(std::vector<std::tuple<int, int, int, int>> blocks, int w, int h) {
    Mask m(w, h);
    for (const auto& [bx, by, bw, bh] : blocks) {
        for (int y = by; y < by + bh; ++y)
            for (int x = bx; x < bx + bw; ++x) m.set(x, y, true);
    }
    return m;
}

Image image_with_blocks(std::vector<std::tuple<int, int, int, int>> blocks, int w, int h) {
    Image img(w, h, 0);
    for (const auto& [bx, by, bw, bh] : blocks) {
        for (int y = by; y < by + bh; ++y)
            for (int x = bx; x < bx + bw; ++x) img.at(x, y).a = 255;
    }
    return img;
}

}  // namespace

TEST_CASE("Grid: empty cell skipped, filled cell detected", "[grid]") {
    // 64x64，cell 16 → 4x4 网格
    // cell (0,0) 和 (2,1) 有内容，其余空白
    Mask m = mask_with_blocks({{1, 1, 5, 5}, {33, 17, 8, 8}}, 64, 64);
    auto rects = grid_detect(m, 16);
    REQUIRE(rects.size() == 2);
    CHECK(rects[0].x == 0);
    CHECK(rects[0].y == 0);
    CHECK(rects[0].width == 16);
    CHECK(rects[0].height == 16);
    CHECK(rects[1].x == 32);
    CHECK(rects[1].y == 16);
}

TEST_CASE("Grid: partial edge cell is clamped to image size", "[grid]") {
    // 20x20，cell 16 → 2x2 网格，右下角 cell 只有 4x4
    Mask m = mask_with_blocks({{17, 17, 2, 2}}, 20, 20);
    auto rects = grid_detect(m, 16);
    REQUIRE(rects.size() == 1);
    CHECK(rects[0].x == 16);
    CHECK(rects[0].y == 16);
    CHECK(rects[0].width == 4);
    CHECK(rects[0].height == 4);
}

TEST_CASE("Grid: invalid cell size returns empty", "[grid]") {
    Mask m(16, 16);
    CHECK(grid_detect(m, 0).empty());
    CHECK(grid_detect(m, -4).empty());
    CHECK(grid_detect(Mask(), 16).empty());
}

TEST_CASE("Auto: 16px grid with in-cell sprites -> COMPONENTS_IN_GRID", "[grid]") {
    // 64x64，精灵 10x10 对齐 16px 网格原点，2x2 网格（4 个分量，两方向间距均 16）
    // 组件 bbox(10x10) < cell(16x16) 且每 cell 恰 1 组件 → COMPONENTS_IN_GRID（bbox 切割）
    Mask m = mask_with_blocks({{0, 0, 10, 10}, {16, 0, 10, 10}, {0, 16, 10, 10},
                               {16, 16, 10, 10}},
                              64, 64);
    AutoOptions opts;
    AutoDetection det = auto_detect(m, opts);
    REQUIRE(det.mode == AutoSliceMode::ComponentsInGrid);
    REQUIRE(det.rects.size() == 4);
    CHECK(det.grid.best.period_x == 16);
    CHECK(det.grid.best.period_y == 16);
    // 每 cell 一个组件 → 输出组件 bbox（10x10），不是 16x16 cell
    for (const auto& r : det.rects) {
        CHECK(r.width == 10);
        CHECK(r.height == 10);
    }
}

TEST_CASE("Auto: fully opaque mask falls back to components", "[grid]") {
    // 全前景 = 1 个连通分量 → 无网格 → COMPONENTS，1 个整图 sprite
    Mask m(64, 64, true);
    AutoOptions opts;
    AutoDetection det = auto_detect(m, opts);
    CHECK(det.mode == AutoSliceMode::Components);
    CHECK(det.rects.size() == 1);
    CHECK(det.rects[0].width == 64);
    CHECK(det.rects[0].height == 64);
}

TEST_CASE("Auto: irregular layout is not a grid", "[grid]") {
    // 5 个块，间距不一（无稳定网格）→ COMPONENTS
    Mask m = mask_with_blocks({{2, 2, 8, 8}, {40, 5, 8, 8}, {10, 40, 8, 8},
                               {50, 45, 8, 8}, {25, 20, 8, 8}},
                              64, 64);
    AutoOptions opts;
    AutoDetection det = auto_detect(m, opts);
    CHECK(det.mode == AutoSliceMode::Components);
    REQUIRE(det.rects.size() == 5);
}

TEST_CASE("Auto: too few components is not a grid", "[grid]") {
    // 3 个块 < 4 → 无法判断 → COMPONENTS
    Mask m = mask_with_blocks({{0, 0, 8, 8}, {16, 0, 8, 8}, {0, 16, 8, 8}}, 64, 64);
    AutoOptions opts;
    AutoDetection det = auto_detect(m, opts);
    CHECK(det.mode == AutoSliceMode::Components);
    REQUIRE(det.rects.size() == 3);
}

TEST_CASE("Detect grid: 3x3 grid at 16px detects 16x16", "[grid]") {
    // 3x3 网格：中心间距 16
    Mask m = mask_with_blocks({{1, 1, 8, 8}, {17, 1, 8, 8}, {33, 1, 8, 8},
                               {1, 17, 8, 8}, {17, 17, 8, 8}, {33, 17, 8, 8},
                               {1, 33, 8, 8}, {17, 33, 8, 8}, {33, 33, 8, 8}},
                              64, 64);
    GridDetection det = detect_grid(m);
    REQUIRE(det.is_grid);
    CHECK(det.best.period_x == 16);
    CHECK(det.best.period_y == 16);
    CHECK(det.confidence >= 0.65);
}

TEST_CASE("Detect grid: irregular layout is rejected", "[grid]") {
    Mask m = mask_with_blocks({{2, 2, 8, 8}, {40, 5, 8, 8}, {10, 40, 8, 8},
                               {50, 45, 8, 8}, {25, 20, 8, 8}},
                              64, 64);
    GridDetection det = detect_grid(m);
    CHECK_FALSE(det.is_grid);
}

TEST_CASE("Detect grid: rectangular cells supported", "[grid]") {
    // 3x3 网格但 X 周期 32、Y 周期 16（非方形 cell）
    Mask m = mask_with_blocks({{1, 1, 10, 8}, {33, 1, 10, 8}, {65, 1, 10, 8},
                               {1, 17, 10, 8}, {33, 17, 10, 8}, {65, 17, 10, 8},
                               {1, 33, 10, 8}, {33, 33, 10, 8}, {65, 33, 10, 8}},
                              96, 64);
    GridDetection det = detect_grid(m);
    REQUIRE(det.is_grid);
    CHECK(det.best.period_x == 32);
    CHECK(det.best.period_y == 16);
}

TEST_CASE("Splitter: grid mode returns grid rects", "[grid]") {
    Image img = image_with_blocks({{1, 1, 5, 5}, {33, 17, 8, 8}}, 64, 64);
    SplitOptions opts;
    opts.mode = DetectionMode::Grid;
    opts.grid_cell_size = 16;
    auto result = split_image(img, opts);
    REQUIRE(result.sprites.size() == 2);
    CHECK(result.sprites[0].x == 0);
    CHECK(result.sprites[1].x == 32);
}

TEST_CASE("Splitter: auto mode picks components in grid", "[grid]") {
    // 10x10 精灵在 16px 网格上：AUTO → COMPONENTS_IN_GRID，4 个组件 bbox（10x10）
    Image img = image_with_blocks({{0, 0, 10, 10}, {16, 0, 10, 10}, {0, 16, 10, 10},
                                   {16, 16, 10, 10}},
                                  64, 64);
    SplitOptions opts;
    opts.mode = DetectionMode::Auto;
    auto result = split_image(img, opts);
    REQUIRE(result.sprites.size() == 4);
    // 不是 16x16 cell，而是 10x10 组件 bbox
    CHECK(result.auto_mode == static_cast<int>(AutoSliceMode::ComponentsInGrid));
    for (const auto& r : result.sprites) {
        CHECK(r.width == 10);
        CHECK(r.height == 10);
    }
}

TEST_CASE("Splitter: auto falls back to components for irregular layout", "[grid]") {
    // 不规则排列（无网格）→ auto 回退 components
    Image img = image_with_blocks({{2, 2, 8, 8}, {40, 5, 8, 8}, {10, 40, 8, 8},
                                   {50, 45, 8, 8}, {25, 20, 8, 8}},
                                  64, 64);
    SplitOptions opts;
    opts.mode = DetectionMode::Auto;
    auto result = split_image(img, opts);
    REQUIRE(result.sprites.size() == 5);  // components 模式切出全部 5 个
}

TEST_CASE("Splitter: grid + remove_background share foreground pipeline", "[grid]") {
    // 无透明通道的白底素材：alpha 模式网格无效（全非空），
    // remove_background 后网格只命中精灵所在 cell
    Image img(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            img.at(x, y).r = 253; img.at(x, y).g = 253; img.at(x, y).b = 253; img.at(x, y).a = 255;
        }
    for (int y = 5; y < 15; ++y)
        for (int x = 5; x < 15; ++x) {
            img.at(x, y).r = 0; img.at(x, y).g = 0; img.at(x, y).b = 0;
        }

    // 不清理背景：alpha 全不透明 → 所有 cell 非空
    SplitOptions plain;
    plain.mode = DetectionMode::Grid;
    plain.grid_cell_size = 16;
    auto all = split_image(img, plain);
    CHECK(all.sprites.size() == 16);  // 4x4 全非空

    // 清理背景后：只有 (0,0) cell 非空
    SplitOptions with_bg;
    with_bg.mode = DetectionMode::Grid;
    with_bg.grid_cell_size = 16;
    with_bg.remove_background = true;
    with_bg.background_threshold = 12;
    auto filtered = split_image(img, with_bg);
    REQUIRE(filtered.sprites.size() == 1);
    CHECK(filtered.sprites[0].x == 0);
    CHECK(filtered.sprites[0].y == 0);
}
