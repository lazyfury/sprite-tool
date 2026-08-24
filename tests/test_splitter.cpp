#include "segmentation/splitter.hpp"

#include "export/png_exporter.hpp"
#include "mask/mask.hpp"

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

TEST_CASE("Splitter: auto fallback auto-filters noise fragments", "[splitter]") {
    // 2 个大块 + 30 个散布 1px 噪点（模拟白底素材表去背景后的抗锯齿碎片）：
    // 噪点位置用伪随机散布，避免排列成规则网格被 grid 检测命中
    std::vector<std::tuple<int, int, int, int>> blocks = {
        {5, 5, 40, 40}, {60, 60, 40, 40}};
    for (int i = 0; i < 30; ++i) {
        blocks.push_back({100 + (i * 7) % 90, 5 + (i * 13) % 90, 1, 1});
    }
    Image img = image_with_blocks(blocks, 200, 200);

    // components 模式默认不过滤：32 个分量
    SplitOptions comp_opts;
    REQUIRE(split_image(img, comp_opts).sprites.size() == 32);

    // auto 模式：检测不到网格回退 components，自动推导 min-size 滤噪 → 2 个大块
    SplitOptions auto_opts;
    auto_opts.mode = DetectionMode::Auto;
    auto result = split_image(img, auto_opts);
    REQUIRE(result.sprites.size() == 2);
    for (const auto& r : result.sprites) {
        CHECK(r.width == 40);
        CHECK(r.height == 40);
    }

    // 用户显式指定 min-size 时优先于自动推导（45x45 连大块也被滤掉）
    SplitOptions strict;
    strict.mode = DetectionMode::Auto;
    strict.min_width = 45;
    strict.min_height = 45;
    CHECK(split_image(img, strict).sprites.empty());
}

TEST_CASE("Splitter: external bg_mask drives detection (remote pipeline)", "[splitter]") {
    // 整图不透明灰色（无纯色背景差异），remove_background 下 color 算法会把整图
    // 当 1 个分量；外部 bg_mask 应完全决定切分，而非 color 算法。
    Image img(20, 20);
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 20; ++x) {
            img.at(x, y).r = 200;
            img.at(x, y).g = 200;
            img.at(x, y).b = 200;
            img.at(x, y).a = 255;
        }
    SplitOptions opts;
    opts.remove_background = true;
    opts.background_threshold = 12;

    // 1) 不传 bg_mask：color 算法（环带采样 → 整图纯色都当背景）→ 0 个 sprite，
    //    正好说明纯算法对非纯色/无背景差异素材失效，外部 mask 才有意义
    auto via_color = split_image(img, opts);
    CHECK(via_color.sprites.empty());

    // 2) 外部 bg_mask 全前景（无背景）→ 1 个 sprite（无变化）
    Mask no_bg(20, 20, false);
    auto via_mask_whole = split_image(img, opts, &no_bg);
    REQUIRE(via_mask_whole.sprites.size() == 1);
    CHECK(via_mask_whole.sprites[0].width == 20);
    CHECK(via_mask_whole.sprites[0].height == 20);

    // 3) 外部 bg_mask：左半为背景 → 只切出右半 10x20
    Mask left_bg(20, 20, false);
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 10; ++x) left_bg.set(x, y, true);
    auto via_mask_left = split_image(img, opts, &left_bg);
    REQUIRE(via_mask_left.sprites.size() == 1);
    CHECK(via_mask_left.sprites[0].x == 10);
    CHECK(via_mask_left.sprites[0].width == 10);
    CHECK(via_mask_left.sprites[0].height == 20);

    // 4) 外部 bg_mask 全背景 → 无 sprite（优先级高于 color 算法）
    Mask all_bg(20, 20, true);
    CHECK(split_image(img, opts, &all_bg).sprites.empty());

    // 5) 尺寸不匹配 → 抛异常
    Mask wrong(19, 20, false);
    CHECK_THROWS_AS(split_image(img, opts, &wrong), std::invalid_argument);

    // 6) 非 remove_background 模式：bg_mask 被忽略，走 alpha 分割
    SplitOptions alpha_opts;
    Mask m(20, 20, true);
    auto via_alpha = split_image(img, alpha_opts, &m);
    REQUIRE(via_alpha.sprites.size() == 1);  // 整图 alpha=255 → 1 个 sprite
}
