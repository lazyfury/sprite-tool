#include "mask/mask_io.hpp"

#include "export/png_exporter.hpp"
#include <catch_amalgamated.hpp>

using namespace sps;

TEST_CASE("MaskIO: white mask preserves all pixels", "[maskio]") {
    Image mask = make_white_mask(4, 3);
    CHECK(mask.width() == 4);
    CHECK(mask.height() == 3);
    // 全白（R=255）
    CHECK(mask.at(0, 0).r == 255);
    CHECK(mask.at(3, 2).r == 255);
}

TEST_CASE("MaskIO: apply mask sets alpha from gray", "[maskio]") {
    Image img(2, 2, 0);
    img.at(0, 0).a = 255;
    img.at(1, 0).a = 255;
    img.at(0, 1).a = 255;
    img.at(1, 1).a = 255;

    // mask: 左上白(255 保留) 右上黑(0 擦除) 左下灰(128 半透明) 右下白
    // 注意：灰度图要求 R=G=B 一致，stb 灰度转换取 (R+G+B)/3
    Image mask(2, 2, 0);
    auto set_gray = [&](int x, int y, uint8_t v) {
        mask.at(x, y).r = v;
        mask.at(x, y).g = v;
        mask.at(x, y).b = v;
    };
    set_gray(0, 0, 255);
    set_gray(1, 0, 0);
    set_gray(0, 1, 128);
    set_gray(1, 1, 255);

    const std::string mask_path = "test_mask.png";
    save_png(mask, mask_path);
    auto alpha = load_mask_alpha(mask_path, 2, 2);
    std::remove(mask_path.c_str());

    apply_mask(img, alpha);
    CHECK(img.at(0, 0).a == 255);
    CHECK(img.at(1, 0).a == 0);
    CHECK(img.at(0, 1).a == 128);
    CHECK(img.at(1, 1).a == 255);
}

TEST_CASE("MaskIO: white mask preserves original transparency (multiplicative)", "[maskio]") {
    // 关键回归：全白 mask 不应把原透明像素变不透明（乘法叠加，而非覆盖）
    Image img(2, 2, 0);
    img.at(0, 0).a = 255;  // 不透明
    img.at(1, 0).a = 100;  // 半透明
    img.at(0, 1).a = 0;    // 全透明
    img.at(1, 1).a = 200;

    Image mask = make_white_mask(2, 2);  // 全 255
    const std::string mask_path = "test_mask_white.png";
    save_png(mask, mask_path);
    auto alpha = load_mask_alpha(mask_path, 2, 2);
    std::remove(mask_path.c_str());

    apply_mask(img, alpha);
    CHECK(img.at(0, 0).a == 255);  // 保留
    CHECK(img.at(1, 0).a == 100);  // 保留半透明
    CHECK(img.at(0, 1).a == 0);    // 保留全透明（背景不泛白）
    CHECK(img.at(1, 1).a == 200);  // 保留
}

TEST_CASE("MaskIO: gray mask dims semi-transparent pixel", "[maskio]") {
    // 原 alpha=200 的像素，mask=128 → 结果 = 200*128/255 ≈ 100
    Image img(1, 1, 0);
    img.at(0, 0).a = 200;
    Image mask(1, 1, 0);
    mask.at(0, 0).r = 128;
    mask.at(0, 0).g = 128;
    mask.at(0, 0).b = 128;
    const std::string mask_path = "test_mask_gray.png";
    save_png(mask, mask_path);
    auto alpha = load_mask_alpha(mask_path, 1, 1);
    std::remove(mask_path.c_str());

    apply_mask(img, alpha);
    CHECK(img.at(0, 0).a == static_cast<uint8_t>(200 * 128 / 255));  // ≈100
}

TEST_CASE("MaskIO: load smaller mask aligns to top-left", "[maskio]") {
    // mask 2x2 应用到 4x4：缺省区域保留(255)
    Image mask(2, 2, 0);
    mask.at(0, 0).r = 0;  // 左上擦除
    mask.at(1, 1).r = 0;
    const std::string mask_path = "test_mask_small.png";
    save_png(mask, mask_path);
    auto alpha = load_mask_alpha(mask_path, 4, 4);
    std::remove(mask_path.c_str());

    CHECK(alpha[0] == 0);            // (0,0) 擦除
    CHECK(alpha[1 * 4 + 1] == 0);    // (1,1) 擦除
    CHECK(alpha[2 * 4 + 2] == 255);  // (2,2) 缺省保留
    CHECK(alpha[3 * 4 + 3] == 255);
}

TEST_CASE("MaskIO: missing mask file throws", "[maskio]") {
    CHECK_THROWS_AS(load_mask_alpha("no_such_mask_xyz.png", 4, 4), std::runtime_error);
}

TEST_CASE("MaskIO: apply smaller mask throws", "[maskio]") {
    Image img(4, 4);
    std::vector<uint8_t> small(2 * 2, 255);
    CHECK_THROWS_AS(apply_mask(img, small), std::invalid_argument);
}
