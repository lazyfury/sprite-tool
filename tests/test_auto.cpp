#include "mask/mask.hpp"
#include "segmentation/auto_detector.hpp"
#include "segmentation/splitter.hpp"

#include <catch_amalgamated.hpp>

using namespace sps;

namespace {

Mask mask_with_blocks(std::vector<std::tuple<int, int, int, int>> blocks, int w, int h) {
    Mask m(w, h);
    for (const auto& [bx, by, bw, bh] : blocks) {
        for (int y = by; y < by + bh; ++y)
            for (int x = bx; x < bx + bw; ++x) m.set(x, y, true);
    }
    return m;
}

// 规则网格：cols x rows，cell 尺寸，每 cell 一个 comp x comp 的组件（居中，略小于 cell）
Mask grid_mask(int cols, int rows, int cell, int comp) {
    const int w = cols * cell;
    const int h = rows * cell;
    Mask m(w, h);
    const int pad = (cell - comp) / 2;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const int x0 = c * cell + pad;
            const int y0 = r * cell + pad;
            for (int y = y0; y < y0 + comp; ++y)
                for (int x = x0; x < x0 + comp; ++x) m.set(x, y, true);
        }
    }
    return m;
}

}  // namespace

TEST_CASE("Auto regression 01: uniform grid, sprite==cell -> GRID", "[auto]") {
    // 8x8 网格，cell 64，组件 62x62（≈cell，2px 间隙保证 CCL 分离）→ AUTO → GRID，64 个 cell
    const Mask m = grid_mask(8, 8, 64, 62);
    AutoOptions opts;
    AutoDetection det = auto_detect(m, opts);
    REQUIRE(det.mode == AutoSliceMode::Grid);
    REQUIRE(det.rects.size() == 64);
    for (const auto& r : det.rects) {
        CHECK(r.width == 64);
        CHECK(r.height == 64);
    }
}

TEST_CASE("Auto regression 02: irregular objects -> COMPONENTS", "[auto]") {
    // 不同尺寸、不同位置，无稳定网格 → AUTO → COMPONENTS（6 个 bbox）
    Mask m = mask_with_blocks({{2, 2, 12, 8}, {40, 5, 20, 30}, {10, 40, 8, 24},
                               {50, 45, 15, 15}, {25, 20, 6, 6}, {70, 10, 30, 12}},
                              100, 80);
    AutoOptions opts;
    AutoDetection det = auto_detect(m, opts);
    CHECK(det.mode == AutoSliceMode::Components);
    REQUIRE(det.rects.size() == 6);
}

TEST_CASE("Auto regression 03: regular layout variable bbox -> COMPONENTS_IN_GRID", "[auto]") {
    // 鱼图场景：4x7 布局 cell 176，组件 120x105（远小于 cell）→ 28 个组件 bbox，非 cell
    const int cols = 4, rows = 7;
    const int cell = 176, comp_w = 120, comp_h = 105;
    const int w = cols * cell, h = rows * cell;
    const int px = (cell - comp_w) / 2, py = (cell - comp_h) / 2;
    std::vector<std::tuple<int, int, int, int>> blocks;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            blocks.push_back({c * cell + px, r * cell + py, comp_w, comp_h});
    const Mask m = mask_with_blocks(blocks, w, h);
    AutoOptions opts;
    AutoDetection det = auto_detect(m, opts);
    REQUIRE(det.mode == AutoSliceMode::ComponentsInGrid);
    REQUIRE(det.grid.best.period_x == 176);
    REQUIRE(det.grid.best.period_y == 176);
    REQUIRE(det.rects.size() == 28);
    for (const auto& r : det.rects) {
        CHECK(r.width == comp_w);
        CHECK(r.height == comp_h);
    }
}

TEST_CASE("Auto regression 04: touching objects -> single component", "[auto]") {
    // 两个组件边界接触（4-邻域连通）→ 1 个连通分量 → COMPONENTS 1 sprite
    Mask m = mask_with_blocks({{5, 5, 20, 20}, {25, 5, 10, 20}}, 60, 40);
    AutoOptions opts;
    AutoDetection det = auto_detect(m, opts);
    CHECK(det.mode == AutoSliceMode::Components);
    REQUIRE(det.rects.size() == 1);
}

TEST_CASE("Auto regression 05: noise filtered by min_pixels", "[auto]") {
    // 1 个大块 + 30 个散布 1px 噪点 → 噪点被面积过滤 → 1 sprite
    std::vector<std::tuple<int, int, int, int>> blocks = {{10, 10, 40, 40}};
    for (int i = 0; i < 30; ++i) {
        blocks.push_back({100 + (i * 7) % 90, 5 + (i * 13) % 90, 1, 1});
    }
    Mask m = mask_with_blocks(blocks, 200, 200);
    AutoOptions opts;
    AutoDetection det = auto_detect(m, opts);
    CHECK(det.mode == AutoSliceMode::Components);
    REQUIRE(det.rects.size() == 1);
    CHECK(det.raw_component_count == 31);
    CHECK(det.filtered_component_count == 1);
}

TEST_CASE("Auto regression 06: empty cells stay empty", "[auto]") {
    // 3x3 网格（cell 32）缺中心一格 → 8 组件 → COMPONENTS_IN_GRID 8 sprites
    std::vector<std::tuple<int, int, int, int>> blocks;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            if (r == 1 && c == 1) continue;
            blocks.push_back({c * 32 + 6, r * 32 + 6, 20, 20});
        }
    }
    Mask m = mask_with_blocks(blocks, 96, 96);
    AutoOptions opts;
    AutoDetection det = auto_detect(m, opts);
    REQUIRE(det.mode == AutoSliceMode::ComponentsInGrid);
    REQUIRE(det.rects.size() == 8);
    CHECK(det.grid_columns == 3);
    CHECK(det.grid_rows == 3);
}

TEST_CASE("Auto: merge_distance merges nearby components", "[auto]") {
    // 角色 (10,10,20,40) + 武器 (37,15,8,8)，间隙 7px：
    // merge=0 → 2 个 sprite；merge=10 → 合并为 1（外接框）
    Mask m = mask_with_blocks({{10, 10, 20, 40}, {37, 15, 8, 8}}, 80, 60);
    AutoOptions opts;
    AutoDetection det0 = auto_detect(m, opts);
    REQUIRE(det0.rects.size() == 2);
    opts.merge_distance = 10;
    AutoDetection det1 = auto_detect(m, opts);
    REQUIRE(det1.rects.size() == 1);
    CHECK(det1.rects[0].x == 10);
    CHECK(det1.rects[0].width == 35);
}

TEST_CASE("Auto: padding clamps to cell boundary (ComponentsInGrid)", "[auto]") {
    // 2x2 网格 cell 64，组件 40x40 居中（pad 12）：
    // padding=20 → expand 后 clamp 到 cell [0,64)，不吃邻居
    const int cell = 64, comp = 40, pad = (cell - comp) / 2;
    Mask m = mask_with_blocks({{pad, pad, comp, comp},
                               {cell + pad, pad, comp, comp},
                               {pad, cell + pad, comp, comp},
                               {cell + pad, cell + pad, comp, comp}},
                              2 * cell, 2 * cell);
    AutoOptions opts;
    opts.padding = 20;
    AutoDetection det = auto_detect(m, opts);
    REQUIRE(det.mode == AutoSliceMode::ComponentsInGrid);
    REQUIRE(det.rects.size() == 4);
    for (const auto& r : det.rects) {
        CHECK(r.width == cell);
        CHECK(r.height == cell);
        CHECK(r.x % cell == 0);
        CHECK(r.y % cell == 0);
    }
}
