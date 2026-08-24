#include "segmentation/splitter.hpp"

#include "export/png_exporter.hpp"

#include <catch_amalgamated.hpp>

using namespace sps;

namespace {

// 构造 WxH 图像：在给定 (x,y) 放 WxH 的不透明实心块（其余全透明）
Image image_with_blocks(std::vector<std::tuple<int, int, int, int>> blocks, int w, int h) {
    Image img(w, h, 0);
    for (const auto& [bx, by, bw, bh] : blocks) {
        for (int y = by; y < by + bh; ++y)
            for (int x = bx; x < bx + bw; ++x) {
                img.at(x, y).r = 255;
                img.at(x, y).g = 255;
                img.at(x, y).b = 255;
                img.at(x, y).a = 255;
            }
    }
    return img;
}

}  // namespace

TEST_CASE("Splitter: empty image returns empty result", "[splitter]") {
    SplitOptions opts;
    CHECK(split_image(Image(), opts).sprites.empty());
}

TEST_CASE("Splitter: fully transparent image returns empty result", "[splitter]") {
    SplitOptions opts;
    CHECK(split_image(Image(10, 10, 0), opts).sprites.empty());
}

TEST_CASE("Splitter: single block is detected", "[splitter]") {
    Image img = image_with_blocks({{2, 3, 4, 5}}, 20, 20);
    SplitOptions opts;
    auto result = split_image(img, opts);
    REQUIRE(result.sprites.size() == 1);
    CHECK(result.sprites[0].x == 2);
    CHECK(result.sprites[0].y == 3);
    CHECK(result.sprites[0].width == 4);
    CHECK(result.sprites[0].height == 5);
}

TEST_CASE("Splitter: multiple separated blocks", "[splitter]") {
    Image img = image_with_blocks({{1, 1, 2, 2}, {10, 10, 3, 3}, {5, 20, 1, 1}}, 30, 30);
    SplitOptions opts;
    auto result = split_image(img, opts);
    REQUIRE(result.sprites.size() == 3);
}

TEST_CASE("Splitter: alpha threshold filters semi-transparent pixels", "[splitter]") {
    Image img(10, 10, 0);
    img.at(1, 1).a = 5;    // 半透明：threshold=1 时是前景
    img.at(8, 8).a = 128;  // 半透明：阈值 100 下仍前景
    img.at(5, 5).a = 200;  // 接近不透明

    SplitOptions opts;
    opts.alpha_threshold = 1;
    REQUIRE(split_image(img, opts).sprites.size() == 3);

    opts.alpha_threshold = 100;
    auto result = split_image(img, opts);
    REQUIRE(result.sprites.size() == 2);  // alpha=5 的被过滤
    for (const auto& r : result.sprites) {
        CHECK_FALSE((r.x == 1 && r.y == 1));
    }
}

TEST_CASE("Splitter: min size filter drops small components", "[splitter]") {
    Image img = image_with_blocks({{1, 1, 2, 2}, {10, 10, 8, 8}}, 30, 30);
    SplitOptions opts;
    opts.min_width = 4;
    opts.min_height = 4;
    auto result = split_image(img, opts);
    REQUIRE(result.sprites.size() == 1);
    CHECK(result.sprites[0].x == 10);
    CHECK(result.sprites[0].width == 8);
}

TEST_CASE("Splitter: unsupported options throw", "[splitter]") {
    Image img = image_with_blocks({{1, 1, 2, 2}}, 10, 10);

    SplitOptions grid_opts;
    grid_opts.grid_cell_size = 0;  // grid 模式非法 cell 尺寸 → grid_detect 返回空而非抛错
    grid_opts.mode = DetectionMode::Grid;
    CHECK(split_image(img, grid_opts).sprites.empty());

    SplitOptions merge_opts;
    merge_opts.merge_nearby = true;
    merge_opts.merge_distance = -1;
    CHECK_THROWS_AS(split_image(img, merge_opts), std::invalid_argument);
}

TEST_CASE("Splitter: negative threshold throws", "[splitter]") {
    SplitOptions opts;
    opts.alpha_threshold = -1;
    CHECK_THROWS_AS(split_image(Image(5, 5), opts), std::invalid_argument);
}

TEST_CASE("Splitter: e2e crop then png roundtrip", "[splitter]") {
    Image img = image_with_blocks({{2, 2, 4, 4}}, 16, 16);
    SplitOptions opts;
    auto result = split_image(img, opts);
    REQUIRE(result.sprites.size() == 1);
    const auto& r = result.sprites[0];
    Image cropped = img.cropped(r.x, r.y, r.width, r.height);
    CHECK(cropped.width() == 4);
    CHECK(cropped.height() == 4);
    CHECK(cropped.at(0, 0).a == 255);
    CHECK(cropped.at(3, 3).a == 255);

    const std::string path = "test_e2e_crop.png";
    save_png(cropped, path);
    Image loaded = Image::load_png(path);
    std::remove(path.c_str());
    CHECK(loaded.width() == 4);
    CHECK(loaded.height() == 4);
}
