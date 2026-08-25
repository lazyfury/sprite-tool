// sprite-split CLI：v0.9 解耦版（CLI11 子命令）
//
// 用法：
//   sprite-split <command> [args]
//
// 子命令（背景移除与切分分离，两命令管道组合）：
//   info <input>                      分析图片，输出统计 + 两步推荐（不切分）
//   remove-background <input> [flags] 去背景，整图导出透明 PNG（--stdout 可直出管道）
//   split <input> [flags]             透明图检测 + 切分导出（components/grid/auto）
//   manual <input> [flags]            交互式画框 + 切分导出（始终写 meta.json）
//   from-json <input> <meta.json>     从 meta.json 加载 rects 直接切图
//   sheet <input> --cols N [flags]    重排为规整 sprite sheet（支持 --from-json）
//
// 通用 flag：
//   --output DIR          输出目录（默认 ./out/sprites）
//   --format json|text    结构化 JSON 结果输出到 stdout（默认 text；json 模式
//                         下 stdout 只含结果对象，进度/日志走 stderr，便于管道）
//   -q, --quiet           文本模式：只输出最终摘要
//   --version             版本号
//   --help                帮助（子命令后跟 --help 查看该命令专属帮助）
//
// 管道：
//   remove-background --stdout 输出 PNG 二进制到 stdout（与 --format json 互斥）
//   所有命令 input 支持 '-'（从 stdin 读 PNG）
//
// 退出码：0 正常 / 1 参数错误 / 2 运行错误

#include "analyzer.hpp"
#include "bg_remote.hpp"  // extra：sps_bg_remote（Remote 后端注册）
#include "export/json_exporter.hpp"
#include "export/png_exporter.hpp"
#include "export/sheet.hpp"
#include "image/image.hpp"
#include "mask/mask.hpp"
#include "mask/mask_io.hpp"
#include "segmentation/background.hpp"
#include "segmentation/background_remover.hpp"
#include "segmentation/splitter.hpp"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "skill_prompt.hpp"  // 构建时嵌入的 SKILL.md（--prompt 输出）

namespace {

constexpr const char* kVersion = "0.9.1";

// ============================ 输出抽象 ============================
// --format json：stdout 只输出一个结果对象（res），人类可读信息走 stderr；
// --stdout：stdout 只输出 PNG 二进制（force_stderr=true，一切人类可读走 stderr）。
struct Out {
    bool json = false;         // --format json
    bool quiet = false;        // -q/--quiet
    bool force_stderr = false; // --stdout 模式：note 一律走 stderr（stdout 留给二进制）
    nlohmann::json res;        // json 模式的结果对象（stdout 唯一输出）

    void note(const std::string& s) const {
        if (json || force_stderr)
            std::cerr << s << "\n";
        else
            std::cout << s << "\n";
    }
    static void warn(const std::string& s) { std::cerr << "warning: " << s << "\n"; }
    void finish() const {
        if (json) std::cout << res.dump() << "\n";
    }
};

// ============================ 共享选项 ============================

struct CommonOpts {
    std::string output_dir = "out/sprites";
    bool output_set = false;
    bool json_format = false;
    bool quiet = false;
    std::string format_str = "text";
};

struct CliOpts {
    CommonOpts common;
    sps::SplitOptions opts;  // split/sheet 检测选项
    bool write_json = false;
    bool json_only = false;
    bool gen_masks = false;
    int erase_tl_w = 0, erase_tl_h = 0;   // --erase-tl WxH
    int sheet_cols = 0;                   // sheet --cols N
    std::string from_json;                // sheet --from-json FILE
    sps::BackgroundOptions bg;            // remove-background
    bool bg_backend_remote = false;       // remove-background --bg-backend remote
    std::string bg_url = "http://127.0.0.1:8000";  // remote 服务 base URL
    bool stdout_mode = false;             // remove-background --stdout
    // CLI11 绑定的字符串（生命周期 = 本结构，回调可安全引用）
    std::string mode_str = "components";
    std::string bg_color_str;
    std::string bg_backend_str = "color";
};

// 参数/运行错误：继承 std::exception，统一被 main 的 catch 捕获
struct ArgError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

// ============================ 图像加载（input='-' 走 stdin） ============================

sps::Image load_input(const std::string& input, Out& out) {
    if (input == "-") {
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(std::cin)),
                                   std::istreambuf_iterator<char>());
        if (bytes.empty()) throw std::runtime_error("empty stdin (no PNG data)");
        sps::Image img = sps::Image::load_png_from_memory(bytes.data(), bytes.size());
        out.note("loaded <stdin> (" + std::to_string(img.width()) + "x" +
                 std::to_string(img.height()) + ")");
        return img;
    }
    sps::Image img = sps::Image::load_png(input);
    out.note("loaded " + input + " (" + std::to_string(img.width()) + "x" +
             std::to_string(img.height()) + ")");
    return img;
}

// ============================ 通用逻辑 ============================

// 检测选项合法性校验（split/sheet 共用）；非法时抛 ArgError
void validate_split_opts(const CliOpts& o) {
    if (o.opts.min_width < 1) throw ArgError{"--min-width must be >= 1"};
    if (o.opts.min_height < 1) throw ArgError{"--min-height must be >= 1"};
    if (o.opts.mode == sps::DetectionMode::Grid && o.opts.grid_cell_size < 1)
        throw ArgError{"--cell-size must be >= 1 in grid mode"};
    if (o.opts.merge_nearby && o.opts.mode == sps::DetectionMode::Grid)
        throw ArgError{"--merge-distance only applies to components/auto mode"};
    if (o.opts.alpha_threshold < 0) throw ArgError{"--alpha-threshold must be >= 0"};
}

