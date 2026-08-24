#include "export/json_exporter.hpp"

#include <catch_amalgamated.hpp>

using namespace sps;

TEST_CASE("JSON: export_json serializes rects", "[json]") {
    Image img(100, 50);
    SplitResult result;
    result.sprites.push_back({1, 2, 30, 40});
    result.sprites.push_back({50, 5, 10, 20});

    std::string json = export_json(img, result, "sheet.png");
    CHECK(json.find("\"image\": \"sheet.png\"") != std::string::npos);
    CHECK(json.find("\"width\": 100") != std::string::npos);
    CHECK(json.find("\"height\": 50") != std::string::npos);
    CHECK(json.find("\"x\": 1") != std::string::npos);
    CHECK(json.find("\"y\": 2") != std::string::npos);
    CHECK(json.find("\"width\": 30") != std::string::npos);
    CHECK(json.find("\"x\": 50") != std::string::npos);
    // 两个 sprite
    CHECK(json.find("\"sprites\"") != std::string::npos);
}

TEST_CASE("JSON: empty result yields empty sprites array", "[json]") {
    Image img(10, 10);
    SplitResult result;
    std::string json = export_json(img, result, "empty.png");
    CHECK(json.find("\"sprites\": []") != std::string::npos);
}

TEST_CASE("JSON: roundtrip export then load", "[json]") {
    Image img(100, 50);
    SplitResult result;
    result.sprites.push_back({1, 2, 30, 40});
    result.sprites.push_back({50, 5, 10, 20});

    std::string json = export_json(img, result, "sheet.png");
    SplitResult loaded;
    REQUIRE(load_json(json, 100, 50, loaded));
    REQUIRE(loaded.sprites.size() == 2);
    CHECK(loaded.sprites[0].x == 1);
    CHECK(loaded.sprites[0].y == 2);
    CHECK(loaded.sprites[0].width == 30);
    CHECK(loaded.sprites[0].height == 40);
    CHECK(loaded.sprites[1].x == 50);
    CHECK(loaded.sprites[1].width == 10);
}

TEST_CASE("JSON: load clamps out-of-bounds rects", "[json]") {
    Image img(50, 50);
    SplitResult loaded;
    std::string json = R"({"sprites":[{"x":-5,"y":10,"width":100,"height":200}]})";
    REQUIRE(load_json(json, 50, 50, loaded));
    REQUIRE(loaded.sprites.size() == 1);
    CHECK(loaded.sprites[0].x == 0);   // clamp 到 0
    CHECK(loaded.sprites[0].y == 10);
    CHECK(loaded.sprites[0].width == 50);   // clamp 到 50-0
    CHECK(loaded.sprites[0].height == 40);  // clamp 到 50-10
}

TEST_CASE("JSON: load skips invalid entries and rejects bad json", "[json]") {
    SplitResult loaded;
    // 缺字段的条目跳过
    std::string json1 = R"({"sprites":[{"x":1,"y":1,"width":5,"height":5},{"x":1,"y":1}]})";
    REQUIRE(load_json(json1, 50, 50, loaded));
    REQUIRE(loaded.sprites.size() == 1);

    // 完全非法 JSON
    SplitResult loaded2;
    CHECK_FALSE(load_json("not json at all", 50, 50, loaded2));

    // 缺 sprites 字段
    SplitResult loaded3;
    CHECK_FALSE(load_json("{\"width\":50}", 50, 50, loaded3));
}
