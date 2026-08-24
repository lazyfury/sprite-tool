#include "export/sheet.hpp"

#include <catch_amalgamated.hpp>

using namespace sps;

TEST_CASE("Sheet: repack arranges sprites in grid", "[sheet]") {
    std::vector<Image> sprites;
    sprites.push_back(Image(10, 10, 0));  // 全透明也占位
    sprites.push_back(Image(10, 10, 0));
    sprites.push_back(Image(10, 10, 0));
    sprites.push_back(Image(10, 10, 0));

    std::vector<SpriteRect> rects;
    Image sheet = repack_sheet(sprites, 2, 0, rects);  // 2 列，无 padding
    CHECK(sheet.width() == 20);
    CHECK(sheet.height() == 20);
    REQUIRE(rects.size() == 4);
    CHECK(rects[0].x == 0);
    CHECK(rects[0].y == 0);
    CHECK(rects[1].x == 10);
    CHECK(rects[1].y == 0);
    CHECK(rects[2].x == 0);
    CHECK(rects[2].y == 10);
    CHECK(rects[3].x == 10);
    CHECK(rects[3].y == 10);
}

TEST_CASE("Sheet: cell size fits largest sprite, others centered", "[sheet]") {
    std::vector<Image> sprites;
    sprites.push_back(Image(20, 10, 0));  // 最大宽
    sprites.push_back(Image(10, 20, 0));  // 最大高

    std::vector<SpriteRect> rects;
    Image sheet = repack_sheet(sprites, 2, 4, rects);  // 2 列，padding 4
    // cell = max(20,10)+8 = 28 x max(10,20)+8 = 28
    CHECK(sheet.width() == 28 * 2);
    CHECK(sheet.height() == 28);
    REQUIRE(rects.size() == 2);
    // sprite0 (20x10) 居中于 cell 28: ox = 4 + (28-8-20)/2 = 4+0 = 4
    CHECK(rects[0].x == 4);
    CHECK(rects[0].y == 4 + (28 - 8 - 10) / 2);  // 4+5=9
    // sprite1 (10x20) 在第二列
    CHECK(rects[1].x == 28 + 4 + (20 - 10) / 2);  // 32+5=37
}

TEST_CASE("Sheet: crop_sprites applies masks", "[sheet]") {
    Image src(10, 10, 0);
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x) src.at(x, y).a = 255;

    std::vector<SpriteRect> rects = {{0, 0, 5, 5}};
    // mask 全 0（擦除全部）
    std::vector<std::vector<uint8_t>> masks = {std::vector<uint8_t>(5 * 5, 0)};
    auto out = crop_sprites(src, rects, masks);
    REQUIRE(out.size() == 1);
    CHECK(out[0].at(0, 0).a == 0);  // 被擦除
}

TEST_CASE("Sheet: empty input yields empty sheet", "[sheet]") {
    std::vector<Image> empty;
    std::vector<SpriteRect> rects;
    CHECK(repack_sheet(empty, 4, 4, rects).empty());
    CHECK(rects.empty());
}