// 输出 meta.json：to_stdout=true 时直接输出（text→stdout 原始 JSON，json→嵌入 res.meta，
// 供 UI/管道捕获，stdout 必须纯净）；否则写入 output_dir/meta.json 并记录路径
void emit_meta(const sps::Image& image, const sps::SplitResult& result,
               const std::string& image_name, const std::string& output_dir,
               bool to_stdout, Out& out) {
    const std::string json = sps::export_json(image, result, image_name);
    if (to_stdout) {
        if (out.json) {
            out.res["meta"] = nlohmann::json::parse(json);
        } else {
            std::cout << json << "\n";
        }
        return;
    }
    std::filesystem::create_directories(output_dir);
    const std::string meta_path = (std::filesystem::path(output_dir) / "meta.json").string();
    std::ofstream f(meta_path);
    if (!f) throw std::runtime_error("cannot open '" + meta_path + "'");
    f << json << "\n";
    out.note("wrote " + meta_path);
    if (out.json) out.res["meta_path"] = meta_path;
}

// 把 rects 裁剪导出为 PNG。文本模式打印明细（-q 抑制）；json 模式收集进 res["sprites"]。
int export_rects(const sps::Image& image, const sps::SplitResult& result,
                 const std::string& output_dir, Out& out) {
    std::filesystem::create_directories(output_dir);
    if (out.json) out.res["sprites"] = nlohmann::json::array();
    int count = 0;
    for (std::size_t i = 0; i < result.sprites.size(); ++i) {
        const auto& r = result.sprites[i];
        ++count;
        const std::string name = "sprite_" + std::to_string(count) + ".png";
        const std::string path = (std::filesystem::path(output_dir) / name).string();
        sps::Image cropped = image.cropped(r.x, r.y, r.width, r.height);
        // 应用橡皮擦 mask（若存在）：mask 是灰度图，白=保留/黑=透明
        bool has_mask = false;
        if (i < result.mask_paths.size() && !result.mask_paths[i].empty()) {
            try {
                auto alpha = sps::load_mask_alpha(result.mask_paths[i], r.width, r.height);
                sps::apply_mask(cropped, alpha);
                has_mask = true;
            } catch (const std::exception& e) {
                Out::warn(std::string(e.what()) + " (sprite " + std::to_string(count) +
                          " exported without mask)");
            }
        }
        sps::save_png(cropped, path);
        if (!out.json && !out.quiet) {
            std::cout << "  " << name << " rect=(" << r.x << "," << r.y << " "
                      << r.width << "x" << r.height << ")"
                      << (has_mask ? " [mask]" : "") << "\n";
        }
        if (out.json) {
            out.res["sprites"].push_back({{"name", name},
                                          {"x", r.x},
                                          {"y", r.y},
                                          {"width", r.width},
                                          {"height", r.height},
                                          {"mask", has_mask}});
        }
    }
    return count;
}

void print_runtime_error(const Out& out, const std::exception& e) {
    if (out.json) {
        std::cerr << nlohmann::json{{"status", "error"}, {"error", e.what()}}.dump()
                  << "\n";
    } else {
        std::cerr << "error: " << e.what() << "\n";
    }
}

// Auto 模式诊断输出（方案 §24）：文本 → note；json → 嵌入 out.res["auto"]。
// 仅 mode=Auto 时填充（auto_mode >= 0）。
void emit_auto_analysis(const sps::SplitResult& result, Out& out) {
    if (result.auto_mode < 0) return;
    const char* mode_name = result.auto_mode == 0
                                ? "COMPONENTS"
                                : (result.auto_mode == 1 ? "GRID" : "COMPONENTS_IN_GRID");
    if (out.json) {
        out.res["auto"] = {
            {"mode", mode_name},
            {"confidence", std::round(result.auto_confidence * 100.0) / 100.0},
            {"raw_components", result.auto_raw_components},
            {"filtered_components", result.auto_filtered_components},
            {"merged_components", result.auto_merged_components},
            {"grid_columns", result.auto_grid_columns},
            {"grid_rows", result.auto_grid_rows},
            {"cell_width", result.auto_grid_cell_w},
            {"cell_height", result.auto_grid_cell_h},
            {"occupied_cells", result.auto_occupied_cells},
            {"cells_with_multi", result.auto_cells_with_multi},
        };
        return;
    }
    const bool detected = result.auto_grid_columns > 0;
    std::ostringstream ss;
    ss << "AUTO ANALYSIS\n"
       << "  components:\n"
       << "    raw: " << result.auto_raw_components << "\n"
       << "    filtered: " << result.auto_filtered_components << "\n"
       << "    merged: " << result.auto_merged_components << "\n"
       << "  grid:\n"
       << "    detected: " << (detected ? "true" : "false") << "\n";
    if (detected) {
        ss << "    cell: " << result.auto_grid_cell_w << "x" << result.auto_grid_cell_h
           << "\n"
           << "    layout: " << result.auto_grid_columns << "x" << result.auto_grid_rows
           << "\n"
           << "    confidence: " << std::fixed << std::setprecision(2)
           << result.auto_confidence << "\n"
           << "  mapping:\n"
           << "    occupied cells: " << result.auto_occupied_cells << "\n"
           << "    cells with 1 component: "
           << (result.auto_occupied_cells - result.auto_cells_with_multi) << "\n"
           << "    cells with >1 component: " << result.auto_cells_with_multi << "\n";
    }
    ss << "  decision:\n"
       << "    " << mode_name << "\n"
       << "  result:\n"
       << "    sprites: " << result.sprites.size();
    out.note(ss.str());
}

