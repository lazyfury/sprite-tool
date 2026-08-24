#include "segmentation/background.hpp"
#include "segmentation/splitter.hpp"
#include "export/png_exporter.hpp"

#include <catch_amalgamated.hpp>

using namespace sps;

namespace {

// 构造 WxH 图像：背景为纯色 bg，在 (x,y) 放 WxH 的不透明实心块（颜色 block）
Image image_with_blocks(Pixel bg, std::vector<std::tuple<int, int, int, int>> blocks,
                        Pixel block_color, int w, int h) {
    Image img(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            img.at(x, y).r = bg.r;
            img.at(x, y).g = bg.g;
            img.at(x, y).b = bg.b;
            img.at(x, y).a = 255;  // 无透明通道（模拟真实素材）
        }
    for (const auto& [bx, by, bw, bh] : blocks) {
        for (int y = by; y < by + bh; ++y)
            for (int x = bx; x < bx + bw; ++x) {
                img.at(x, y).r = block_color.r;
                img.at(x, y).g = block_color.g;
                img.at(x, y).b = block_color.b;
                img.at(x, y).a = 255;
            }
    }
    return img;
}

}  // namespace

TEST_CASE("Background: white bg with dark block is detected", "[background]") {
    Image img = image_with_blocks(Pixel{253, 253, 253}, {{10, 10, 5, 5}}, Pixel{0, 0, 0}, 40, 40);
    BackgroundOptions opts;
    opts.threshold = 12;
    Mask bg = background_mask(img, opts);

    // 四角是背景
    CHECK(bg.get(0, 0));
    CHECK(bg.get(39, 0));
    CHECK(bg.get(0, 39));
    CHECK(bg.get(39, 39));
    // 块内部是前景
    CHECK_FALSE(bg.get(10, 10));
    CHECK_FALSE(bg.get(14, 14));
    // 与块相邻的背景像素仍被 flood fill 标记
    CHECK(bg.get(9, 10));
    CHECK(bg.get(15, 10));
}

TEST_CASE("Background: black bg with white block", "[background]") {
    Image img = image_with_blocks(Pixel{0, 0, 0}, {{5, 5, 8, 8}}, Pixel{255, 255, 255}, 30, 30);
    BackgroundOptions opts;
    opts.threshold = 12;
    Mask bg = background_mask(img, opts);
    CHECK(bg.get(0, 0));
    CHECK_FALSE(bg.get(5, 5));
    CHECK_FALSE(bg.get(12, 12));
    CHECK(bg.get(0, 29));
}

TEST_CASE("Background: colored bg with contrasting sprite", "[background]") {
    Image img = image_with_blocks(Pixel{200, 200, 200}, {{2, 2, 3, 3}}, Pixel{0, 200, 0}, 20, 20);
    BackgroundOptions opts;
    opts.threshold = 12;
    Mask bg = background_mask(img, opts);
    CHECK(bg.get(19, 19));
    CHECK_FALSE(bg.get(2, 2));
}

TEST_CASE("Background: flood fill stops at color discontinuity", "[background]") {
    // 均匀白底 255，中间一块 200 灰（距离 55 > threshold 12）
    Image img(20, 20);
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 20; ++x) {
            uint8_t v = (10 <= x && x < 15 && 10 <= y && y < 15) ? 200 : 255;
            img.at(x, y) = Pixel{v, v, v, 255};
        }
    BackgroundOptions opts;
    opts.threshold = 12;
    Mask bg = background_mask(img, opts);
    CHECK(bg.get(0, 0));       // 背景区域
    CHECK(bg.get(19, 19));     // 背景区域
    CHECK_FALSE(bg.get(12, 12));  // 灰色块是前景（不透明且颜色远离背景）
    CHECK(bg.get(9, 12));      // 与灰块相邻的背景仍被 flood fill 标记
}

