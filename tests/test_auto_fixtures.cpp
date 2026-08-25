#include "image/image.hpp"
#include "segmentation/auto_detector.hpp"
#include "segmentation/splitter.hpp"

#include <catch_amalgamated.hpp>

using namespace sps;

// Auto 重构回归素材（方案 §25，tests/fixtures/auto/*.png，由 tools/gen_auto_fixtures.cpp 生成）：
// 端到端：PNG 加载 → alpha 分割 → auto_detect 决策 → rects。
// 素材是固定基准：算法调参不得破坏这些断言（先跑生成器可复现素材）。
// 注意：fixtures 是程序化构造的抽象场景（方块），真实素材回归另见 main.gd 冒烟。

namespace {

// 单个 fixture 断言：决策模式 / sprite 数量 / 布局 / cell 尺寸
struct Expect {
    AutoSliceMode mode;
    int count;
    int cols;
    int rows;
    int cell_w;  // 0 = 不检查（无网格）
    int cell_h;
};

void check_fixture(const std::string& name, const Expect& exp) {
    INFO("fixture " + name);
    const Image img = Image::load_png("tests/fixtures/auto/" + name);
    REQUIRE(img.width() > 0);
    SplitOptions opts;
    opts.mode = DetectionMode::Auto;
    const SplitResult result = split_image(img, opts);
    CHECK(static_cast<AutoSliceMode>(result.auto_mode) == exp.mode);
    CHECK(result.sprites.size() == static_cast<std::size_t>(exp.count));
    CHECK(result.auto_grid_columns == exp.cols);
    CHECK(result.auto_grid_rows == exp.rows);
    if (exp.cell_w > 0) {
        CHECK(result.auto_grid_cell_w == exp.cell_w);
        CHECK(result.auto_grid_cell_h == exp.cell_h);
    }
}

}  // namespace

TEST_CASE("Auto fixture 01: uniform grid, sprite==cell -> GRID 64x(64x64)", "[auto][fixtures]") {
    check_fixture("01_uniform_grid.png",
                  {AutoSliceMode::Grid, 64, 8, 8, 64, 64});
    // 再验证 rect 全部是 64x64 cell
    const Image img = Image::load_png("tests/fixtures/auto/01_uniform_grid.png");
    SplitOptions opts;
    opts.mode = DetectionMode::Auto;
    const SplitResult result = split_image(img, opts);
    REQUIRE(result.sprites.size() == 64);
    for (const auto& r : result.sprites) {
        CHECK(r.width == 64);
        CHECK(r.height == 64);
    }
}

TEST_CASE("Auto fixture 02: irregular objects -> COMPONENTS 6", "[auto][fixtures]") {
    check_fixture("02_irregular_objects.png",
                  {AutoSliceMode::Components, 6, 0, 0, 0, 0});
}

TEST_CASE("Auto fixture 03: regular layout variable bbox (fish) -> COMPONENTS_IN_GRID 28 bbox",
          "[auto][fixtures]") {
    check_fixture("03_regular_layout_variable_bbox.png",
                  {AutoSliceMode::ComponentsInGrid, 28, 4, 7, 176, 176});
    // 核心验收：输出 28 个 120x105 组件 bbox，不是 28 个 176x176 cell
    const Image img = Image::load_png("tests/fixtures/auto/03_regular_layout_variable_bbox.png");
    SplitOptions opts;
    opts.mode = DetectionMode::Auto;
    const SplitResult result = split_image(img, opts);
    REQUIRE(result.sprites.size() == 28);
    for (const auto& r : result.sprites) {
        CHECK(r.width == 120);
        CHECK(r.height == 105);
    }
}

TEST_CASE("Auto fixture 04: touching objects -> COMPONENTS 1", "[auto][fixtures]") {
    check_fixture("04_objects_touching.png",
                  {AutoSliceMode::Components, 1, 0, 0, 0, 0});
}

TEST_CASE("Auto fixture 05: noise filtered -> COMPONENTS 1", "[auto][fixtures]") {
    check_fixture("05_noise.png",
                  {AutoSliceMode::Components, 1, 0, 0, 0, 0});
    // 噪点被面积过滤（raw 31 → filtered 1）
    const Image img = Image::load_png("tests/fixtures/auto/05_noise.png");
    SplitOptions opts;
    opts.mode = DetectionMode::Auto;
    const SplitResult result = split_image(img, opts);
    CHECK(result.auto_raw_components == 31);
    CHECK(result.auto_filtered_components == 1);
}

TEST_CASE("Auto fixture 06: empty cells stay empty -> COMPONENTS_IN_GRID 8", "[auto][fixtures]") {
    check_fixture("06_empty_cells.png",
                  {AutoSliceMode::ComponentsInGrid, 8, 3, 3, 32, 32});
}