// remove-background 背景清理（整图透明导出专用）：
// remote 成功时直接采用服务端透明图（保留 AI 软边 alpha）；失败/不可达 → warning + 回退 color。
// 返回是否实际走了 remote 后端。
bool remove_background(sps::Image& image, const CliOpts& o) {
    const auto kind = o.bg_backend_remote ? sps::BackgroundBackend::Remote
                                          : sps::BackgroundBackend::Color;
    sps::BackgroundRemoverOptions opts;
    opts.color = o.bg;
    opts.remote_url = o.bg_url;

    auto remover = sps::create_background_remover(kind, opts);
    if (!remover) {
        throw ArgError{"--bg-backend remote unavailable (sps_bg_remote not linked)"};
    }

    bool used_remote = false;
    if (o.bg_backend_remote) {
        std::cerr << "note: remote background backend: " << o.bg_url << "\n";
        try {
            sps::Image transparent = remover->process_transparent(image);
            if (transparent.width() != image.width() ||
                transparent.height() != image.height()) {
                throw std::runtime_error(
                    "remote bg returned different size (" +
                    std::to_string(transparent.width()) + "x" +
                    std::to_string(transparent.height()) + ")");
            }
            image = std::move(transparent);
            used_remote = true;
        } catch (const std::exception& e) {
            Out::warn(std::string("remote background backend failed (") + e.what() +
                      "); falling back to color backend");
            remover = sps::create_background_remover(sps::BackgroundBackend::Color, opts);
            sps::Mask background = remover->process(image);
            sps::make_background_transparent(image, background);
            used_remote = false;
        }
    } else {
        sps::Mask background = remover->process(image);
        sps::make_background_transparent(image, background);
        used_remote = false;
    }
    return used_remote;
}

// 解析 "R,G,B" → Pixel（--bg-color）
sps::Pixel parse_bg_color(const std::string& v) {
    int r = 0, g = 0, b = 0;
    if (std::sscanf(v.c_str(), "%d,%d,%d", &r, &g, &b) != 3 || r < 0 || r > 255 ||
        g < 0 || g > 255 || b < 0 || b > 255) {
        throw ArgError{"invalid --bg-color '" + v + "' (expected R,G,B in 0..255)"};
    }
    return {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b), 255};
}

// ============================ 子命令 handler ============================
// 签名：run_xxx(const CmdArgs&, const CliOpts&, Out&) → 退出码

struct CmdArgs {
    std::string input;
    std::string meta_path;
};

int run_info(const CmdArgs& args, const CliOpts& o, Out& out) {
    (void)o;
    try {
        sps::Image image = load_input(args.input, out);
        const int bg_threshold = 12;
        sps::ImageStats s = sps::analyze_image(image, bg_threshold);

        const long total = s.total_pixels;
        const double opaque_pct = total ? 100.0 * s.opaque_pixels / total : 0.0;
        const double trans_pct = total ? 100.0 * s.transparent_pixels / total : 0.0;
        const double semi_pct = total ? 100.0 * s.semi_pixels / total : 0.0;

        // ---- 两步推荐（先 remove-background 再 split；输入已是透明图则直接 split） ----
        const std::string stem =
            (args.input == "-") ? "<stdin>" : std::filesystem::path(args.input).stem().string();
        std::vector<std::string> rec;
        if (!s.has_transparency) {
            rec.push_back("step 1: sprite-split remove-background " + args.input +
                          " --output tmp");
            rec.push_back("step 2: sprite-split split tmp/" + stem +
                          "_transparent.png (see below for split flags)");
        } else {
            rec.push_back("split (input already has transparency):");
        }
        if (s.suggested_min_width > 1 || s.suggested_min_height > 1) {
            rec.push_back("--min-width " + std::to_string(s.suggested_min_width) +
                          " --min-height " + std::to_string(s.suggested_min_height) +
                          " (filter " + std::to_string(s.component_count) +
                          " components, many are small)");
        }
        rec.push_back("--json (also export sprite metadata)");

        std::string example = "sprite-split split " + args.input;
        if (s.suggested_min_width > 1)
            example += " --min-width " + std::to_string(s.suggested_min_width) +
                       " --min-height " + std::to_string(s.suggested_min_height);
        example += " --output out/sprites --json";

        if (out.json) {
            out.res["width"] = s.width;
            out.res["height"] = s.height;
            out.res["total_pixels"] = total;
            out.res["opaque_percent"] = std::round(opaque_pct * 10.0) / 10.0;
            out.res["transparent_percent"] = std::round(trans_pct * 10.0) / 10.0;
            out.res["semi_percent"] = std::round(semi_pct * 10.0) / 10.0;
            out.res["has_transparency"] = s.has_transparency;
            out.res["bg_estimate"] = {{"r", static_cast<int>(s.bg_estimate.r)},
                                      {"g", static_cast<int>(s.bg_estimate.g)},
                                      {"b", static_cast<int>(s.bg_estimate.b)}};
            out.res["bg_uniform"] = s.bg_uniform;
            out.res["foreground_percent"] = s.foreground_percent;
            out.res["foreground_pixels"] = s.foreground_pixels;
            out.res["components"] = s.component_count;
            out.res["largest_component"] = {
                {"x", s.largest_component.x},
                {"y", s.largest_component.y},
                {"width", s.largest_component.width},
                {"height", s.largest_component.height}};
            out.res["median_component_area"] = static_cast<long>(s.median_component_area);
            out.res["recommended"] = rec;
            out.res["example"] = example;
        } else {
            std::cout << "\n=== image info ===\n"
                      << "size:               " << s.width << " x " << s.height << " ("
                      << total << " px)\n"
                      << "alpha:              " << std::fixed << std::setprecision(1)
                      << opaque_pct << "% opaque, " << trans_pct << "% transparent, "
                      << semi_pct << "% semi  "
                      << (s.has_transparency ? "[has transparency]" : "[NO transparency]")
                      << "\n"
                      << "bg estimate:        rgb(" << static_cast<int>(s.bg_estimate.r)
                      << "," << static_cast<int>(s.bg_estimate.g) << ","
                      << static_cast<int>(s.bg_estimate.b) << ")"
                      << (s.bg_uniform ? " [uniform]" : " [non-uniform]") << "\n"
                      << "foreground (t=" << bg_threshold << "): " << s.foreground_percent
                      << "% (" << s.foreground_pixels << " px)\n"
                      << "components:         " << s.component_count << " (largest "
                      << s.largest_component.width << "x" << s.largest_component.height
                      << " @ (" << s.largest_component.x << "," << s.largest_component.y
                      << "), median area " << static_cast<long>(s.median_component_area)
                      << ")\n"
                      << "\n=== recommended ===\n";
            for (const auto& r : rec) std::cout << "  " << r << "\n";
            std::cout << "\n  e.g. " << example << "\n";
        }
        out.finish();
        return 0;
    } catch (const std::exception& e) {
        print_runtime_error(out, e);
        return 2;
    }
}