TEST_CASE("Splitter: remove_background splits white-bg sheet", "[background]") {
    Image img = image_with_blocks(Pixel{253, 253, 253}, {{2, 2, 4, 4}, {20, 20, 6, 6}},
                                  Pixel{0, 0, 0}, 40, 40);
    SplitOptions opts;
    opts.remove_background = true;
    opts.background_threshold = 12;
    auto result = split_image(img, opts);
    REQUIRE(result.sprites.size() == 2);
    CHECK(result.sprites[0].x == 2);
    CHECK(result.sprites[0].y == 2);
    CHECK(result.sprites[0].width == 4);
    CHECK(result.sprites[1].x == 20);
    CHECK(result.sprites[1].width == 6);
}

TEST_CASE("Splitter: remove_background with alpha-threshold irrelevant", "[background]") {
    // 无透明通道时，只有 remove_background 能正确切出内部精灵
    Image img = image_with_blocks(Pixel{255, 255, 255}, {{5, 5, 5, 5}}, Pixel{10, 10, 10}, 30, 30);

    SplitOptions alpha_opts;  // 默认 alpha 分割：全不透明 → 整图 1 个分量
    auto alpha_result = split_image(img, alpha_opts);
    REQUIRE(alpha_result.sprites.size() == 1);
    CHECK(alpha_result.sprites[0].width == 30);  // 整图

    SplitOptions bg_opts;
    bg_opts.remove_background = true;
    bg_opts.background_threshold = 12;
    auto result = split_image(img, bg_opts);
    REQUIRE(result.sprites.size() == 1);
    CHECK(result.sprites[0].x == 5);
    CHECK(result.sprites[0].width == 5);
}

TEST_CASE("Splitter: negative background threshold throws", "[background]") {
    SplitOptions opts;
    opts.remove_background = true;
    opts.background_threshold = -1;
    CHECK_THROWS_AS(split_image(Image(5, 5), opts), std::invalid_argument);
}

TEST_CASE("Background: make_background_transparent zeroes bg alpha", "[background]") {
    Image img = image_with_blocks(Pixel{253, 253, 253}, {{10, 10, 5, 5}}, Pixel{0, 0, 0}, 40, 40);
    BackgroundOptions opts;
    opts.threshold = 12;
    Mask bg = background_mask(img, opts);

    make_background_transparent(img, bg);
    // 背景像素 alpha=0
    CHECK(img.at(0, 0).a == 0);
    CHECK(img.at(39, 39).a == 0);
    CHECK(img.at(9, 10).a == 0);  // 与块相邻的背景
    // 前景像素保留
    CHECK(img.at(10, 10).a == 255);
    CHECK(img.at(14, 14).a == 255);
}

TEST_CASE("Background: transparency roundtrip via png", "[background]") {
    Image img = image_with_blocks(Pixel{255, 255, 255}, {{5, 5, 4, 4}}, Pixel{0, 0, 0}, 20, 20);
    BackgroundOptions opts;
    opts.threshold = 12;
    Mask bg = background_mask(img, opts);
    make_background_transparent(img, bg);

    const std::string path = "test_bg_transparent.png";
    save_png(img, path);
    Image loaded = Image::load_png(path);
    std::remove(path.c_str());

    CHECK(loaded.at(0, 0).a == 0);   // 背景透明
    CHECK(loaded.at(5, 5).a == 255); // 前景不透明
    CHECK(loaded.at(8, 8).a == 255);
}

TEST_CASE("Background: size mismatch throws", "[background]") {
    Image img(10, 10);
    Mask wrong(8, 8);
    CHECK_THROWS_AS(make_background_transparent(img, wrong), std::invalid_argument);
}

