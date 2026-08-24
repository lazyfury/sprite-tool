#include "mask/morphology.hpp"
#include "segmentation/splitter.hpp"

#include <catch_amalgamated.hpp>

using namespace sps;

namespace {

Mask mask_from_rows(std::vector<std::string> rows) {
    const int h = static_cast<int>(rows.size());
    const int w = static_cast<int>(rows[0].size());
    Mask m(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (rows[y][x] == '#') m.set(x, y, true);
    return m;
}

Image image_with_alpha_blocks(std::vector<std::tuple<int, int, int, int>> blocks, int w, int h) {
    Image img(w, h, 0);
    for (const auto& [bx, by, bw, bh] : blocks) {
        for (int y = by; y < by + bh; ++y)
            for (int x = bx; x < bx + bw; ++x) img.at(x, y).a = 255;
    }
    return img;
}

}  // namespace

TEST_CASE("Morphology: dilate grows foreground by radius", "[morph]") {
    // #.
    // ..
    Mask m = mask_from_rows({"#.", ".."});
    Mask d = dilate(m, 1);
    CHECK(d.get(0, 0));
    CHECK(d.get(1, 0));  // 右
    CHECK(d.get(0, 1));  // 下
    CHECK_FALSE(d.get(1, 1));  // 对角线（4-邻域）
}

TEST_CASE("Morphology: dilate radius 0 is identity", "[morph]") {
    Mask m = mask_from_rows({"#."});
    Mask d = dilate(m, 0);
    CHECK(d.get(0, 0));
    CHECK_FALSE(d.get(1, 0));
}

TEST_CASE("Morphology: dilate bridges one-pixel gap", "[morph]") {
    // ##.##
    // ##.##
    Mask m = mask_from_rows({"##.##", "##.##"});
    Mask d = dilate(m, 1);  // 间隙被填上
    CHECK(d.get(0, 0));
    CHECK(d.get(2, 0));  // 间隙被填充
    CHECK(d.get(2, 1));
    // 注意：形态学开运算（erode∘dilate）不会分离已粘连部分；
    // splitter 的 merge 用「原 mask 重算精确 bbox」实现腐蚀回原边界（见 test_morphology 的 merge 用例）
}

TEST_CASE("Splitter: merge_nearby merges split parts", "[morph]") {
    // 同一角色被 1px 间隙拆成上下两块
    // 上块 (2,2)-(6,3)，下块 (2,5)-(6,6)：间隙 1px
    Image img = image_with_alpha_blocks({{2, 2, 5, 2}, {2, 5, 5, 2}}, 12, 10);
    SplitOptions opts;

    // 不合并：2 个分量
    auto separate = split_image(img, opts);
    REQUIRE(separate.sprites.size() == 2);

    // 合并：merge_distance=1 时膨胀 1px 连接，再腐蚀回原边界 → 1 个整体
    opts.merge_nearby = true;
    opts.merge_distance = 1;
    auto merged = split_image(img, opts);
    REQUIRE(merged.sprites.size() == 1);
    CHECK(merged.sprites[0].x == 2);
    CHECK(merged.sprites[0].y == 2);
    CHECK(merged.sprites[0].width == 5);
    CHECK(merged.sprites[0].height == 5);  // y:2..6
}

TEST_CASE("Splitter: merge_distance too small does not merge", "[morph]") {
    // 间隙 3px：dilate r=1 最大桥接 2px，3px 间隙不合并
    Image img = image_with_alpha_blocks({{2, 2, 3, 2}, {2, 7, 3, 2}}, 12, 12);
    SplitOptions opts;
    opts.merge_nearby = true;
    opts.merge_distance = 1;
    auto result = split_image(img, opts);
    REQUIRE(result.sprites.size() == 2);
}

TEST_CASE("Splitter: merge_distance bridges 2px gap", "[morph]") {
    // 间隙 2px：dilate r=1 两侧各扩展 1px 恰好桥接 → 合并为 1 个
    Image img = image_with_alpha_blocks({{2, 2, 3, 2}, {2, 6, 3, 2}}, 12, 12);
    SplitOptions opts;
    opts.merge_nearby = true;
    opts.merge_distance = 1;
    auto result = split_image(img, opts);
    REQUIRE(result.sprites.size() == 1);
}

TEST_CASE("Splitter: negative merge_distance throws", "[morph]") {
    SplitOptions opts;
    opts.merge_nearby = true;
    opts.merge_distance = -1;
    CHECK_THROWS_AS(split_image(Image(8, 8), opts), std::invalid_argument);
}