int run_remove_background(const CmdArgs& args, const CliOpts& o, Out& out) {
    try {
        sps::Image image = load_input(args.input, out);

        // 背景色参考：用户指定优先；color 后端未指定时取环带估计。
        // （在透明化之前采样，RGB 不受 alpha 置零影响）
        nlohmann::json bg_color_json;
        if (o.bg.has_bg_color) {
            bg_color_json = {{"r", static_cast<int>(o.bg.bg_color.r)},
                             {"g", static_cast<int>(o.bg.bg_color.g)},
                             {"b", static_cast<int>(o.bg.bg_color.b)}};
        } else if (!o.bg_backend_remote) {
            const auto est = sps::estimate_background(image);
            bg_color_json = {{"r", static_cast<int>(est.color.r)},
                             {"g", static_cast<int>(est.color.g)},
                             {"b", static_cast<int>(est.color.b)}};
        }

        const bool used_remote = remove_background(image, o);

        // 背景像素统计：两种后端统一按结果图 alpha==0 计数
        // （color 二值透明化后背景 alpha=0；remote keep_alpha 成功路径同样 alpha=0）
        long bg_px = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (image.at(x, y).a == 0) ++bg_px;
        const long total = static_cast<long>(image.width()) * image.height();
        const double bg_pct = total ? 100.0 * bg_px / total : 0.0;
        const std::string backend = used_remote ? "remote" : "color";

        if (o.stdout_mode) {
            // 真管道：PNG 二进制直出 stdout；一切人类可读信息走 stderr
            const std::vector<uint8_t> png = sps::encode_png(image);
            std::cout.write(reinterpret_cast<const char*>(png.data()),
                            static_cast<std::streamsize>(png.size()));
            std::cout.flush();
            std::cerr << "done: transparent PNG (" << image.width() << "x"
                      << image.height() << ", bg removed "
                      << static_cast<long>(std::round(bg_pct)) << "%, backend=" << backend
                      << ") -> <stdout>\n";
            return 0;
        }

        // 输出文件名：<stem>_transparent.png（保持原图尺寸，整图透明导出）
        std::filesystem::create_directories(o.common.output_dir);
        const std::string stem =
            (args.input == "-") ? "stdin" : std::filesystem::path(args.input).stem().string();
        const std::string out_name = (stem.empty() ? "transparent" : stem) + "_transparent.png";
        const std::string out_path =
            (std::filesystem::path(o.common.output_dir) / out_name).string();
        sps::save_png(image, out_path);

        if (out.json) {
            out.res["output"] = out_path;
            out.res["width"] = image.width();
            out.res["height"] = image.height();
            out.res["background_pixels"] = bg_px;
            out.res["background_percent"] = std::round(bg_pct * 10.0) / 10.0;
            out.res["bg_backend"] = backend;
            if (!bg_color_json.is_null()) out.res["bg_color"] = bg_color_json;
        } else {
            std::string summary = "done: " + out_name + " (" +
                                  std::to_string(image.width()) + "x" +
                                  std::to_string(image.height()) + ", bg removed " +
                                  std::to_string(static_cast<long>(std::round(bg_pct))) +
                                  "%, backend=" + backend;
            if (!bg_color_json.is_null()) {
                summary += ", bg=rgb(" + std::to_string(bg_color_json["r"].get<int>()) +
                           "," + std::to_string(bg_color_json["g"].get<int>()) + "," +
                           std::to_string(bg_color_json["b"].get<int>()) + ")";
            }
            summary += ") -> " + out_path;
            std::cout << summary << "\n";
        }
        out.finish();
        return 0;
    } catch (const std::exception& e) {
        print_runtime_error(out, e);
        return 2;
    }
}

