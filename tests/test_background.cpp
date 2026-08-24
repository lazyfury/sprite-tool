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

TEST_CASE("Splitter: contract erodes foreground outline (free-lasso shrink)", "[background]") {
    // 自由选区收缩：8x8 块 + remove-background，contract=2 → 前景轮廓向内腐蚀 2 圈，
    // bbox 重算到腐蚀后的轮廓（只切轮廓、不切贴边内容）。
    Image img = image_with_blocks(Pixel{253, 253, 253}, {{16, 16, 8, 8}}, Pixel{0, 0, 0}, 40, 40);
    SplitOptions opts;
    opts.remove_background = true;
    auto normal = split_image(img, opts);
    REQUIRE(normal.sprites.size() == 1);
    CHECK(normal.sprites[0].x == 16);
    CHECK(normal.sprites[0].width == 8);

    opts.contract = 2;
    auto shrunk = split_image(img, opts);
    REQUIRE(shrunk.sprites.size() == 1);
    CHECK(shrunk.sprites[0].x == 18);
    CHECK(shrunk.sprites[0].y == 18);
    CHECK(shrunk.sprites[0].width == 4);
    CHECK(shrunk.sprites[0].height == 4);
}

TEST_CASE("Splitter: contract trims halo fringe after background removal", "[background]") {
    // 块外一圈 1px 过渡色（压缩/AA 毛边，dist≈24 > 阈值 12）→ 成为前景的一部分，
    // bbox 被撑到 12x12；contract=2 腐蚀后 halo 被剪掉，bbox 收紧到 8x8。
    Image img = image_with_blocks(Pixel{253, 253, 253}, {{20, 20, 10, 10}}, Pixel{0, 0, 0}, 50, 50);
    for (int y = 19; y <= 30; ++y)
        for (int x = 19; x <= 30; ++x) {
            const bool in_block = (x >= 20 && x < 30 && y >= 20 && y < 30);
            if (!in_block) img.at(x, y) = Pixel{245, 245, 245, 255};
        }

    SplitOptions opts;
    opts.remove_background = true;
    auto with_halo = split_image(img, opts);
    REQUIRE(with_halo.sprites.size() == 1);
    CHECK(with_halo.sprites[0].x == 19);
    CHECK(with_halo.sprites[0].width == 12);

    opts.contract = 2;
    auto trimmed = split_image(img, opts);
    REQUIRE(trimmed.sprites.size() == 1);
    CHECK(trimmed.sprites[0].x == 21);
    CHECK(trimmed.sprites[0].y == 21);
    CHECK(trimmed.sprites[0].width == 8);
    CHECK(trimmed.sprites[0].height == 8);
}

TEST_CASE("Splitter: contract requires remove-background and components mode", "[background]") {
    // contract 属于 remove-background 功能：alpha 模式或 grid 模式不允许
    SplitOptions opts;
    opts.contract = 2;  // alpha 模式（无 remove-background）
    CHECK_THROWS_AS(split_image(Image(8, 8), opts), std::invalid_argument);

    opts.remove_background = true;
    opts.mode = DetectionMode::Grid;  // grid 无自由选区概念
    CHECK_THROWS_AS(split_image(Image(8, 8), opts), std::invalid_argument);
}

TEST_CASE("Splitter: contract eroding whole sprite drops it", "[background]") {
    // 4x4 块腐蚀 10 圈 → 前景 mask 被完全腐蚀 → 无 sprite
    Image img = image_with_blocks(Pixel{253, 253, 253}, {{10, 10, 4, 4}}, Pixel{0, 0, 0}, 30, 30);
    SplitOptions opts;
    opts.remove_background = true;
    opts.contract = 10;
    CHECK(split_image(img, opts).sprites.empty());
}

TEST_CASE("Splitter: negative contract throws", "[background]") {
    SplitOptions opts;
    opts.contract = -1;
    CHECK_THROWS_AS(split_image(Image(8, 8), opts), std::invalid_argument);
}

TEST_CASE("Background: noisy compressed-like bg is cleaned adaptively", "[background]") {
    // 模拟 JPEG 压缩噪声：纯色背景每通道 ±5 抖动（曼哈顿距离最高 15），
    // 旧固定阈值 12 会漏掉大量背景；自适应阈值应自动放大并清干净。
    const Pixel base{64, 148, 73};
    Image img(60, 60);
    for (int y = 0; y < 60; ++y)
        for (int x = 0; x < 60; ++x) {
            const int off = ((x * 7 + y * 13) % 11) - 5;  // -5..5
            img.at(x, y) = Pixel{static_cast<uint8_t>(base.r + off),
                                 static_cast<uint8_t>(base.g + off),
                                 static_cast<uint8_t>(base.b + off), 255};
        }
    // 中心深色块（远离背景色，不应被吃）
    for (int y = 20; y < 30; ++y)
        for (int x = 20; x < 30; ++x) img.at(x, y) = Pixel{0, 0, 0, 255};

    BackgroundOptions opts;
    opts.threshold = 12;
    Mask bg = background_mask(img, opts);

    // 背景（含噪声）全部被清理
    CHECK(bg.get(0, 0));
    CHECK(bg.get(59, 59));
    CHECK(bg.get(1, 30));
    // 物体内部仍是前景
    CHECK_FALSE(bg.get(25, 25));
}

TEST_CASE("Background: transition halo near object edge is cleaned", "[background]") {
    // 纯色背景 + 物体边缘一圈「过渡相近色」（压缩/AA 造成的偏色带），
    // 应被边缘清扫吃掉，而物体内部保留。
    const Pixel base{64, 148, 73};
    Image img(50, 50);
    for (int y = 0; y < 50; ++y)
        for (int x = 0; x < 50; ++x) {
            const int off = ((x * 5 + y * 7) % 5) - 2;  // -2..2 轻噪声
            img.at(x, y) = Pixel{static_cast<uint8_t>(base.r + off),
                                 static_cast<uint8_t>(base.g + off),
                                 static_cast<uint8_t>(base.b + off), 255};
        }
    const int bx = 20, by = 20, bw = 10, bh = 10;
    for (int y = by; y < by + bh; ++y)
        for (int x = bx; x < bx + bw; ++x) img.at(x, y) = Pixel{0, 0, 0, 255};
    // 物体外一圈 1px 过渡带：颜色介于背景与物体之间（dist ≈ 24，超过严格阈值、
    // 低于清扫容差）。
    for (int y = by - 1; y <= by + bh; ++y)
        for (int x = bx - 1; x <= bx + bw; ++x) {
            const bool in_block = (x >= bx && x < bx + bw && y >= by && y < by + bh);
            if (!in_block) img.at(x, y) = Pixel{55, 140, 66, 255};
        }

    BackgroundOptions opts;
    opts.threshold = 12;
    Mask bg = background_mask(img, opts);

    CHECK(bg.get(0, 0));                     // 背景
    CHECK(bg.get(bx - 1, by));               // 过渡带（左）被清扫
    CHECK(bg.get(by, bx + bw));              // 过渡带（右）被清扫
    CHECK_FALSE(bg.get(bx + 2, by + 2));     // 物体内部仍是前景
}
