#include "image/image.hpp"
#include "export/png_exporter.hpp"

#include <catch_amalgamated.hpp>

#include <fstream>
#include <vector>

using namespace sps;

TEST_CASE("Image: default empty", "[image]") {
    Image img;
    CHECK(img.empty());
    CHECK(img.width() == 0);
    CHECK(img.height() == 0);
}

TEST_CASE("Image: construct and set pixels", "[image]") {
    Image img(4, 3);
    CHECK_FALSE(img.empty());
    CHECK(img.width() == 4);
    CHECK(img.height() == 3);
    CHECK(img.byte_size() == 4u * 3u * 4u);  // RGBA

    img.at(1, 2).r = 255;
    img.at(1, 2).g = 128;
    img.at(1, 2).b = 64;
    img.at(1, 2).a = 32;
    CHECK(img.at(1, 2).r == 255);
    CHECK(img.at(1, 2).g == 128);
    CHECK(img.at(1, 2).b == 64);
    CHECK(img.at(1, 2).a == 32);
}

TEST_CASE("Image: cropped extracts sub-rect", "[image]") {
    Image img(5, 5, 0);
    // 在 (1,1)-(3,3) 放一个 3x3 前景块（alpha=255）
    for (int y = 1; y <= 3; ++y)
        for (int x = 1; x <= 3; ++x) img.at(x, y).a = 255;

    Image sub = img.cropped(1, 1, 3, 3);
    CHECK(sub.width() == 3);
    CHECK(sub.height() == 3);
    CHECK(sub.at(0, 0).a == 255);
    CHECK(sub.at(2, 2).a == 255);
    // 区域外像素为 0
    CHECK(sub.at(0, 0).r == 0);
}

TEST_CASE("Image: cropped out-of-bounds throws", "[image]") {
    Image img(4, 4);
    CHECK_THROWS_AS(img.cropped(-1, 0, 2, 2), std::out_of_range);
    CHECK_THROWS_AS(img.cropped(0, 0, 5, 2), std::out_of_range);
    CHECK_THROWS_AS(img.cropped(0, 0, 0, 2), std::out_of_range);
}

TEST_CASE("Image: png roundtrip preserves pixels", "[image]") {
    Image img(3, 2, 0);
    img.at(0, 0) = Pixel{255, 0, 0, 255};
    img.at(1, 0) = Pixel{0, 255, 0, 128};
    img.at(2, 0) = Pixel{0, 0, 255, 255};
    img.at(0, 1) = Pixel{10, 20, 30, 40};
    img.at(1, 1) = Pixel{255, 255, 255, 255};
    img.at(2, 1) = Pixel{0, 0, 0, 0};

    const std::string path = "test_roundtrip.png";
    save_png(img, path);
    Image loaded = Image::load_png(path);
    std::remove(path.c_str());

    REQUIRE(loaded.width() == 3);
    REQUIRE(loaded.height() == 2);
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 3; ++x) {
            const Pixel& a = img.at(x, y);
            const Pixel& b = loaded.at(x, y);
            INFO("pixel (" << x << "," << y << ")");
            CHECK(b.r == a.r);
            CHECK(b.g == a.g);
            CHECK(b.b == a.b);
            CHECK(b.a == a.a);
        }
    }
}

TEST_CASE("Image: load nonexistent file throws", "[image]") {
    CHECK_THROWS_AS(Image::load_png("definitely_missing_file_xyz.png"), std::runtime_error);
}

TEST_CASE("Image: load_png_from_memory equals file load", "[image]") {
    // 先用文件路径加载 fixtures 图，再读原始字节走内存解码，结果应逐像素一致
    const std::string path = "tests/fixtures/test_sheet.png";
    Image from_file = Image::load_png(path);

    std::ifstream f(path, std::ios::binary);
    REQUIRE(f);
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
    Image from_memory = Image::load_png_from_memory(raw.data(), raw.size());

    REQUIRE(from_memory.width() == from_file.width());
    REQUIRE(from_memory.height() == from_file.height());
    REQUIRE(from_memory.byte_size() == from_file.byte_size());
    CHECK(std::equal(from_file.data(), from_file.data() + from_file.byte_size(),
                     from_memory.data()));
}

TEST_CASE("Image: load_png_from_memory rejects garbage", "[image]") {
    const uint8_t garbage[] = {'n', 'o', 't', 'a', 'p', 'n', 'g'};
    CHECK_THROWS_AS(Image::load_png_from_memory(garbage, sizeof(garbage)),
                    std::runtime_error);
}