int run_split(const CmdArgs& args, const CliOpts& o, Out& out) {
    const bool meta_to_stdout = o.json_only && !o.common.output_set;
    const bool silent_stdout = meta_to_stdout && !out.json;

    try {
        sps::Image image = load_input(args.input, out);
        // 输入恒为透明图（背景移除由 remove-background 命令完成）→ 纯 alpha 切分
        sps::SplitResult result = sps::split_image(image, o.opts);
        emit_auto_analysis(result, out);

        if (result.sprites.empty()) {
            std::string w =
                "no sprites detected. Input must have transparency (alpha > "
                "--alpha-threshold); if not, run 'sprite-split remove-background' first.";
            if (out.json)
                out.res["warning"] = w;
            else if (!silent_stdout)
                Out::warn(w);
        }

        if (o.json_only) {
            emit_meta(image, result, args.input, o.common.output_dir, meta_to_stdout, out);
            if (out.json) {
                out.res["count"] = static_cast<int>(result.sprites.size());
                out.res["json_only"] = true;
            } else if (!silent_stdout) {
                std::cout << "done: " << result.sprites.size()
                          << " rect(s) -> meta.json (no pngs)\n";
            }
            out.finish();
            return 0;
        }

        // ---- --gen-masks：为每个 sprite 生成 mask（全白或带擦除区），
        //      meta.json 写入 mask 字段，然后继续正常切图导出（应用 mask） ----
        if (o.gen_masks) {
            std::filesystem::create_directories(o.common.output_dir);
            const std::string mask_dir =
                (std::filesystem::path(o.common.output_dir) / "masks").string();
            std::filesystem::create_directories(mask_dir);
            result.mask_paths.resize(result.sprites.size());
            for (std::size_t i = 0; i < result.sprites.size(); ++i) {
                const auto& r = result.sprites[i];
                const std::string mask_name =
                    "sprite_" + std::to_string(i + 1) + "_mask.png";
                // 相对路径存进 meta.json（相对 meta.json 所在目录）
                result.mask_paths[i] =
                    (std::filesystem::path("masks") / mask_name).generic_string();
                sps::Image mask = sps::make_white_mask(r.width, r.height);
                // --erase-tl WxH：左上角涂黑擦除（mask 黑=0，alpha 乘 0 → 擦除）
                if (o.erase_tl_w > 0) {
                    const int ew = std::min(o.erase_tl_w, r.width);
                    const int eh = std::min(o.erase_tl_h, r.height);
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
            // gen-masks 总是写 meta.json（UI 需要 mask 字段）
            const std::string meta_path =
                (std::filesystem::path(o.common.output_dir) / "meta.json").string();
            std::ofstream f(meta_path);
            if (!f) throw std::runtime_error("cannot open '" + meta_path + "'");
            f << sps::export_json(image, result, args.input) << "\n";
            out.note("wrote " + meta_path + " (+ " + std::to_string(result.sprites.size()) +
                     " masks in " + mask_dir + ")");
            if (out.json) {
                out.res["meta_path"] = meta_path;
                out.res["masks_dir"] = mask_dir;
            }
            // 相对 mask 路径解析为绝对（相对 output_dir），供 export_rects 使用
            for (auto& mp : result.mask_paths) {
                if (!mp.empty() && std::filesystem::path(mp).is_relative()) {
                    mp = (std::filesystem::path(o.common.output_dir) / mp).string();
                }
            }
        } else if (o.write_json) {
            std::filesystem::create_directories(o.common.output_dir);
            const std::string meta_path =
                (std::filesystem::path(o.common.output_dir) / "meta.json").string();
            std::ofstream f(meta_path);
            if (!f) throw std::runtime_error("cannot open '" + meta_path + "'");
            f << sps::export_json(image, result, args.input) << "\n";
            out.note("wrote " + meta_path);
            if (out.json) out.res["meta_path"] = meta_path;
        }

        const int count = export_rects(image, result, o.common.output_dir, out);
        if (out.json) {
            out.res["count"] = count;
            out.res["output_dir"] = o.common.output_dir;
        } else {
            std::cout << "done: " << count << " sprite(s) -> " << o.common.output_dir
                      << "\n";
        }
        out.finish();
        return 0;
    } catch (const std::exception& e) {
        print_runtime_error(out, e);
        return 2;
    }
}

int run_manual(const CmdArgs& args, const CliOpts& o, Out& out) {
    if (args.input == "-") {
        std::cerr << "error: manual mode reads rects from stdin; input from stdin ('-') "
                     "not supported\n";
        return 1;
    }
    try {
        sps::Image image = load_input(args.input, out);

        out.note("manual mode: type rects as 'x y width height' (one per line).");
        out.note("             empty line or 'q' to finish.");
        out.note("             image size " + std::to_string(image.width()) + "x" +
                 std::to_string(image.height()));
        sps::SplitResult manual;
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty() || line == "q" || line == "quit") break;
            std::istringstream ss(line);
            int x, y, w, h;
            if (!(ss >> x >> y >> w >> h) || w <= 0 || h <= 0) {
                out.note("  (skip) expected: x y width height");
                continue;
            }
            if (x < 0 || y < 0 || x + w > image.width() || y + h > image.height()) {
                out.note("  (skip) rect out of bounds (" + std::to_string(image.width()) +
                         "x" + std::to_string(image.height()) + ")");
                continue;
            }
            manual.sprites.push_back({x, y, w, h});
            out.note("  added rect=(" + std::to_string(x) + "," + std::to_string(y) + " " +
                     std::to_string(w) + "x" + std::to_string(h) + ") total " +
                     std::to_string(manual.sprites.size()));
        }
        if (manual.sprites.empty()) {
            if (out.json) {
                out.res["count"] = 0;
                out.res["output_dir"] = o.common.output_dir;
            } else {
                std::cout << "no rects entered, nothing to do.\n";
            }
            out.finish();
            return 0;
        }

        const int count = export_rects(image, manual, o.common.output_dir, out);
        // manual 总是写 meta.json（这是 manual 的产物）
        const std::string meta_path =
            (std::filesystem::path(o.common.output_dir) / "meta.json").string();
        std::ofstream f(meta_path);
        if (!f) throw std::runtime_error("cannot open '" + meta_path + "'");
        f << sps::export_json(image, manual, args.input) << "\n";
        out.note("wrote " + meta_path);
        if (out.json) {
            out.res["count"] = count;
            out.res["output_dir"] = o.common.output_dir;
            out.res["meta_path"] = meta_path;
        } else {
            std::cout << "done: " << count << " sprite(s) -> " << o.common.output_dir
                      << "\n";
        }
        out.finish();
        return 0;
    } catch (const std::exception& e) {
        print_runtime_error(out, e);
        return 2;
    }
}

