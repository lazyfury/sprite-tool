// sprite-split CLI：M3 版
//
// 用法：
//   sprite-split input.png [选项]
//   sprite-split input.png --info     # 仅输出图片信息与推荐参数，不切分
//
// 选项：
//   --output DIR            输出目录（默认 ./sprites）
//   --alpha-threshold N     alpha > N 视为前景（默认 1）
//   --padding N             精灵向外扩展像素（默认 0，须 >= 0）
//   --min-width N           过滤小于该宽度的分量（默认 1）
//   --min-height N          过滤小于该高度的分量（默认 1）
//   --remove-background     用四角采样+flood fill 去掉近纯色背景，导出透明 PNG
//   --background-threshold N 背景色距离阈值（默认 12）
//   --mode MODE             检测模式：components | grid | auto（默认 components）
//   --cell-size N           grid/auto 模式的格子尺寸（默认 16，须 >= 1）
//   --merge-distance N      合并间距阈值：先膨胀合并再腐蚀回原边界（默认 0=关）
//   --json                  同时输出 meta.json（SpriteRect 元数据）
//   --info                  分析图片并输出推荐参数（不执行切分）
//   -q, --quiet             只输出最终结果，不打印每个 sprite 明细
//   --version               显示版本号
//   --help                  显示帮助

#include "analyzer.hpp"
#include "export/json_exporter.hpp"
#include "export/png_exporter.hpp"
#include "export/sheet.hpp"
#include "image/image.hpp"
#include "mask/mask_io.hpp"
#include "segmentation/background.hpp"
#include "segmentation/splitter.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

constexpr const char* kVersion = "0.6.0";

void print_usage(const char* prog) {
    std::cout
        << "Usage: " << prog << " input.png [options]\n"
        << "\n"
        << "Modes:\n"
        << "  (default)              auto-detect and split\n"
        << "  --manual               interactively draw boxes, save meta.json, split\n"
        << "  --from-json FILE       load sprite rects from meta.json and split only\n"
        << "  --info                 analyze image and print recommended options\n"
        << "\n"
        << "Options:\n"
        << "  --output DIR             output directory (default ./sprites)\n"
        << "  --alpha-threshold N      foreground if alpha > N (default 1)\n"
        << "  --padding N              expand each sprite by N px (default 0)\n"
        << "  --min-width N            drop components narrower than N (default 1)\n"
        << "  --min-height N           drop components shorter than N (default 1)\n"
        << "  --remove-background      remove near-uniform background, export transparent\n"
        << "  --background-threshold N color distance threshold (default 12)\n"
        << "  --bg-color R,G,B         manual background color (overrides corner sampling)\n"
        << "  --contract N             shrink each sprite by N px after detection (default 0)\n"
        << "  --mode MODE              components | grid | auto (default components)\n"
        << "  --cell-size N            grid cell size for grid/auto (default 16)\n"
        << "  --merge-distance N       dilate-merge components within N px (default 0)\n"
        << "  --json                   also write meta.json with sprite rects\n"
        << "  --json-only              export meta.json only, no PNGs (stdout if no --output)\n"
        << "  --gen-masks              write eraser masks + meta.json, then split with them\n"
        << "  --erase-tl WxH           with --gen-masks: erase WxH from top-left of each mask\n"
        << "  --sheet COLS             repack sprites into a COLS-column grid sheet.png\n"
        << "  -q, --quiet              print summary only, not per-sprite lines\n"
        << "  --version                show version\n"
        << "  --help                   show this help\n"
        << "\n"
        << "Examples:\n"
        << "  " << prog << " char.png --info                          # analyze + get recommended options\n"
        << "  " << prog << " char.png --remove-background --output sprites --json\n"
        << "  " << prog << " sheet.png --remove-background --mode grid --cell-size 8 --output sprites\n"
        << "  " << prog << " char.png --merge-distance 3 --output sprites       # merge split parts\n"
        << "  " << prog << " char.png --manual --output sprites                 # draw boxes manually\n"
        << "  " << prog << " char.png --from-json meta.json --output sprites    # re-split from edited json\n"
        << "\n"
        << "Tips:\n"
        << "  - Always run --info first: it reports transparency, bg color and noisy components,\n"
        << "    then suggests a ready-to-run command.\n"
        << "  - Fully-opaque PNG (no alpha channel) needs --remove-background; without it the\n"
        << "    whole image is treated as one sprite.\n"
        << "  - Noisy sheets: filter with --min-width/--min-height (suggested by --info).\n"
        << "  - --mode auto: projection + autocorrelation + scoring (periodicity/alignment/boundary/size/occupancy); falls back to components when confidence < 0.65.\n"
        << "  - Workflow: auto-split -> edit meta.json (or --manual) -> --from-json to re-cut exactly.\n"
        << "  - Agent skill: see .pi/skills/sprite-splitter/SKILL.md for full usage guide.\n";
}

