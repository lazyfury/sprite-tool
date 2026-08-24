#include "analyzer.hpp"

#include <catch_amalgamated.hpp>

using namespace sps;

TEST_CASE("Analyzer: empty image", "[analyzer]") {
    ImageStats s = analyze_image(Image());
    CHECK(s.width == 0);
    CHECK(s.total_pixels == 0);
    CHECK(s.component_count == 0);
}

TEST_CASE("Analyzer: fully transparent image", "[analyzer]") {
    Image img(10, 10, 0);  // fill=0 → alpha 全 0
    ImageStats s = analyze_image(img);
    CHECK(s.total_pixels == 100);
    CHECK(s.transparent_pixels == 100);
    CHECK(s.has_transparency);
    CHECK(s.uniform_alpha);
    CHECK(s.component_count == 0);
}

TEST_CASE("Analyzer: fully opaque uniform image", "[analyzer]") {
    Image img(20, 20);
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 20; ++x) img.at(x, y) = Pixel{253, 253, 253, 255};
    ImageStats s = analyze_image(img);
    CHECK(s.opaque_pixels == 400);
    CHECK_FALSE(s.has_transparency);
    CHECK(s.uniform_alpha);
    CHECK(s.bg_uniform);
    CHECK(s.bg_estimate.r == 253);
    // 纯色 → 全背景，无分量
    CHECK(s.component_count == 0);
}

TEST_CASE("Analyzer: white bg with dark blocks", "[analyzer]") {
    Image img(40, 40);
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 40; ++x) img.at(x, y) = Pixel{253, 253, 253, 255};
    // 两个黑块
    for (int y = 2; y < 8; ++y)
        for (int x = 2; x < 8; ++x) img.at(x, y) = Pixel{0, 0, 0, 255};
    for (int y = 20; y < 30; ++y)
        for (int x = 20; x < 30; ++x) img.at(x, y) = Pixel{0, 0, 0, 255};

    ImageStats s = analyze_image(img, 12);
    CHECK_FALSE(s.has_transparency);
    CHECK(s.bg_uniform);
    CHECK(s.bg_estimate.r == 253);
    CHECK(s.foreground_percent > 0);
    CHECK(s.foreground_percent < 50);
    // 两个独立分量
    CHECK(s.component_count == 2);
    CHECK(s.largest_component.width == 10);
    CHECK(s.largest_component.height == 10);
}

TEST_CASE("Analyzer: mixed alpha", "[analyzer]") {
    Image img(10, 10, 0);
    img.at(0, 0).a = 255;
    img.at(1, 1).a = 128;
    ImageStats s = analyze_image(img);
    CHECK(s.opaque_pixels == 1);
    CHECK(s.semi_pixels == 1);
    CHECK(s.transparent_pixels == 98);
    CHECK(s.has_transparency);
    CHECK_FALSE(s.uniform_alpha);
}

TEST_CASE("Analyzer: many tiny components suggests min size", "[analyzer]") {
    Image img(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) img.at(x, y) = Pixel{253, 253, 253, 255};
    // 30 个 1x1 噪点（固定坐标，避开大块区域 20..39）+ 1 个大块
    const int noise_x[30] = {2,  4,  6,  8,  10, 12, 14, 16, 3,  5,  7,  9,  11, 13, 15,
                             2,  4,  6,  8,  10, 12, 14, 16, 3,  5,  7,  9,  11, 13, 15};
    const int noise_y[30] = {2,  2,  2,  2,  2,  2,  2,  2,  4,  4,  4,  4,  4,  4,  4,
                             6,  6,  6,  6,  6,  6,  6,  6,  8,  8,  8,  8,  8,  8,  8};
    for (int i = 0; i < 30; ++i) {
        img.at(noise_x[i], noise_y[i]) = Pixel{0, 0, 0, 255};
    }
    for (int y = 20; y < 40; ++y)
        for (int x = 20; x < 40; ++x) img.at(x, y) = Pixel{0, 0, 0, 255};

    ImageStats s = analyze_image(img, 12);
    CHECK(s.component_count >= 31);          // 30 噪点 + 大块
    CHECK(s.largest_component.width == 20);
    CHECK(s.suggested_min_width >= 2);       // 分量多 → 有推荐过滤值
}