TEST_CASE("Background: manual bg color works when corners not background", "[background]") {
    // 四角被内容占满（非背景色），自动四角采样会估错；手动指定背景色可正确清理
    Image img(30, 30);
    for (int y = 0; y < 30; ++y)
        for (int x = 0; x < 30; ++x) img.at(x, y) = Pixel{250, 250, 250, 255};  // 浅背景
    // 四角放深色内容块
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) img.at(x, y) = Pixel{0, 0, 0, 255};
    for (int y = 22; y < 30; ++y)
        for (int x = 22; x < 30; ++x) img.at(x, y) = Pixel{0, 0, 0, 255};
    // 中心一块内容
    for (int y = 12; y < 18; ++y)
        for (int x = 12; x < 18; ++x) img.at(x, y) = Pixel{0, 100, 0, 255};

    BackgroundOptions opts;
    opts.threshold = 12;
    opts.has_bg_color = true;
    opts.bg_color = Pixel{250, 250, 250, 255};  // 手动指定浅灰背景
    Mask bg = background_mask(img, opts);

    // 背景像素被识别（边缘浅色区域）
    CHECK(bg.get(15, 0));
    CHECK(bg.get(0, 15));
    CHECK(bg.get(29, 29) == false);  // 右下角深色内容不是背景
    CHECK_FALSE(bg.get(12, 12));     // 中心内容
    CHECK_FALSE(bg.get(17, 17));
    CHECK(bg.get(20, 20));  // 背景区域（浅色）
}

TEST_CASE("Splitter: manual bg color splits despite occupied corners", "[background]") {
    Image img(30, 30);
    for (int y = 0; y < 30; ++y)
        for (int x = 0; x < 30; ++x) img.at(x, y) = Pixel{250, 250, 250, 255};
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) img.at(x, y) = Pixel{0, 0, 0, 255};
    for (int y = 22; y < 30; ++y)
        for (int x = 22; x < 30; ++x) img.at(x, y) = Pixel{0, 0, 0, 255};
    for (int y = 12; y < 18; ++y)
        for (int x = 12; x < 18; ++x) img.at(x, y) = Pixel{0, 100, 0, 255};

    SplitOptions opts;
    opts.remove_background = true;
    opts.background_threshold = 12;
    opts.has_bg_color = true;
    opts.bg_color = Pixel{250, 250, 250, 255};
    auto result = split_image(img, opts);

    // 三个内容块分别成为独立 sprite
    REQUIRE(result.sprites.size() == 3);
}

TEST_CASE("Splitter: contract shrinks sprite rect", "[background]") {
    Image img(20, 20, 0);
    for (int y = 4; y < 12; ++y)
        for (int x = 4; x < 12; ++x) img.at(x, y).a = 255;

    SplitOptions opts;  // 无 contract
    auto normal = split_image(img, opts);
    REQUIRE(normal.sprites.size() == 1);
    CHECK(normal.sprites[0].width == 8);
    CHECK(normal.sprites[0].x == 4);

    opts.contract = 2;  // 收缩 2px
    auto shrunk = split_image(img, opts);
    REQUIRE(shrunk.sprites.size() == 1);
    CHECK(shrunk.sprites[0].x == 6);
    CHECK(shrunk.sprites[0].y == 6);
    CHECK(shrunk.sprites[0].width == 4);
    CHECK(shrunk.sprites[0].height == 4);
}

TEST_CASE("Splitter: contract never below 1x1", "[background]") {
    // 大块 16x16 收缩 8px → 每边缩 8，剩 0x0 → clamp 到 1x1（仍在图像内）
    Image img(30, 30, 0);
    for (int y = 2; y < 18; ++y)
        for (int x = 2; x < 18; ++x) img.at(x, y).a = 255;
    SplitOptions opts;
    opts.contract = 8;
    auto result = split_image(img, opts);
    REQUIRE(result.sprites.size() == 1);
    CHECK(result.sprites[0].width == 1);
    CHECK(result.sprites[0].height == 1);
    CHECK(result.sprites[0].x == 10);
    CHECK(result.sprites[0].y == 10);
}

TEST_CASE("Splitter: contract beyond bounds drops sprite", "[background]") {
    // 1px sprite 收缩过大 → 完全移出图像 → 丢弃（PS 语义：收缩到消失）
    Image img(10, 10, 0);
    img.at(5, 5).a = 255;
    SplitOptions opts;
    opts.contract = 10;
    CHECK(split_image(img, opts).sprites.empty());
}

TEST_CASE("Splitter: negative contract throws", "[background]") {
    SplitOptions opts;
    opts.contract = -1;
    CHECK_THROWS_AS(split_image(Image(8, 8), opts), std::invalid_argument);
}