// 返回 0 正常 / 1 参数错误 / 2 运行错误
int parse_int(const std::string& arg, int& out, const std::string& name) {
    try {
        std::size_t pos = 0;
        int v = std::stoi(arg, &pos);
        if (pos != arg.size()) throw std::invalid_argument("trailing chars");
        out = v;
        return 0;
    } catch (...) {
        std::cerr << "error: invalid value for " << name << ": '" << arg << "'\n";
        return 1;
    }
}

// 把 rects 裁剪导出为 PNG（可选透明化已由调用方应用到 image 上）
// 返回导出的 sprite 数
int export_rects(const sps::Image& image, const sps::SplitResult& result,
                 const std::string& output_dir, bool quiet) {
    std::filesystem::create_directories(output_dir);
    int count = 0;
    for (std::size_t i = 0; i < result.sprites.size(); ++i) {
        const auto& r = result.sprites[i];
        ++count;
        std::string name = "sprite_" + std::to_string(count) + ".png";
        std::string path = (std::filesystem::path(output_dir) / name).string();
        sps::Image cropped = image.cropped(r.x, r.y, r.width, r.height);
        // 应用橡皮擦 mask（若存在）：mask 是灰度图，白=保留/黑=透明
        if (i < result.mask_paths.size() && !result.mask_paths[i].empty()) {
            try {
                auto alpha = sps::load_mask_alpha(result.mask_paths[i], r.width, r.height);
                sps::apply_mask(cropped, alpha);
            } catch (const std::exception& e) {
                std::cerr << "warning: " << e.what() << " (sprite " << count
                          << " exported without mask)\n";
            }
        }
        sps::save_png(cropped, path);
        if (!quiet) {
            std::cout << "  " << name << " rect=(" << r.x << "," << r.y << " "
                      << r.width << "x" << r.height << ")"
                      << (i < result.mask_paths.size() && !result.mask_paths[i].empty()
                              ? " [mask]"
                              : "")
                      << "\n";
        }
    }
    return count;
}

