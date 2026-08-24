#include "segmentation/connected_components.hpp"

#include <catch_amalgamated.hpp>

using namespace sps;

namespace {

// 用字符串网格构造 mask：'#' 前景，'.' 背景
Mask mask_from_rows(std::vector<std::string> rows) {
    const int h = static_cast<int>(rows.size());
    const int w = static_cast<int>(rows[0].size());
    Mask m(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (rows[y][x] == '#') m.set(x, y, true);
    return m;
}

}  // namespace

TEST_CASE("CCL: empty mask gives no components", "[ccl]") {
    CHECK(connected_components(Mask(5, 5)).empty());
    CHECK(connected_components(Mask()).empty());
}

TEST_CASE("CCL: isolated single pixels are separate components", "[ccl]") {
    // # . #
    // . . .
    // # . #
    Mask m = mask_from_rows({"#.#", "...", "#.#"});
    auto comps = connected_components(m);
    REQUIRE(comps.size() == 4);
    for (const auto& c : comps) {
        CHECK(c.bounds.width == 1);
        CHECK(c.bounds.height == 1);
        CHECK(c.area == 1);
    }
}

TEST_CASE("CCL: 4-connectivity not 8-connectivity", "[ccl]") {
    // # .
    // . #
    Mask m = mask_from_rows({"#.", ".#"});
    auto comps = connected_components(m);
    REQUIRE(comps.size() == 2);
}

TEST_CASE("CCL: U-shape is one component (8 not merged diagonally)", "[ccl]") {
    // ###
    // #.#
    // ###
    Mask m = mask_from_rows({"###", "#.#", "###"});
    auto comps = connected_components(m);
    REQUIRE(comps.size() == 1);
    const auto& c = comps[0];
    CHECK(c.bounds.x == 0);
    CHECK(c.bounds.y == 0);
    CHECK(c.bounds.width == 3);
    CHECK(c.bounds.height == 3);
    CHECK(c.area == 8);  // 9 - 1 洞
}

TEST_CASE("CCL: full image is one component", "[ccl]") {
    Mask m(7, 5, true);
    auto comps = connected_components(m);
    REQUIRE(comps.size() == 1);
    CHECK(comps[0].bounds.width == 7);
    CHECK(comps[0].bounds.height == 5);
    CHECK(comps[0].area == 35);
}

TEST_CASE("CCL: adjacent components do not merge through one-pixel gap", "[ccl]") {
    // ##.##
    // ##.##
    Mask m = mask_from_rows({"##.##", "##.##"});
    auto comps = connected_components(m);
    REQUIRE(comps.size() == 2);
    CHECK(comps[0].bounds.width == 2);
    CHECK(comps[1].bounds.x == 3);
}

TEST_CASE("CCL: component at image corner", "[ccl]") {
    // ##..
    // #...
    Mask m = mask_from_rows({"##..", "#..."});
    auto comps = connected_components(m);
    REQUIRE(comps.size() == 1);
    CHECK(comps[0].bounds.x == 0);
    CHECK(comps[0].bounds.y == 0);
    CHECK(comps[0].bounds.width == 2);
    CHECK(comps[0].bounds.height == 2);
    CHECK(comps[0].area == 3);
}

TEST_CASE("CCL: output sorted by y then x", "[ccl]") {
    // 下方分量应先于上方分量输出（y 升序）
    // #.......
    // ........
    // .....#..
    Mask m = mask_from_rows({"#.......", "........", ".....#.."});
    auto comps = connected_components(m);
    REQUIRE(comps.size() == 2);
    CHECK(comps[0].bounds.y == 0);
    CHECK(comps[0].bounds.x == 0);
    CHECK(comps[1].bounds.y == 2);
    CHECK(comps[1].bounds.x == 5);
}