int run_from_json(const CmdArgs& args, const CliOpts& o, Out& out) {
    try {
        sps::Image image = load_input(args.input, out);
        std::ifstream f(args.meta_path);
        if (!f) throw std::runtime_error("cannot open '" + args.meta_path + "'");
        const std::string json_text((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
        sps::SplitResult loaded;
        if (!sps::load_json(json_text, image.width(), image.height(), loaded))
            throw std::runtime_error("invalid meta.json '" + args.meta_path +
                                     "' (missing/invalid sprites array)");
        out.note("loaded " + std::to_string(loaded.sprites.size()) + " rect(s) from " +
                 args.meta_path);

        // 解析相对 mask 路径：相对 meta.json 所在目录
        const std::string meta_dir = std::filesystem::path(args.meta_path).parent_path().string();
        for (auto& mp : loaded.mask_paths) {
            if (!mp.empty() && std::filesystem::path(mp).is_relative()) {
                mp = (std::filesystem::path(meta_dir) / mp).string();
            }
        }

        const int count = export_rects(image, loaded, o.common.output_dir, out);
        if (out.json) {
            out.res["count"] = count;
            out.res["output_dir"] = o.common.output_dir;
        } else {
            std::cout << "done: " << count << " sprite(s) -> " << o.common.output_dir
                      << "\n";
        }
        out.finish();
        return 0;
    } catch (const std::exception& e) {
        print_runtime_error(out, e);
        return 2;
    }
}

int run_sheet(const CmdArgs& args, const CliOpts& o, Out& out) {
    try {
        sps::Image image = load_input(args.input, out);

        // 矩形来源：--from-json（应用 mask）或自动检测（透明图 alpha 切分）
        sps::SplitResult rects;
        if (!o.from_json.empty()) {
            std::ifstream f(o.from_json);
            if (!f) throw std::runtime_error("cannot open '" + o.from_json + "'");
            const std::string json_text((std::istreambuf_iterator<char>(f)),
                                        std::istreambuf_iterator<char>());
            if (!sps::load_json(json_text, image.width(), image.height(), rects))
                throw std::runtime_error("invalid meta.json '" + o.from_json +
                                         "' (missing/invalid sprites array)");
            out.note("loaded " + std::to_string(rects.sprites.size()) + " rect(s) from " +
                     o.from_json);
            const std::string meta_dir =
                std::filesystem::path(o.from_json).parent_path().string();
            for (auto& mp : rects.mask_paths) {
                if (!mp.empty() && std::filesystem::path(mp).is_relative()) {
                    mp = (std::filesystem::path(meta_dir) / mp).string();
                }
            }
        } else {
            rects = sps::split_image(image, o.opts);
            if (rects.sprites.empty())
                Out::warn("no sprites detected. Input must have transparency; if not, "
                          "run 'sprite-split remove-background' first.");
        }

        // 加载 mask（应用擦除）
        std::vector<std::vector<uint8_t>> masks(rects.sprites.size());
        for (std::size_t i = 0; i < rects.sprites.size(); ++i) {
            if (i < rects.mask_paths.size() && !rects.mask_paths[i].empty()) {
                try {
                    masks[i] = sps::load_mask_alpha(rects.mask_paths[i],
                                                    rects.sprites[i].width,
                                                    rects.sprites[i].height);
                } catch (const std::exception& e) {
                    Out::warn(std::string(e.what()) + " (sprite " +
                              std::to_string(i + 1) + " without mask)");
                }
            }
        }
        const auto sprites = sps::crop_sprites(image, rects.sprites, masks);
        std::vector<sps::SpriteRect> new_rects;
        sps::Image sheet = sps::repack_sheet(sprites, o.sheet_cols, 4, new_rects);

        std::filesystem::create_directories(o.common.output_dir);
        const std::string sheet_path =
            (std::filesystem::path(o.common.output_dir) / "sheet.png").string();
        sps::save_png(sheet, sheet_path);

        // 写 sheet_meta.json：精灵在新 sheet 中的坐标 + 原始 rect
        nlohmann::json j;
        j["sheet"] = sheet_path;
        j["width"] = sheet.width();
        j["height"] = sheet.height();
        j["sprites"] = nlohmann::json::array();
        for (std::size_t i = 0; i < rects.sprites.size(); ++i) {
            j["sprites"].push_back(
                {{"src", {{"x", rects.sprites[i].x},
                          {"y", rects.sprites[i].y},
                          {"width", rects.sprites[i].width},
                          {"height", rects.sprites[i].height}}},
                 {"dst", {{"x", new_rects[i].x},
                          {"y", new_rects[i].y},
                          {"width", new_rects[i].width},
                          {"height", new_rects[i].height}}}});
        }
        const std::string smeta_path =
            (std::filesystem::path(o.common.output_dir) / "sheet_meta.json").string();
        std::ofstream f(smeta_path);
        if (!f) throw std::runtime_error("cannot open '" + smeta_path + "'");
        f << j.dump(2) << "\n";
        out.note("wrote " + smeta_path);

        if (out.json) {
            out.res["sheet_path"] = sheet_path;
            out.res["sheet_meta_path"] = smeta_path;
            out.res["width"] = sheet.width();
            out.res["height"] = sheet.height();
            out.res["count"] = static_cast<int>(rects.sprites.size());
        } else {
            std::cout << "done: sheet " << sheet.width() << "x" << sheet.height() << " ("
                      << rects.sprites.size() << " sprites, " << o.sheet_cols
                      << " cols) -> " << sheet_path << "\n";
        }
        out.finish();
        return 0;
    } catch (const std::exception& e) {
        print_runtime_error(out, e);
        return 2;
    }
}

// ============================ CLI 组装（CLI11） ============================

struct CommandContext {
    CmdArgs args;
    CliOpts opts;
    Out out;
    int (*handler)(const CmdArgs&, const CliOpts&, Out&) = nullptr;
};

// 通用 flag：--output / --format json|text / -q
void add_common_flags(CLI::App* cmd, CommandContext& ctx) {
    auto& co = ctx.opts.common;
    cmd->add_option("--output", co.output_dir,
                    "output directory (default ./out/sprites)");
    cmd->add_option("--format", co.format_str, "machine-readable JSON result on stdout")
        ->check(CLI::IsMember({"json", "text"}));
    cmd->add_flag("-q,--quiet", co.quiet, "text mode: summary only");
}

// 子命令 callback 的公共收尾：解析 --format / --output 到运行时状态
void finalize_common(CLI::App* cmd, CommandContext& ctx) {
    auto& co = ctx.opts.common;
    co.output_set = cmd->get_option("--output")->count() > 0;
    co.json_format = (co.format_str == "json");
    ctx.out.json = co.json_format;
    ctx.out.quiet = co.quiet;
    if (co.json_format) ctx.out.res = {{"status", "ok"}, {"command", cmd->get_name()}};
}

// split/sheet 共享的检测 flag
void add_split_flags(CLI::App* cmd, CliOpts& o) {
    cmd->add_option("--mode", o.mode_str, "detection mode: components|grid|auto")
        ->check(CLI::IsMember({"components", "grid", "auto"}));
    cmd->add_option("--alpha-threshold", o.opts.alpha_threshold,
                    "foreground if alpha > N (default 1)")
        ->check(CLI::NonNegativeNumber);
    cmd->add_option("--min-width", o.opts.min_width,
                    "drop components narrower than N (default 1)")
        ->check(CLI::PositiveNumber);
    cmd->add_option("--min-height", o.opts.min_height,
                    "drop components shorter than N (default 1)")
        ->check(CLI::PositiveNumber);
    cmd->add_option("--merge-distance", o.opts.merge_distance,
                    "merge components within N px (0 = off, components only)")
        ->check(CLI::NonNegativeNumber);
    cmd->add_option("--cell-size", o.opts.grid_cell_size,
                    "grid cell size for grid/auto (default 16)")
        ->check(CLI::PositiveNumber);
}

// 子命令 callback 收尾：--mode / --merge-distance → SplitOptions
void finalize_split_flags(CliOpts& o) {
    if (o.mode_str == "grid") o.opts.mode = sps::DetectionMode::Grid;
    else if (o.mode_str == "auto") o.opts.mode = sps::DetectionMode::Auto;
    else o.opts.mode = sps::DetectionMode::ConnectedComponents;
    o.opts.merge_nearby = o.opts.merge_distance > 0;
}

// 背景 flag（仅 remove-background）
void add_background_flags(CLI::App* cmd, CliOpts& o) {
    cmd->add_option("--background-threshold", o.bg.threshold,
                    "color distance threshold floor (default 12)")
        ->check(CLI::NonNegativeNumber);
    cmd->add_option("--edge-clean", o.bg.edge_passes,
                    "edge transition cleanup rings (default 3; 0 = off)")
        ->check(CLI::NonNegativeNumber);
    cmd->add_option("--bg-color", o.bg_color_str,
                    "manual background color R,G,B (overrides ring sampling)");
    cmd->add_option("--bg-backend", o.bg_backend_str, "color | remote (default color)")
        ->check(CLI::IsMember({"color", "remote"}));
    cmd->add_option("--bg-url", o.bg_url,
                    "remote background service base URL (default "
                    "http://127.0.0.1:8000; unreachable -> warning + fallback to color)");
}

// 子命令 callback 收尾：--bg-color / --bg-backend → BackgroundOptions
void finalize_bg_flags(CliOpts& o) {
    if (!o.bg_color_str.empty()) {
        o.bg.has_bg_color = true;
        o.bg.bg_color = parse_bg_color(o.bg_color_str);
    }
    o.bg_backend_remote = (o.bg_backend_str == "remote");
}

}  // namespace

int main(int argc, char* argv[]) {
    // 注册 extra 库（sps_bg_remote）提供的 Remote 背景后端到 core 注册表
    sps::bg_remote::register_backend();

    CLI::App app{"sprite-split: sprite sheet analyzer CLI"};
    app.set_version_flag("--version", kVersion);

    // --prompt：输出完整 agent prompt（构建时嵌入的 SKILL.md），供 AI 代理直接获取使用规范
    bool show_prompt = false;
    app.add_flag("--prompt", show_prompt,
                 "print the full agent prompt (SKILL.md) and exit");

    // ---- info ----
    CommandContext info_ctx;
    info_ctx.handler = run_info;
    auto* info = app.add_subcommand(
        "info", "analyze image, print stats + recommended two-step workflow (no splitting)");
    info->add_option("input", info_ctx.args.input, "input image (or '-' for stdin)")->required();
    add_common_flags(info, info_ctx);
    info->callback([&] { finalize_common(info, info_ctx); });

    // ---- remove-background ----
    CommandContext rb_ctx;
    rb_ctx.handler = run_remove_background;
    auto* rb = app.add_subcommand(
        "remove-background",
        "remove near-uniform background, export full transparent PNG (--stdout pipes to next command)");
    rb->add_option("input", rb_ctx.args.input, "input image (or '-' for stdin)")->required();
    add_common_flags(rb, rb_ctx);
    add_background_flags(rb, rb_ctx.opts);
    rb->add_flag("--stdout", rb_ctx.opts.stdout_mode,
                 "write transparent PNG bytes to stdout (incompatible with --format json)");
    rb->callback([&] {
        finalize_common(rb, rb_ctx);
        finalize_bg_flags(rb_ctx.opts);
    });

    // ---- split ----
    CommandContext split_ctx;
    split_ctx.handler = run_split;
    auto* split = app.add_subcommand(
        "split",
        "detect sprites in a TRANSPARENT image and export PNGs (+ optional meta.json)");
    split->add_option("input", split_ctx.args.input,
                      "transparent input image (or '-' for stdin)")
        ->required();
    add_common_flags(split, split_ctx);
    add_split_flags(split, split_ctx.opts);
    split->add_flag("--json", split_ctx.opts.write_json, "also write meta.json");
    split->add_flag("--json-only", split_ctx.opts.json_only,
                    "export meta.json only, no PNGs (stdout if no --output)");
    split->add_flag("--gen-masks", split_ctx.opts.gen_masks,
                    "write eraser masks + meta.json, then split with them");
    std::string erase_tl;
    split->add_option("--erase-tl", erase_tl,
                      "with --gen-masks: erase WxH from top-left of each mask (e.g. 30x30)");
    split->callback([&] {
        finalize_common(split, split_ctx);
        finalize_split_flags(split_ctx.opts);
    });

    // ---- manual ----
    CommandContext manual_ctx;
    manual_ctx.handler = run_manual;
    auto* manual = app.add_subcommand(
        "manual",
        "interactively draw sprite rects ('x y width height' per line), export + meta.json");
    manual->add_option("input", manual_ctx.args.input, "transparent input image")->required();
    add_common_flags(manual, manual_ctx);
    manual->callback([&] { finalize_common(manual, manual_ctx); });

    // ---- from-json ----
    CommandContext fj_ctx;
    fj_ctx.handler = run_from_json;
    auto* fj = app.add_subcommand("from-json",
                                  "load rects from meta.json, cut input and export PNGs");
    fj->add_option("input", fj_ctx.args.input,
                   "transparent input image (or '-' for stdin)")
        ->required();
    fj->add_option("meta.json", fj_ctx.args.meta_path, "meta.json with sprite rects")
        ->required();
    add_common_flags(fj, fj_ctx);
    fj->callback([&] { finalize_common(fj, fj_ctx); });

    // ---- sheet ----
    CommandContext sheet_ctx;
    sheet_ctx.handler = run_sheet;
    auto* sheet = app.add_subcommand(
        "sheet",
        "repack sprites into a COLS-column grid sprite sheet (sheet.png + sheet_meta.json)");
    sheet->add_option("input", sheet_ctx.args.input,
                      "transparent input image (or '-' for stdin)")
        ->required();
    add_common_flags(sheet, sheet_ctx);
    sheet->add_option("--cols", sheet_ctx.opts.sheet_cols, "columns per row (required)")
        ->required()
        ->check(CLI::PositiveNumber);
    sheet->add_option("--from-json", sheet_ctx.opts.from_json,
                      "load rects from meta.json (masks applied); default: auto-detect");
    add_split_flags(sheet, sheet_ctx.opts);
    sheet->callback([&] {
        finalize_common(sheet, sheet_ctx);
        finalize_split_flags(sheet_ctx.opts);
    });

    // ---- 解析与分派 ----
    try {
        app.parse(argc, argv);
    } catch (const CLI::CallForHelp&) {
        std::cout << app.help();
        return 0;
    } catch (const CLI::CallForVersion&) {
        std::cout << "sprite-split " << kVersion << "\n";
        return 0;
    } catch (const CLI::ParseError& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        // callback 内的业务校验（如 --bg-color 格式）在此统一捕获
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    // --prompt：输出完整 agent prompt（构建时嵌入的 SKILL.md），不需要子命令
    if (show_prompt) {
        std::cout << sps::kSkillPrompt;
        const std::size_t len = std::char_traits<char>::length(sps::kSkillPrompt);
        if (len > 0 && sps::kSkillPrompt[len - 1] != '\n') std::cout << '\n';
        return 0;
    }

    // 无子命令 → 主帮助 + 退出码 1（与 require_subcommand 语义一致）
    if (!info->parsed() && !rb->parsed() && !split->parsed() && !manual->parsed() &&
        !fj->parsed() && !sheet->parsed()) {
        std::cout << app.help();
        return 1;
    }

    // 每命令一致性校验 + 执行
    CommandContext* ctx = nullptr;
    if (info->parsed()) {
        ctx = &info_ctx;
    } else if (rb->parsed()) {
        ctx = &rb_ctx;
        if (rb_ctx.opts.stdout_mode && rb_ctx.opts.common.json_format) {
            std::cerr << "error: --stdout is incompatible with --format json\n";
            return 1;
        }
        if (rb_ctx.opts.stdout_mode) {
            rb_ctx.out.force_stderr = true;  // stdout 留给 PNG 二进制
            rb_ctx.opts.common.json_format = false;
            rb_ctx.out.json = false;
        }
    } else if (split->parsed()) {
        ctx = &split_ctx;
        if (!erase_tl.empty()) {
            int w = 0, h = 0;
            if (std::sscanf(erase_tl.c_str(), "%dx%d", &w, &h) != 2 || w <= 0 || h <= 0) {
                std::cerr << "error: invalid --erase-tl '" << erase_tl
                          << "' (expected WxH, e.g. 30x30)\n";
                return 1;
            }
            split_ctx.opts.erase_tl_w = w;
            split_ctx.opts.erase_tl_h = h;
        }
        try {
            validate_split_opts(split_ctx.opts);
        } catch (const ArgError& e) {
            std::cerr << "error: " << e.what() << "\n";
            return 1;
        }
    } else if (manual->parsed()) {
        ctx = &manual_ctx;
    } else if (fj->parsed()) {
        ctx = &fj_ctx;
    } else if (sheet->parsed()) {
        ctx = &sheet_ctx;
        try {
            validate_split_opts(sheet_ctx.opts);
        } catch (const ArgError& e) {
            std::cerr << "error: " << e.what() << "\n";
            return 1;
        }
    }

    if (ctx == nullptr || ctx->handler == nullptr) {
        std::cerr << "error: unknown command\n";
        return 1;
    }
    return ctx->handler(ctx->args, ctx->opts, ctx->out);
}