// 输出 meta.json：to_stdout=true 时打印到 stdout（供 UI 管道捕获，stdout 必须纯净），
// 否则写入 output_dir/meta.json 并打印路径
void emit_meta(const sps::Image& image, const sps::SplitResult& result,
               const std::string& image_name, const std::string& output_dir,
               bool to_stdout) {
    std::string json = sps::export_json(image, result, image_name);
    if (to_stdout) {
        std::cout << json << "\n";
        return;
    }
    std::filesystem::create_directories(output_dir);
    std::string meta_path = (std::filesystem::path(output_dir) / "meta.json").string();
    std::ofstream f(meta_path);
    if (!f) throw std::runtime_error("cannot open '" + meta_path + "'");
    f << json << "\n";
    std::cout << "wrote " << meta_path << "\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string input;
    std::string output_dir = "sprites";
    bool output_set = false;  // 是否显式传了 --output（区分默认目录）
    std::string from_json;
    bool manual_mode = false;
    sps::SplitOptions opts;
    bool write_json = false;
    bool json_only = false;  // 只导出 meta.json，不切 PNG
    bool gen_masks = false;  // 为每个 sprite 生成全白 mask（UI 橡皮擦起点）
    int erase_tl_w = 0, erase_tl_h = 0;  // --erase-tl WxH：左上角擦除区域（与 --gen-masks 配合）
    int sheet_cols = 0;  // --sheet COLS：把精灵重排成 COLS 列的规整 sprite sheet
    bool quiet = false;
    bool info_only = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "error: missing value for " << name << "\n";
                std::exit(1);
            }
            return argv[++i];
        };

        if (a == "--help" || a == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (a == "--version") {
            std::cout << "sprite-split " << kVersion << "\n";
            return 0;
        } else if (a == "--output") {
            output_dir = next("--output");
            output_set = true;
        } else if (a == "--alpha-threshold") {
            if (parse_int(next("--alpha-threshold"), opts.alpha_threshold,
                          "--alpha-threshold"))
                return 1;
        } else if (a == "--padding") {
            if (parse_int(next("--padding"), opts.padding, "--padding")) return 1;
        } else if (a == "--min-width") {
            if (parse_int(next("--min-width"), opts.min_width, "--min-width")) return 1;
        } else if (a == "--min-height") {
            if (parse_int(next("--min-height"), opts.min_height, "--min-height")) return 1;
        } else if (a == "--remove-background") {
            opts.remove_background = true;
        } else if (a == "--background-threshold") {
            if (parse_int(next("--background-threshold"), opts.background_threshold,
                          "--background-threshold"))
                return 1;
        } else if (a == "--bg-color") {
            std::string c = next("--bg-color");
            int r = 0, g = 0, b = 0;
            if (std::sscanf(c.c_str(), "%d,%d,%d", &r, &g, &b) != 3 || r < 0 || r > 255 ||
                g < 0 || g > 255 || b < 0 || b > 255) {
                std::cerr << "error: invalid --bg-color '" << c
                          << "' (expected R,G,B in 0..255)\n";
                return 1;
            }
            opts.has_bg_color = true;
            opts.bg_color = sps::Pixel{static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                                       static_cast<uint8_t>(b), 255};
        } else if (a == "--contract") {
            if (parse_int(next("--contract"), opts.contract, "--contract")) return 1;
        } else if (a == "--mode") {
            std::string m = next("--mode");
            if (m == "components") {
                opts.mode = sps::DetectionMode::ConnectedComponents;
            } else if (m == "grid") {
                opts.mode = sps::DetectionMode::Grid;
            } else if (m == "auto") {
                opts.mode = sps::DetectionMode::Auto;
            } else {
                std::cerr << "error: unknown mode '" << m
                          << "' (expected components|grid|auto)\n";
                return 1;
            }
        } else if (a == "--cell-size") {
            if (parse_int(next("--cell-size"), opts.grid_cell_size, "--cell-size")) return 1;
        } else if (a == "--merge-distance") {
            if (parse_int(next("--merge-distance"), opts.merge_distance, "--merge-distance"))
                return 1;
            opts.merge_nearby = true;
        } else if (a == "--json") {
            write_json = true;
        } else if (a == "--json-only") {
            json_only = true;
        } else if (a == "--gen-masks") {
            gen_masks = true;
        } else if (a == "--erase-tl") {
            std::string v = next("--erase-tl");
            if (std::sscanf(v.c_str(), "%dx%d", &erase_tl_w, &erase_tl_h) != 2 ||
                erase_tl_w <= 0 || erase_tl_h <= 0) {
                std::cerr << "error: invalid --erase-tl '" << v
                          << "' (expected WxH, e.g. 30x30)\n";
                return 1;
            }
        } else if (a == "--sheet") {
            if (parse_int(next("--sheet"), sheet_cols, "--sheet")) return 1;
            if (sheet_cols <= 0) {
                std::cerr << "error: --sheet cols must be >= 1\n";
                return 1;
            }
        } else if (a == "--info") {
            info_only = true;
        } else if (a == "--manual") {
            manual_mode = true;
        } else if (a == "--from-json") {
            from_json = next("--from-json");
        } else if (a == "-q" || a == "--quiet") {
            quiet = true;
        } else if (!a.empty() && a[0] == '-') {
            std::cerr << "error: unknown option '" << a << "'\n";
            return 1;
        } else if (input.empty()) {
            input = a;
        } else {
            std::cerr << "error: unexpected argument '" << a << "'\n";
            return 1;
        }
    }

    if (input.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    // 参数合法性校验（core 层也会抛错，这里提前给出更友好的错误与退出码）
    auto fail_arg = [](const std::string& msg) {
        std::cerr << "error: " << msg << "\n";
        std::exit(1);
    };
    if (opts.padding < 0) fail_arg("--padding must be >= 0");
    if (opts.min_width < 1) fail_arg("--min-width must be >= 1");
    if (opts.min_height < 1) fail_arg("--min-height must be >= 1");
    if (opts.grid_cell_size < 1 && opts.mode == sps::DetectionMode::Grid)
        fail_arg("--cell-size must be >= 1 in grid mode");
    if (opts.merge_nearby && opts.merge_distance < 0)
        fail_arg("--merge-distance must be >= 0");
    if (opts.merge_nearby && opts.mode != sps::DetectionMode::ConnectedComponents)
        fail_arg("--merge-distance only applies to components mode");
    if (opts.contract < 0) fail_arg("--contract must be >= 0");
    if (opts.has_bg_color && !opts.remove_background)
        fail_arg("--bg-color requires --remove-background");

    // --json-only：无 --output 时 JSON 直出 stdout，stdout 必须纯净（抑制日志）
    const bool meta_to_stdout = json_only && !output_set;
    if (json_only && manual_mode)
        fail_arg("--manual and --json-only cannot be combined (manual writes meta.json itself)");
    if (json_only && !from_json.empty())
        fail_arg("--from-json and --json-only cannot be combined (from-json already reads meta.json)");

    try {
        sps::Image image = sps::Image::load_png(input);
        if (!meta_to_stdout) {
            std::cout << "loaded " << input << " (" << image.width() << "x" << image.height()
                      << ")\n";
        }

        // ---- --info：分析并推荐，不切分 ----
        if (info_only) {
            const int bg_threshold =
                opts.remove_background ? opts.background_threshold : 12;
            sps::ImageStats s = sps::analyze_image(image, bg_threshold, opts.has_bg_color,
                                                   opts.bg_color);

            const long total = s.total_pixels;
            const double opaque_pct = total ? 100.0 * s.opaque_pixels / total : 0.0;
            const double trans_pct = total ? 100.0 * s.transparent_pixels / total : 0.0;
            const double semi_pct = total ? 100.0 * s.semi_pixels / total : 0.0;

            std::cout << "\n=== image info ===\n"
                      << "size:               " << s.width << " x " << s.height
                      << " (" << total << " px)\n"
                      << "alpha:              " << std::fixed << std::setprecision(1)
                      << opaque_pct << "% opaque, " << trans_pct << "% transparent, "
                      << semi_pct << "% semi  "
                      << (s.has_transparency ? "[has transparency]" : "[NO transparency]")
                      << "\n"
                      << "bg estimate:        rgb(" << (int)s.bg_estimate.r << ","
                      << (int)s.bg_estimate.g << "," << (int)s.bg_estimate.b << ")"
                      << (s.bg_uniform ? " [uniform corners]" : " [non-uniform]") << "\n"
                      << "foreground (t=" << bg_threshold << "): " << s.foreground_percent
                      << "% (" << s.foreground_pixels << " px)\n"
                      << "components:         " << s.component_count
                      << " (largest " << s.largest_component.width << "x"
                      << s.largest_component.height << " @ (" << s.largest_component.x
                      << "," << s.largest_component.y << "), median area "
                      << (long)s.median_component_area << ")\n";

            // ---- 推荐参数 ----
            std::cout << "\n=== recommended ===\n";
            std::vector<std::string> rec;
            if (!s.has_transparency) {
                rec.push_back("--remove-background");
                if (s.bg_uniform) {
                    rec.push_back("--background-threshold 12 (bg is uniform)");
                } else {
                    rec.push_back("--background-threshold 12 (bg not uniform; tune)");
                }
            }
            if (s.suggested_min_width > 1 || s.suggested_min_height > 1) {
                rec.push_back("--min-width " + std::to_string(s.suggested_min_width) +
                              " --min-height " + std::to_string(s.suggested_min_height) +
                              " (filter " + std::to_string(s.component_count) +
                              " components, many are small)");
            }
            if (rec.empty()) {
                rec.push_back("--alpha-threshold 1 (transparency already present)");
            }
            rec.push_back("--json (also export sprite metadata)");
            for (const auto& r : rec) std::cout << "  " << r << "\n";

            std::cout << "\n  e.g. sprite-split " << input;
            if (!s.has_transparency) std::cout << " --remove-background";
            if (s.suggested_min_width > 1)
                std::cout << " --min-width " << s.suggested_min_width << " --min-height "
                          << s.suggested_min_height;
            std::cout << " --output sprites --json\n";
            return 0;
        }

        // ---- 背景清理透明化：三种模式共用（manual/from-json 也支持透明导出） ----
        sps::Mask background;
        if (opts.remove_background) {
            sps::BackgroundOptions bg;
            bg.threshold = opts.background_threshold;
            bg.has_bg_color = opts.has_bg_color;
            bg.bg_color = opts.bg_color;
            background = sps::background_mask(image, bg);
            sps::make_background_transparent(image, background);
        }

        // ---- --manual：交互式画框，写 meta.json 并切图 ----
        if (manual_mode) {
            std::cout << "manual mode: type rects as 'x y width height' (one per line).\n"
                      << "             empty line or 'q' to finish.\n"
                      << "             image size " << image.width() << "x"
                      << image.height() << "\n";
            sps::SplitResult manual;
            std::string line;
            while (std::getline(std::cin, line)) {
                if (line.empty() || line == "q" || line == "quit") break;
                std::istringstream ss(line);
                int x, y, w, h;
                if (!(ss >> x >> y >> w >> h) || w <= 0 || h <= 0) {
                    std::cout << "  (skip) expected: x y width height\n";
                    continue;
                }
                if (x < 0 || y < 0 || x + w > image.width() || y + h > image.height()) {
                    std::cout << "  (skip) rect out of bounds (" << image.width() << "x"
                              << image.height() << ")\n";
                    continue;
                }
                manual.sprites.push_back({x, y, w, h});
                std::cout << "  added rect=(" << x << "," << y << " " << w << "x" << h
                          << ") total " << manual.sprites.size() << "\n";
            }
            if (manual.sprites.empty()) {
                std::cout << "no rects entered, nothing to do.\n";
                return 0;
            }

            int count = export_rects(image, manual, output_dir, quiet);

            // 写 meta.json（无 --json 也写，因为这是 manual 的产物）
            std::string meta_path = (std::filesystem::path(output_dir) / "meta.json").string();
            std::ofstream f(meta_path);
            if (!f) throw std::runtime_error("cannot open '" + meta_path + "'");
            f << sps::export_json(image, manual, input) << "\n";
            std::cout << "wrote " << meta_path << "\n";
            std::cout << "done: " << count << " sprite(s) -> " << output_dir << "\n";
            return 0;
        }

        // ---- --from-json：从 meta.json 加载 rects 直接切图（跳过自动检测） ----
        if (!from_json.empty()) {
            std::ifstream f(from_json);
            if (!f) {
                std::cerr << "error: cannot open '" << from_json << "'\n";
                return 2;
            }
            std::string json_text((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());
            sps::SplitResult loaded;
            if (!sps::load_json(json_text, image.width(), image.height(), loaded)) {
                std::cerr << "error: invalid meta.json '" << from_json
                          << "' (missing/invalid sprites array)\n";
                return 2;
            }
            std::cout << "loaded " << loaded.sprites.size() << " rect(s) from " << from_json
                      << "\n";

            // 解析相对 mask 路径：相对 meta.json 所在目录
            const std::string meta_dir =
                std::filesystem::path(from_json).parent_path().string();
            for (auto& mp : loaded.mask_paths) {
                if (!mp.empty() && std::filesystem::path(mp).is_relative()) {
                    mp = (std::filesystem::path(meta_dir) / mp).string();
                }
            }

            // ---- --sheet COLS：裁剪后重排成规整网格 sprite sheet ----
            if (sheet_cols > 0) {
                // 加载 mask（应用擦除）
                std::vector<std::vector<uint8_t>> masks(loaded.sprites.size());
                for (std::size_t i = 0; i < loaded.sprites.size(); ++i) {
                    if (!loaded.mask_paths[i].empty()) {
                        try {
                            masks[i] = sps::load_mask_alpha(
                                loaded.mask_paths[i], loaded.sprites[i].width,
                                loaded.sprites[i].height);
                        } catch (const std::exception& e) {
                            std::cerr << "warning: " << e.what()
                                      << " (sprite " << (i + 1) << " without mask)\n";
                        }
                    }
                }
                auto sprites = sps::crop_sprites(image, loaded.sprites, masks);
                std::vector<sps::SpriteRect> new_rects;
                sps::Image sheet = sps::repack_sheet(sprites, sheet_cols, 4, new_rects);
                std::string sheet_path =
                    (std::filesystem::path(output_dir) / "sheet.png").string();
                std::filesystem::create_directories(output_dir);
                sps::save_png(sheet, sheet_path);

                // 写 sheet_meta.json：精灵在新 sheet 中的坐标 + 原始 rect
                {
                    nlohmann::json j;
                    j["sheet"] = sheet_path;
                    j["width"] = sheet.width();
                    j["height"] = sheet.height();
                    j["sprites"] = nlohmann::json::array();
                    for (std::size_t i = 0; i < loaded.sprites.size(); ++i) {
                        j["sprites"].push_back(
                            {{"src", {{"x", loaded.sprites[i].x},
                                     {"y", loaded.sprites[i].y},
                                     {"width", loaded.sprites[i].width},
                                     {"height", loaded.sprites[i].height}}},
                             {"dst", {{"x", new_rects[i].x},
                                     {"y", new_rects[i].y},
                                     {"width", new_rects[i].width},
                                     {"height", new_rects[i].height}}}});
                    }
                    std::string smeta_path =
                        (std::filesystem::path(output_dir) / "sheet_meta.json").string();
                    std::ofstream f(smeta_path);
                    if (!f) throw std::runtime_error("cannot open '" + smeta_path + "'");
                    f << j.dump(2) << "\n";
                    std::cout << "wrote " << smeta_path << "\n";
                }
                std::cout << "done: sheet " << sheet.width() << "x" << sheet.height()
                          << " (" << loaded.sprites.size() << " sprites, " << sheet_cols
                          << " cols) -> " << sheet_path << "\n";
                return 0;
            }

            int count = export_rects(image, loaded, output_dir, quiet);
            std::cout << "done: " << count << " sprite(s) -> " << output_dir << "\n";
            return 0;
        }

        sps::SplitResult result = sps::split_image(image, opts);

        if (result.sprites.empty() && !meta_to_stdout) {
            std::cout << "warning: no sprites detected. "
                      << (opts.remove_background
                              ? "Try lowering --background-threshold, or adjust --mode / --cell-size.\n"
                              : "Image may have no transparency; try --remove-background.\n")
                      << "       hint: use --help to see all options.\n";
        }

        // ---- --json-only：只输出 meta.json（写文件或 stdout），不切 PNG ----
        if (json_only) {
            emit_meta(image, result, input, output_dir, meta_to_stdout);
            if (!meta_to_stdout) {
                std::cout << "done: " << result.sprites.size()
                          << " rect(s) -> meta.json (no pngs)\n";
            }
            return 0;
        }

        // ---- --gen-masks：为每个 sprite 生成 mask（全白或带擦除区），
        //      meta.json 写入 mask 字段，然后继续正常切图导出（应用 mask） ----
        if (gen_masks) {
            std::filesystem::create_directories(output_dir);
            const std::string mask_dir = (std::filesystem::path(output_dir) / "masks").string();
            std::filesystem::create_directories(mask_dir);
            result.mask_paths.resize(result.sprites.size());
            for (std::size_t i = 0; i < result.sprites.size(); ++i) {
                const auto& r = result.sprites[i];
                std::string mask_name = "sprite_" + std::to_string(i + 1) + "_mask.png";
                // 相对路径存进 meta.json（相对 meta.json 所在目录）
                result.mask_paths[i] =
                    (std::filesystem::path("masks") / mask_name).generic_string();
                sps::Image mask = sps::make_white_mask(r.width, r.height);
                // --erase-tl WxH：左上角涂黑擦除（mask 黑=0，alpha 乘 0 → 擦除）
                if (erase_tl_w > 0) {
                    const int ew = std::min(erase_tl_w, r.width);
                    const int eh = std::min(erase_tl_h, r.height);
                    for (int y = 0; y < eh; ++y) {
                        for (int x = 0; x < ew; ++x) {
                            mask.at(x, y).r = 0;
                            mask.at(x, y).g = 0;
                            mask.at(x, y).b = 0;
                        }
                    }
                }
                sps::save_png(mask, (std::filesystem::path(mask_dir) / mask_name).string());
            }
            if (write_json || true) {  // gen-masks 总是写 meta.json（UI 需要 mask 字段）
                std::string meta_path =
                    (std::filesystem::path(output_dir) / "meta.json").string();
                std::ofstream f(meta_path);
                if (!f) throw std::runtime_error("cannot open '" + meta_path + "'");
                f << sps::export_json(image, result, input) << "\n";
                std::cout << "wrote " << meta_path << " (+ " << result.sprites.size()
                          << " masks in " << mask_dir << ")\n";
            }
            // 把相对 mask 路径解析为绝对路径（相对 output_dir），供 export_rects 使用
            for (auto& mp : result.mask_paths) {
                if (!mp.empty() && std::filesystem::path(mp).is_relative()) {
                    mp = (std::filesystem::path(output_dir) / mp).string();
                }
            }
            // 继续正常切图导出（export_rects 会应用 mask）
        } else if (write_json) {
            std::string meta_path = (std::filesystem::path(output_dir) / "meta.json").string();
            std::ofstream f(meta_path);
            if (!f) throw std::runtime_error("cannot open '" + meta_path + "'");
            f << sps::export_json(image, result, input) << "\n";
            std::cout << "wrote " << meta_path << "\n";
        }

        int count = export_rects(image, result, output_dir, quiet);

        std::cout << "done: " << count << " sprite(s) -> " << output_dir << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }
}
