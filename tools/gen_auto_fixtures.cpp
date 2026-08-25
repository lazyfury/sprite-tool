// 生成 Auto 重构回归测试素材（6 类，方案 §25）：
//   tests/fixtures/auto/*.png
// 编译（链接 core）：c++ -std=c++20 -I core tools/gen_auto_fixtures.cpp build/libsps_core.a -o /tmp/gen_fix
// 用法：gen_fix <输出目录>
// 产物为透明底 PNG（alpha 分割），供 CLI / fixture 测试端到端验证 auto 决策管线。
#include "export/png_exporter.hpp"
#include "image/image.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

struct Block {
    int x, y, w, h;
};

sps::Image make_image(int w, int h, const std::vector<Block>& blocks) {
    sps::Image img(w, h, 0);
    for (const auto& b : blocks) {
        for (int y = b.y; y < b.y + b.h; ++y) {
            for (int x = b.x; x < b.x + b.w; ++x) {
                img.at(x, y).r = 255;
                img.at(x, y).g = 255;
                img.at(x, y).b = 255;
                img.at(x, y).a = 255;
            }
        }
    }
    return img;
}

bool save(const sps::Image& img, const std::string& dir, const std::string& name) {
    const std::string path = dir + "/" + name;
    try {
        sps::save_png(img, path);  // 失败抛异常
    } catch (const std::exception& e) {
        std::fprintf(stderr, "save failed: %s (%s)\n", path.c_str(), e.what());
        return false;
    }
    std::printf("wrote %s (%dx%d)\n", path.c_str(), img.width(), img.height());
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <out_dir>\n", argv[0]);
        return 1;
    }
    const std::string dir = argv[1];
    bool ok = true;

    // 01_uniform_grid：8x8 网格 cell 64，组件 62x62（≈cell，2px 间隙）→ AUTO→GRID 64
    {
        std::vector<Block> blocks;
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c) blocks.push_back({c * 64 + 1, r * 64 + 1, 62, 62});
        ok &= save(make_image(512, 512, blocks), dir, "01_uniform_grid.png");
    }
    // 02_irregular_objects：不同尺寸不同位置，无网格 → AUTO→COMPONENTS 6
    {
        std::vector<Block> blocks = {{2, 2, 12, 8}, {40, 5, 20, 30}, {10, 40, 8, 24},
                                     {50, 45, 15, 15}, {25, 20, 6, 6}, {70, 10, 30, 12}};
        ok &= save(make_image(100, 80, blocks), dir, "02_irregular_objects.png");
    }
    // 03_regular_layout_variable_bbox（鱼图场景）：4x7 cell 176，组件 120x105 → AUTO→COMPONENTS_IN_GRID 28 bbox
    {
        std::vector<Block> blocks;
        for (int r = 0; r < 7; ++r)
            for (int c = 0; c < 4; ++c) blocks.push_back({c * 176 + 28, r * 176 + 35, 120, 105});
        ok &= save(make_image(704, 1232, blocks), dir, "03_regular_layout_variable_bbox.png");
    }
    // 04_objects_touching：两块边界接触（4-邻域连通）→ AUTO→COMPONENTS 1
    {
        std::vector<Block> blocks = {{5, 5, 20, 20}, {25, 5, 10, 20}};
        ok &= save(make_image(60, 40, blocks), dir, "04_objects_touching.png");
    }
    // 05_noise：1 大块 + 30 个散布 1px 噪点 → 面积过滤 → AUTO→COMPONENTS 1
    {
        std::vector<Block> blocks = {{10, 10, 40, 40}};
        for (int i = 0; i < 30; ++i) blocks.push_back({100 + (i * 7) % 90, 5 + (i * 13) % 90, 1, 1});
        ok &= save(make_image(200, 200, blocks), dir, "05_noise.png");
    }
    // 06_empty_cells：3x3 网格（cell 32）缺中心，组件 20x20 → AUTO→COMPONENTS_IN_GRID 8
    {
        std::vector<Block> blocks;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                if (!(r == 1 && c == 1)) blocks.push_back({c * 32 + 6, r * 32 + 6, 20, 20});
        ok &= save(make_image(96, 96, blocks), dir, "06_empty_cells.png");
    }

    std::printf(ok ? "ALL FIXTURES OK\n" : "SOME FIXTURES FAILED\n");
    return ok ? 0 : 1;
}
