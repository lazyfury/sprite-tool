// sprite-split CLI：v0.7 子命令版
//
// 用法：
//   sprite-split <command> [args]
//
// 子命令：
//   info <input>                     分析图片，输出统计与推荐参数（不切分）
//   split <input> [flags]            自动检测 + 切分导出（components/grid/auto）
//   manual <input> [flags]           交互式画框 + 切分导出（始终写 meta.json）
//   from-json <input> <meta.json>    从 meta.json 加载 rects 直接切图
//   sheet <input> --cols N [flags]   重排为规整 sprite sheet（支持 --from-json）
//   remove-background <input> [flags] 去背景，整图导出透明 PNG（不切分）
//
// 通用 flag：
//   --output DIR          输出目录（默认 ./sprites）
//   --format json|text    结构化 JSON 结果输出到 stdout（默认 text；json 模式
//                         下 stdout 只含结果对象，进度/日志走 stderr，便于管道）
//   -q, --quiet           文本模式：只输出最终摘要
//   --version             版本号
//   --help                帮助（子命令后跟 --help 查看该命令专属帮助）
//
// 退出码：0 正常 / 1 参数错误 / 2 运行错误

#include "analyzer.hpp"
#include "bg_remote.hpp"  // extra：sps_bg_remote（Remote 后端注册）
#include "export/json_exporter.hpp"
#include "export/png_exporter.hpp"
#include "export/sheet.hpp"
#include "image/image.hpp"
#include "mask/mask_io.hpp"
#include "segmentation/background.hpp"
#include "segmentation/background_remover.hpp"
#include "segmentation/splitter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

constexpr const char* kVersion = "0.8.0";

// ============================ 帮助文本 ============================

void print_main_help(const char* prog) {
    std::cout
        << "Usage: " << prog << " <command> [args]\n"
        << "\n"
        << "Commands:\n"
        << "  info <input>                        analyze image, print stats + recommended options\n"
        << "  split <input> [flags]               auto-detect and split (components/grid/auto)\n"
        << "  manual <input> [flags]              interactively draw rects, export + meta.json\n"
        << "  from-json <input> <meta.json>       load rects from meta.json, cut and export\n"
        << "  sheet <input> --cols N [flags]      repack sprites into a grid sprite sheet\n"
        << "  remove-background <input> [flags]   remove bg, export full transparent PNG\n"
        << "\n"
        << "Common flags:\n"
        << "  --output DIR          output directory (default ./sprites)\n"
        << "  --format json|text    machine-readable JSON result on stdout (default text;\n"
        << "                        in json mode progress goes to stderr, stdout stays pure)\n"
        << "  -q, --quiet           text mode: summary only, no per-sprite lines\n"
        << "  --version             show version\n"
        << "  --help                show this help\n"
        << "\n"
        << "Run '" << prog << " <command> --help' for command-specific flags.\n"
        << "Examples:\n"
        << "  " << prog << " info char.png\n"
        << "  " << prog << " split char.png --remove-background --output sprites --json\n"
        << "  " << prog << " from-json char.png sprites/meta.json --output sprites\n"
        << "  " << prog << " sheet char.png --cols 8 --from-json sprites/meta.json\n"
        << "  " << prog << " remove-background photo.png --output sprites\n";
}

const char* kInfoHelp =
    "Usage: sprite-split info <input> [flags]\n"
    "\n"
    "Analyze <input> and print stats + recommended options (no splitting).\n"
    "\n"
    "Flags:\n"
    "  --remove-background      use background removal for the fg/bg analysis\n"
    "  --background-threshold N color distance threshold (default 12)\n"
    "  --bg-color R,G,B         manual background color (overrides corner sampling)\n"
    "  --format json|text       machine-readable JSON result on stdout (default text)\n"
    "  -q, --quiet              text mode: suppress non-essential lines\n"
    "\n"
    "Example:\n"
    "  sprite-split info char.png --format json | jq '.components'\n";

const char* kSplitHelp =
    "Usage: sprite-split split <input> [flags]\n"
    "\n"
    "Detect sprites in <input> and export them as PNGs (optionally meta.json).\n"
    "\n"
    "Detection:\n"
    "  --mode MODE            components | grid | auto (default components)\n"
    "  --alpha-threshold N    foreground if alpha > N (default 1)\n"
    "  --min-width N          drop components narrower than N (default 1)\n"
    "  --min-height N         drop components shorter than N (default 1)\n"
    "  --merge-distance N     merge components within N px (0 = off, components only)\n"
    "  --cell-size N          grid cell size for grid/auto (default 16)\n"
    "\n"
    "Background cleanup (fully-opaque input needs this):\n"
    "  --remove-background    remove near-uniform background, export transparent\n"
    "  --background-threshold N  color distance threshold floor (default 12;\n"
    "                        auto-grows with background noise)\n"
    "  --edge-clean N        edge transition cleanup rings, 1 ring ~ 1px (default 3;\n"
    "                        0 = off)\n"
    "  --bg-color R,G,B       manual background color (overrides ring sampling)\n"
    "  --contract N           erode foreground outline by N px, re-crop to the\n"
    "                        shrunk outline (trims halo fringe; remove-background only)\n"
    "\n"
    "Background backend (remove-background only):\n"
    "  --bg-backend MODE      color | remote (default color; remote = call the URL below)\n"
    "  --bg-url URL           remote background service base URL\n"
    "                        (default http://127.0.0.1:8000, e.g. examples/rembg-api)\n"
    "                        remote unreachable -> warning + auto fallback to color\n"
    "\n"
    "Export:\n"
    "  --output DIR           output directory (default ./sprites)\n"
    "  --json                 also write meta.json\n"
    "  --json-only            export meta.json only, no PNGs (stdout if no --output)\n"
    "  --gen-masks            write eraser masks + meta.json, then split with them\n"
    "  --erase-tl WxH         with --gen-masks: erase WxH from top-left of each mask\n"
    "  --format json|text     machine-readable JSON result on stdout (default text)\n"
    "  -q, --quiet            text mode: summary only\n"
    "\n"
    "Examples:\n"
    "  sprite-split split char.png --remove-background --output sprites --json\n"
    "  sprite-split split sheet.png --mode grid --cell-size 8 --output sprites\n"
    "  sprite-split split char.png --merge-distance 3 --output sprites\n";

const char* kManualHelp =
    "Usage: sprite-split manual <input> [flags]\n"
    "\n"
    "Interactively draw sprite rects ('x y width height' per line, empty line or 'q'\n"
    "to finish), then export PNGs and always write meta.json.\n"
    "\n"
    "Flags:\n"
    "  --output DIR           output directory (default ./sprites)\n"
    "  --remove-background    remove near-uniform background, export transparent\n"
    "  --background-threshold N  color distance threshold floor (default 12;\n"
    "                        auto-grows with background noise)\n"
    "  --edge-clean N        edge transition cleanup rings, 1 ring ~ 1px (default 3;\n"
    "                        0 = off)\n"
    "  --bg-color R,G,B       manual background color (overrides ring sampling)\n"
    "  --format json|text     machine-readable JSON result on stdout (default text)\n"
    "  -q, --quiet            text mode: summary only\n"
    "\n"
    "Example:\n"
    "  echo '10 10 32 32' | sprite-split manual char.png --output sprites\n";

const char* kFromJsonHelp =
    "Usage: sprite-split from-json <input> <meta.json> [flags]\n"
    "\n"
    "Load sprite rects from meta.json, cut <input> and export PNGs.\n"
    "Mask paths in meta.json are resolved relative to the meta.json directory.\n"
    "\n"
    "Flags:\n"
    "  --output DIR           output directory (default ./sprites)\n"
    "  --remove-background    remove near-uniform background, export transparent\n"
    "  --background-threshold N  color distance threshold floor (default 12;\n"
    "                        auto-grows with background noise)\n"
    "  --edge-clean N        edge transition cleanup rings, 1 ring ~ 1px (default 3;\n"
    "                        0 = off)\n"
    "  --bg-color R,G,B       manual background color (overrides ring sampling)\n"
    "  --bg-backend MODE      color | remote (default color; remote = call the URL below)\n"
    "  --bg-url URL           remote background service base URL (default\n"
    "                        http://127.0.0.1:8000; see examples/rembg-api)\n"
    "  --format json|text     machine-readable JSON result on stdout (default text)\n"
    "  -q, --quiet            text mode: summary only\n"
    "\n"
    "Example:\n"
    "  sprite-split from-json char.png sprites/meta.json --output sprites\n";

const char* kRemoveBgHelp =
    "Usage: sprite-split remove-background <input> [flags]\n"
    "\n"
    "Remove near-uniform background from <input> and export the FULL image as a\n"
    "transparent PNG. Unlike 'split', no sprite detection / cropping happens --\n"
    "the output keeps the original dimensions (one transparent image, not a\n"
    "sprite sheet).\n"
    "Output: <output>/<stem>_transparent.png\n"
    "\n"
    "Flags:\n"
    "  --output DIR           output directory (default ./sprites)\n"
    "  --background-threshold N  color distance threshold floor (default 12;\n"
    "                        auto-grows with background noise)\n"
    "  --edge-clean N        edge transition cleanup rings, 1 ring ~ 1px (default 3;\n"
    "                        0 = off)\n"
    "  --bg-color R,G,B       manual background color (overrides ring sampling)\n"
    "\n"
    "Background backend:\n"
    "  --bg-backend MODE      color | remote (default color; remote = call the URL below)\n"
    "  --bg-url URL           remote background service base URL\n"
    "                        (default http://127.0.0.1:8000, e.g. examples/rembg-api)\n"
    "                        remote unreachable -> warning + auto fallback to color\n"
    "\n"
    "Other:\n"
    "  --format json|text     machine-readable JSON result on stdout (default text)\n"
    "  -q, --quiet            text mode: summary only\n"
    "\n"
    "Example:\n"
    "  sprite-split remove-background photo.png --output sprites --format json | jq '.output'\n";

const char* kSheetHelp =
    "Usage: sprite-split sheet <input> --cols N [flags]\n"
    "\n"
    "Repack sprites into a COLS-column grid sprite sheet (sheet.png + sheet_meta.json).\n"
    "\n"
    "Rects source:\n"
    "  --from-json FILE      load rects from meta.json (masks applied)\n"
    "  (default)             auto-detect with the detection flags below\n"
    "\n"
    "Detection flags (when not using --from-json; same as 'split'):\n"
    "  --mode MODE            components | grid | auto (default components)\n"
    "  --alpha-threshold N    foreground if alpha > N (default 1)\n"
    "  --min-width N          drop components narrower than N (default 1)\n"
    "  --min-height N         drop components shorter than N (default 1)\n"
    "  --merge-distance N     merge components within N px (0 = off, components only)\n"
    "  --cell-size N          grid cell size for grid/auto (default 16)\n"
    "  --remove-background    remove near-uniform background before detection\n"
    "  --background-threshold N  color distance threshold floor (default 12;\n"
    "                        auto-grows with background noise)\n"
    "  --edge-clean N        edge transition cleanup rings, 1 ring ~ 1px (default 3;\n"
    "                        0 = off)\n"
    "  --bg-color R,G,B       manual background color (overrides ring sampling)\n"
    "  --contract N           erode foreground outline by N px, re-crop to the\n"
    "                        shrunk outline (trims halo fringe; remove-background only)\n"
    "\n"
    "Background backend (remove-background only):\n"
    "  --bg-backend MODE      color | remote (default color; remote = call the URL below)\n"
    "  --bg-url URL           remote background service base URL\n"
    "                        (default http://127.0.0.1:8000, e.g. examples/rembg-api)\n"
    "                        remote unreachable -> warning + auto fallback to color\n"
    "\n"
    "Other:\n"
    "  --output DIR           output directory (default ./sprites)\n"
    "  --format json|text     machine-readable JSON result on stdout (default text)\n"
    "  -q, --quiet            text mode: summary only\n"
    "\n"
    "Examples:\n"
    "  sprite-split sheet char.png --cols 8 --from-json sprites/meta.json\n"
    "  sprite-split sheet sheet.png --cols 8 --mode grid --cell-size 8\n";

// ============================ 输出抽象 ============================
// --format json：stdout 只输出一个结果对象（res），人类可读信息走 stderr；
// 文本模式：行为与旧版一致（进度/摘要在 stdout）。
struct Out {
    bool json = false;    // --format json
    bool quiet = false;   // -q/--quiet
    nlohmann::json res;   // json 模式的结果对象（stdout 唯一输出）

    // 进度/人类可读信息：text→stdout，json→stderr（保持 stdout 纯净，管道友好）
    void note(const std::string& s) const {
        if (json)
            std::cerr << s << "\n";
        else
            std::cout << s << "\n";
    }
    // 警告：一律 stderr
    static void warn(const std::string& s) { std::cerr << "warning: " << s << "\n"; }
    // 结束：json 模式输出结果对象
    void finish() const {
        if (json) std::cout << res.dump() << "\n";
    }
};

// ============================ 参数解析 ============================

struct CliOpts {
    std::string output_dir = "sprites";
    bool output_set = false;
    bool quiet = false;
    bool json_format = false;
    sps::SplitOptions opts;  // 检测/背景清理选项（split/sheet/info 共用）
    bool write_json = false;
    bool json_only = false;
    bool gen_masks = false;
    int erase_tl_w = 0, erase_tl_h = 0;   // --erase-tl WxH
    int sheet_cols = 0;                   // sheet --cols N
    bool sheet_cols_set = false;
    std::string from_json;                // sheet --from-json FILE
    std::string bg_url = "http://127.0.0.1:8000";  // remote 背景服务 base URL
    bool bg_backend_remote = false;       // --bg-backend remote
};

struct ArgError {
    std::string msg;
};

int parse_int(const std::string& v, const char* name) {
    try {
        std::size_t pos = 0;
        int out = std::stoi(v, &pos);
        if (pos != v.size()) throw std::invalid_argument("trailing chars");
        return out;
    } catch (...) {
        throw ArgError{std::string("invalid value for ") + name + ": '" + v + "'"};
    }
}

void apply_output(CliOpts& o, const std::string& v) {
    o.output_dir = v;
    o.output_set = true;
}
void apply_alpha_threshold(CliOpts& o, const std::string& v) {
    o.opts.alpha_threshold = parse_int(v, "--alpha-threshold");
}
void apply_min_width(CliOpts& o, const std::string& v) {
    o.opts.min_width = parse_int(v, "--min-width");
}
void apply_min_height(CliOpts& o, const std::string& v) {
    o.opts.min_height = parse_int(v, "--min-height");
}
void apply_remove_background(CliOpts& o, const std::string&) {
    o.opts.remove_background = true;
}
void apply_background_threshold(CliOpts& o, const std::string& v) {
    o.opts.background_threshold = parse_int(v, "--background-threshold");
}
void apply_edge_clean(CliOpts& o, const std::string& v) {
    o.opts.edge_passes = parse_int(v, "--edge-clean");
}
void apply_bg_color(CliOpts& o, const std::string& v) {
    int r = 0, g = 0, b = 0;
    if (std::sscanf(v.c_str(), "%d,%d,%d", &r, &g, &b) != 3 || r < 0 || r > 255 ||
        g < 0 || g > 255 || b < 0 || b > 255) {
        throw ArgError{"invalid --bg-color '" + v + "' (expected R,G,B in 0..255)"};
    }
    o.opts.has_bg_color = true;
    o.opts.bg_color = sps::Pixel{static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                                 static_cast<uint8_t>(b), 255};
}
void apply_contract(CliOpts& o, const std::string& v) {
    o.opts.contract = parse_int(v, "--contract");
}
void apply_mode(CliOpts& o, const std::string& v) {
    if (v == "components")
        o.opts.mode = sps::DetectionMode::ConnectedComponents;
    else if (v == "grid")
        o.opts.mode = sps::DetectionMode::Grid;
    else if (v == "auto")
        o.opts.mode = sps::DetectionMode::Auto;
    else
        throw ArgError{"unknown mode '" + v + "' (expected components|grid|auto)"};
}
void apply_cell_size(CliOpts& o, const std::string& v) {
    o.opts.grid_cell_size = parse_int(v, "--cell-size");
}
void apply_merge_distance(CliOpts& o, const std::string& v) {
    o.opts.merge_distance = parse_int(v, "--merge-distance");
    o.opts.merge_nearby = true;
}
void apply_json(CliOpts& o, const std::string&) { o.write_json = true; }
void apply_json_only(CliOpts& o, const std::string&) { o.json_only = true; }
void apply_gen_masks(CliOpts& o, const std::string&) { o.gen_masks = true; }
void apply_erase_tl(CliOpts& o, const std::string& v) {
    int w = 0, h = 0;
    if (std::sscanf(v.c_str(), "%dx%d", &w, &h) != 2 || w <= 0 || h <= 0)
        throw ArgError{"invalid --erase-tl '" + v + "' (expected WxH, e.g. 30x30)"};
    o.erase_tl_w = w;
    o.erase_tl_h = h;
}
void apply_cols(CliOpts& o, const std::string& v) {
    o.sheet_cols = parse_int(v, "--cols");
    o.sheet_cols_set = true;
}
void apply_from_json(CliOpts& o, const std::string& v) { o.from_json = v; }
void apply_bg_backend(CliOpts& o, const std::string& v) {
    if (v == "remote")
        o.bg_backend_remote = true;
    else if (v == "color")
        o.bg_backend_remote = false;
    else
        throw ArgError{"unknown --bg-backend '" + v + "' (expected color|remote)"};
}
void apply_bg_url(CliOpts& o, const std::string& v) { o.bg_url = v; }
void apply_format(CliOpts& o, const std::string& v) {
    if (v == "json")
        o.json_format = true;
    else if (v == "text")
        o.json_format = false;
    else
        throw ArgError{"invalid --format '" + v + "' (expected json|text)"};
}
void apply_quiet(CliOpts& o, const std::string&) { o.quiet = true; }

struct FlagSpec {
    const char* name;    // 触发名称
    bool takes_value;    // 是否消费下一个参数
    void (*apply)(CliOpts&, const std::string&);  // 无值 flag 时 value 为空串
};

const std::map<std::string, FlagSpec>& flag_table() {
    static const std::map<std::string, FlagSpec> t = {
        {"--output", {"--output", true, apply_output}},
        {"--alpha-threshold", {"--alpha-threshold", true, apply_alpha_threshold}},
        {"--min-width", {"--min-width", true, apply_min_width}},
        {"--min-height", {"--min-height", true, apply_min_height}},
        {"--remove-background", {"--remove-background", false, apply_remove_background}},
        {"--background-threshold",
         {"--background-threshold", true, apply_background_threshold}},
        {"--edge-clean", {"--edge-clean", true, apply_edge_clean}},
        {"--bg-color", {"--bg-color", true, apply_bg_color}},
        {"--contract", {"--contract", true, apply_contract}},
        {"--mode", {"--mode", true, apply_mode}},
        {"--cell-size", {"--cell-size", true, apply_cell_size}},
        {"--merge-distance", {"--merge-distance", true, apply_merge_distance}},
        {"--json", {"--json", false, apply_json}},
        {"--json-only", {"--json-only", false, apply_json_only}},
        {"--gen-masks", {"--gen-masks", false, apply_gen_masks}},
        {"--erase-tl", {"--erase-tl", true, apply_erase_tl}},
        {"--cols", {"--cols", true, apply_cols}},
        {"--from-json", {"--from-json", true, apply_from_json}},
        {"--bg-backend", {"--bg-backend", true, apply_bg_backend}},
        {"--bg-url", {"--bg-url", true, apply_bg_url}},
        {"--format", {"--format", true, apply_format}},
        {"-q", {"-q", false, apply_quiet}},
        {"--quiet", {"--quiet", false, apply_quiet}},
    };
    return t;
}

enum class ParseResult { Ok, Error, Help };

// 通用参数解析：positional 依 slots 顺序填充到 pos_out；flags 按 allowed 白名单接受
ParseResult parse_args(CliOpts& o, const std::set<std::string>& allowed,
                       const std::vector<std::string>& slots,
                       const std::string& help_text, int argc, char** argv, int start,
                       std::vector<std::string>& pos_out) {
    pos_out.assign(slots.size(), std::string());
    int npos = 0;
    for (int i = start; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--help" || a == "-h") {
            std::cout << help_text;
            return ParseResult::Help;
        }
        if (!a.empty() && a[0] == '-') {
            const auto& table = flag_table();
            auto it = table.find(a);
            if (it == table.end()) {
                std::cerr << "error: unknown option '" << a << "'\n";
                return ParseResult::Error;
            }
            if (!allowed.count(a)) {
                std::cerr << "error: option '" << a
                          << "' is not valid for this command\n";
                return ParseResult::Error;
            }
            const FlagSpec& spec = it->second;
            std::string value;
            if (spec.takes_value) {
                if (i + 1 >= argc) {
                    std::cerr << "error: missing value for " << spec.name << "\n";
                    return ParseResult::Error;
                }
                value = argv[++i];
            }
            try {
                spec.apply(o, value);
            } catch (const ArgError& e) {
                std::cerr << "error: " << e.msg << "\n";
                return ParseResult::Error;
            }
        } else {
            if (npos >= static_cast<int>(slots.size())) {
                std::cerr << "error: unexpected argument '" << a << "'\n";
                return ParseResult::Error;
            }
            pos_out[npos++] = a;
        }
    }
    for (std::size_t k = 0; k < slots.size(); ++k) {
        if (pos_out[k].empty()) {
            std::cerr << "error: missing <" << slots[k] << ">\n";
            return ParseResult::Error;
        }
    }
    return ParseResult::Ok;
}

// ============================ 通用逻辑 ============================

// 检测/导出选项合法性校验（split/sheet 共用）；非法时抛 ArgError
void validate_split_opts(const CliOpts& o) {
    if (o.opts.min_width < 1) throw ArgError{"--min-width must be >= 1"};
    if (o.opts.min_height < 1) throw ArgError{"--min-height must be >= 1"};
    if (o.opts.mode == sps::DetectionMode::Grid && o.opts.grid_cell_size < 1)
        throw ArgError{"--cell-size must be >= 1 in grid mode"};
    if (o.opts.merge_nearby && o.opts.merge_distance < 0)
        throw ArgError{"--merge-distance must be >= 0"};
    if (o.opts.merge_nearby && o.opts.mode != sps::DetectionMode::ConnectedComponents)
        throw ArgError{"--merge-distance only applies to components mode"};
    if (o.opts.contract < 0) throw ArgError{"--contract must be >= 0"};
    if (o.opts.contract > 0 && !o.opts.remove_background)
        throw ArgError{"--contract requires --remove-background"};
    if (o.opts.contract > 0 && o.opts.mode == sps::DetectionMode::Grid)
        throw ArgError{"--contract requires components-based mode"};
    if (o.opts.edge_passes < 0) throw ArgError{"--edge-clean must be >= 0"};
    if (o.opts.remove_background && o.opts.background_threshold < 0)
        throw ArgError{"--background-threshold must be >= 0"};
    if (o.opts.has_bg_color && !o.opts.remove_background)
        throw ArgError{"--bg-color requires --remove-background"};
    if (o.bg_backend_remote && !o.opts.remove_background)
        throw ArgError{"--bg-backend remote requires --remove-background"};
}

// 背景清理透明化（split/manual/from-json/sheet 共用）。
// 所有后端统一走 BackgroundRemover 接口：process 输出背景 mask（true=背景），
// 再统一 make_background_transparent —— remote 与纯算法共享同一下游管线
// （含 --contract，由 split_image 基于清理后的 mask 应用）。
//
// used_bg（可选输出）：实际使用的背景 mask（true=背景）。remove_background 开启时
// 无论走 remote 还是回退 color 都填充，供调用方传给 split_image 复用，保证
// remote 的 AI 分割结果真正参与切分（而非仅导出透明化）。keep_alpha 模式下无二值 mask，
// used_bg 保持空。
// used_remote（可选输出）：remote 请求成功时置 true，回退/纯算法时为 false。
// keep_alpha（可选）：remove-background 整图导出专用 —— remote 成功时直接采用服务端
// 透明图（保留 AI 软边 alpha，与 API 直连质量一致），不经过二值 mask 回放；
// 失败/尺寸不一致仍回退 color。split/manual/from-json/sheet 不传，行为不变。
void apply_background_cleanup(sps::Image& image, const CliOpts& o,
                              sps::Mask* used_bg = nullptr,
                              bool* used_remote = nullptr,
                              bool keep_alpha = false) {
    if (!o.opts.remove_background) return;

    const auto kind = o.bg_backend_remote ? sps::BackgroundBackend::Remote
                                          : sps::BackgroundBackend::Color;
    sps::BackgroundRemoverOptions opts;
    opts.color.threshold = o.opts.background_threshold;
    opts.color.has_bg_color = o.opts.has_bg_color;
    opts.color.bg_color = o.opts.bg_color;
    opts.color.edge_passes = o.opts.edge_passes;
    opts.remote_url = o.bg_url;

    auto remover = sps::create_background_remover(kind, opts);
    if (!remover) {
        throw ArgError{"--bg-backend remote unavailable (sps_bg_remote not linked)"};
    }

    sps::Mask background;
    if (o.bg_backend_remote) {
        std::cerr << "note: remote background backend: " << o.bg_url << "\n";
        // remote 后端失败 → warning + 回退纯算法后端（零回归兕底）
        try {
            if (keep_alpha) {
                // 整图透明导出：直接采用服务端透明图（保留 AI 软边 alpha）
                sps::Image transparent = remover->process_transparent(image);
                if (transparent.width() != image.width() ||
                    transparent.height() != image.height()) {
                    throw std::runtime_error(
                        "remote bg returned different size (" +
                        std::to_string(transparent.width()) + "x" +
                        std::to_string(transparent.height()) + ")");
                }
                image = std::move(transparent);
                if (used_remote) *used_remote = true;
                return;  // 无二值 mask，调用方以结果图 alpha 为准
            }
            background = remover->process(image);
            if (used_remote) *used_remote = true;
        } catch (const std::exception& e) {
            Out::warn(std::string("remote background backend failed (") + e.what() +
                      "); falling back to color backend");
            remover = sps::create_background_remover(sps::BackgroundBackend::Color, opts);
            background = remover->process(image);
            if (used_remote) *used_remote = false;
        }
    } else {
        background = remover->process(image);
        if (used_remote) *used_remote = false;
    }
    sps::make_background_transparent(image, background);
    if (used_bg != nullptr) *used_bg = std::move(background);
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

// 把 rects 裁剪导出为 PNG（可选透明化已由调用方应用到 image 上）。
// 文本模式打印明细（-q 抑制）；json 模式收集进 res["sprites"]。
// 返回导出的 sprite 数
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

// ============================ 子命令 ============================

int run_info(int argc, char** argv, int start) {
    CliOpts o;
    Out out;
    std::vector<std::string> pos;
    const std::set<std::string> allowed = {
        "--remove-background", "--background-threshold", "--bg-color",
        "--format", "-q", "--quiet", "--help", "-h"};
    auto pr = parse_args(o, allowed, {"input"}, kInfoHelp, argc, argv, start, pos);
    if (pr == ParseResult::Help) return 0;
    if (pr == ParseResult::Error) return 1;
    const std::string input = pos[0];
    out.json = o.json_format;
    out.quiet = o.quiet;
    if (out.json) out.res = {{"status", "ok"}, {"command", "info"}, {"input", input}};

    try {
        sps::Image image = sps::Image::load_png(input);
        const int bg_threshold =
            o.opts.remove_background ? o.opts.background_threshold : 12;
        sps::ImageStats s = sps::analyze_image(image, bg_threshold, o.opts.has_bg_color,
                                               o.opts.bg_color);

        const long total = s.total_pixels;
        const double opaque_pct = total ? 100.0 * s.opaque_pixels / total : 0.0;
        const double trans_pct = total ? 100.0 * s.transparent_pixels / total : 0.0;
        const double semi_pct = total ? 100.0 * s.semi_pixels / total : 0.0;

        // ---- 推荐参数（文本与 json 共用） ----
        std::vector<std::string> rec;
        if (!s.has_transparency) {
            rec.push_back("--remove-background");
            rec.push_back(s.bg_uniform
                              ? "--background-threshold 12 (bg is uniform)"
                              : "--background-threshold 12 (bg not uniform; tune)");
        }
        if (s.suggested_min_width > 1 || s.suggested_min_height > 1) {
            rec.push_back("--min-width " + std::to_string(s.suggested_min_width) +
                          " --min-height " + std::to_string(s.suggested_min_height) +
                          " (filter " + std::to_string(s.component_count) +
                          " components, many are small)");
        }
        if (rec.empty()) rec.push_back("--alpha-threshold 1 (transparency already present)");
        rec.push_back("--json (also export sprite metadata)");

        std::string example = "sprite-split split " + input;
        if (!s.has_transparency) example += " --remove-background";
        if (s.suggested_min_width > 1)
            example += " --min-width " + std::to_string(s.suggested_min_width) +
                       " --min-height " + std::to_string(s.suggested_min_height);
        example += " --output sprites --json";

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

int run_split(int argc, char** argv, int start) {
    CliOpts o;
    Out out;
    std::vector<std::string> pos;
    const std::set<std::string> allowed = {
        "--output", "--alpha-threshold", "--min-width", "--min-height",
        "--remove-background", "--background-threshold", "--edge-clean", "--bg-color", "--contract",
        "--bg-backend", "--bg-url",
        "--mode", "--cell-size", "--merge-distance", "--json", "--json-only",
        "--gen-masks", "--erase-tl", "--format", "-q", "--quiet", "--help", "-h"};
    auto pr = parse_args(o, allowed, {"input"}, kSplitHelp, argc, argv, start, pos);
    if (pr == ParseResult::Help) return 0;
    if (pr == ParseResult::Error) return 1;
    const std::string input = pos[0];
    try {
        validate_split_opts(o);
    } catch (const ArgError& e) {
        std::cerr << "error: " << e.msg << "\n";
        return 1;
    }
    out.json = o.json_format;
    out.quiet = o.quiet;
    if (out.json) out.res = {{"status", "ok"}, {"command", "split"}, {"input", input}};

    // --json-only 无 --output：meta 直出 stdout（text 模式 stdout 必须纯净）
    const bool meta_to_stdout = o.json_only && !o.output_set;
    const bool silent_stdout = meta_to_stdout && !out.json;

    try {
        sps::Image image = sps::Image::load_png(input);
        if (!silent_stdout)
            out.note("loaded " + input + " (" + std::to_string(image.width()) + "x" +
                     std::to_string(image.height()) + ")");
        sps::Mask bg_mask;  // remote/color 实际使用的背景 mask（供 split_image 复用）
        apply_background_cleanup(image, o, &bg_mask);

        sps::SplitResult result = sps::split_image(image, o.opts,
                                                   bg_mask.empty() ? nullptr : &bg_mask);

        if (result.sprites.empty()) {
            std::string w = "no sprites detected. ";
            w += o.opts.remove_background
                     ? "Try lowering --background-threshold, or adjust --mode / --cell-size."
                     : "Image may have no transparency; try --remove-background.";
            if (out.json)
                out.res["warning"] = w;
            else if (!silent_stdout)
                Out::warn(w);
        }

        if (o.json_only) {
            emit_meta(image, result, input, o.output_dir, meta_to_stdout, out);
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
            std::filesystem::create_directories(o.output_dir);
            const std::string mask_dir =
                (std::filesystem::path(o.output_dir) / "masks").string();
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
                (std::filesystem::path(o.output_dir) / "meta.json").string();
            std::ofstream f(meta_path);
            if (!f) throw std::runtime_error("cannot open '" + meta_path + "'");
            f << sps::export_json(image, result, input) << "\n";
            out.note("wrote " + meta_path + " (+ " + std::to_string(result.sprites.size()) +
                     " masks in " + mask_dir + ")");
            if (out.json) {
                out.res["meta_path"] = meta_path;
                out.res["masks_dir"] = mask_dir;
            }
            // 相对 mask 路径解析为绝对（相对 output_dir），供 export_rects 使用
            for (auto& mp : result.mask_paths) {
                if (!mp.empty() && std::filesystem::path(mp).is_relative()) {
                    mp = (std::filesystem::path(o.output_dir) / mp).string();
                }
            }
        } else if (o.write_json) {
            std::filesystem::create_directories(o.output_dir);
            const std::string meta_path =
                (std::filesystem::path(o.output_dir) / "meta.json").string();
            std::ofstream f(meta_path);
            if (!f) throw std::runtime_error("cannot open '" + meta_path + "'");
            f << sps::export_json(image, result, input) << "\n";
            out.note("wrote " + meta_path);
            if (out.json) out.res["meta_path"] = meta_path;
        }

        const int count = export_rects(image, result, o.output_dir, out);
        if (out.json) {
            out.res["count"] = count;
            out.res["output_dir"] = o.output_dir;
        } else {
            std::cout << "done: " << count << " sprite(s) -> " << o.output_dir << "\n";
        }
        out.finish();
        return 0;
    } catch (const std::exception& e) {
        print_runtime_error(out, e);
        return 2;
    }
}

int run_manual(int argc, char** argv, int start) {
    CliOpts o;
    Out out;
    std::vector<std::string> pos;
    const std::set<std::string> allowed = {
        "--output", "--remove-background", "--background-threshold", "--edge-clean", "--bg-color",
        "--format", "-q", "--quiet", "--help", "-h"};
    auto pr = parse_args(o, allowed, {"input"}, kManualHelp, argc, argv, start, pos);
    if (pr == ParseResult::Help) return 0;
    if (pr == ParseResult::Error) return 1;
    const std::string input = pos[0];
    out.json = o.json_format;
    out.quiet = o.quiet;
    if (out.json) out.res = {{"status", "ok"}, {"command", "manual"}, {"input", input}};

    try {
        sps::Image image = sps::Image::load_png(input);
        out.note("loaded " + input + " (" + std::to_string(image.width()) + "x" +
                 std::to_string(image.height()) + ")");
        apply_background_cleanup(image, o);

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
                out.res["output_dir"] = o.output_dir;
            } else {
                std::cout << "no rects entered, nothing to do.\n";
            }
            out.finish();
            return 0;
        }

        const int count = export_rects(image, manual, o.output_dir, out);
        // manual 总是写 meta.json（这是 manual 的产物）
        const std::string meta_path =
            (std::filesystem::path(o.output_dir) / "meta.json").string();
        std::ofstream f(meta_path);
        if (!f) throw std::runtime_error("cannot open '" + meta_path + "'");
        f << sps::export_json(image, manual, input) << "\n";
        out.note("wrote " + meta_path);
        if (out.json) {
            out.res["count"] = count;
            out.res["output_dir"] = o.output_dir;
            out.res["meta_path"] = meta_path;
        } else {
            std::cout << "done: " << count << " sprite(s) -> " << o.output_dir << "\n";
        }
        out.finish();
        return 0;
    } catch (const std::exception& e) {
        print_runtime_error(out, e);
        return 2;
    }
}

int run_from_json(int argc, char** argv, int start) {
    CliOpts o;
    Out out;
    std::vector<std::string> pos;
    const std::set<std::string> allowed = {
        "--output", "--remove-background", "--background-threshold", "--edge-clean", "--bg-color",
        "--bg-backend", "--bg-url",
        "--format", "-q", "--quiet", "--help", "-h"};
    auto pr = parse_args(o, allowed, {"input", "meta.json"}, kFromJsonHelp, argc, argv,
                         start, pos);
    if (pr == ParseResult::Help) return 0;
    if (pr == ParseResult::Error) return 1;
    const std::string input = pos[0];
    const std::string meta_json = pos[1];
    out.json = o.json_format;
    out.quiet = o.quiet;
    if (out.json)
        out.res = {{"status", "ok"}, {"command", "from-json"}, {"input", input}};

    try {
        sps::Image image = sps::Image::load_png(input);
        out.note("loaded " + input + " (" + std::to_string(image.width()) + "x" +
                 std::to_string(image.height()) + ")");
        std::ifstream f(meta_json);
        if (!f) throw std::runtime_error("cannot open '" + meta_json + "'");
        const std::string json_text((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
        sps::SplitResult loaded;
        if (!sps::load_json(json_text, image.width(), image.height(), loaded))
            throw std::runtime_error("invalid meta.json '" + meta_json +
                                     "' (missing/invalid sprites array)");
        out.note("loaded " + std::to_string(loaded.sprites.size()) + " rect(s) from " +
                 meta_json);

        // 解析相对 mask 路径：相对 meta.json 所在目录
        const std::string meta_dir = std::filesystem::path(meta_json).parent_path().string();
        for (auto& mp : loaded.mask_paths) {
            if (!mp.empty() && std::filesystem::path(mp).is_relative()) {
                mp = (std::filesystem::path(meta_dir) / mp).string();
            }
        }

        apply_background_cleanup(image, o);
        const int count = export_rects(image, loaded, o.output_dir, out);
        if (out.json) {
            out.res["count"] = count;
            out.res["output_dir"] = o.output_dir;
        } else {
            std::cout << "done: " << count << " sprite(s) -> " << o.output_dir << "\n";
        }
        out.finish();
        return 0;
    } catch (const std::exception& e) {
        print_runtime_error(out, e);
        return 2;
    }
}

int run_sheet(int argc, char** argv, int start) {
    CliOpts o;
    Out out;
    std::vector<std::string> pos;
    const std::set<std::string> allowed = {
        "--cols", "--from-json", "--output",
        "--alpha-threshold", "--min-width", "--min-height",
        "--remove-background", "--background-threshold", "--edge-clean", "--bg-color", "--contract",
        "--bg-backend", "--bg-url",
        "--mode", "--cell-size", "--merge-distance",
        "--format", "-q", "--quiet", "--help", "-h"};
    auto pr = parse_args(o, allowed, {"input"}, kSheetHelp, argc, argv, start, pos);
    if (pr == ParseResult::Help) return 0;
    if (pr == ParseResult::Error) return 1;
    const std::string input = pos[0];
    if (!o.sheet_cols_set) {
        std::cerr << "error: missing --cols N (columns per row)\n";
        return 1;
    }
    if (o.sheet_cols <= 0) {
        std::cerr << "error: --cols must be >= 1\n";
        return 1;
    }
    try {
        validate_split_opts(o);
    } catch (const ArgError& e) {
        std::cerr << "error: " << e.msg << "\n";
        return 1;
    }
    out.json = o.json_format;
    out.quiet = o.quiet;
    if (out.json)
        out.res = {{"status", "ok"}, {"command", "sheet"}, {"input", input},
                   {"cols", o.sheet_cols}};

    try {
        sps::Image image = sps::Image::load_png(input);
        out.note("loaded " + input + " (" + std::to_string(image.width()) + "x" +
                 std::to_string(image.height()) + ")");

        // 矩形来源：--from-json（应用 mask）或自动检测
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
            apply_background_cleanup(image, o);
        } else {
            sps::Mask bg_mask;
            apply_background_cleanup(image, o, &bg_mask);
            rects = sps::split_image(image, o.opts,
                                     bg_mask.empty() ? nullptr : &bg_mask);
            if (rects.sprites.empty())
                Out::warn("no sprites detected. Try --remove-background, or adjust "
                          "--mode / --cell-size.");
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

        std::filesystem::create_directories(o.output_dir);
        const std::string sheet_path =
            (std::filesystem::path(o.output_dir) / "sheet.png").string();
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
            (std::filesystem::path(o.output_dir) / "sheet_meta.json").string();
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

int run_remove_background(int argc, char** argv, int start) {
    CliOpts o;
    Out out;
    std::vector<std::string> pos;
    const std::set<std::string> allowed = {
        "--output", "--background-threshold", "--edge-clean", "--bg-color",
        "--bg-backend", "--bg-url", "--format", "-q", "--quiet", "--help", "-h"};
    auto pr = parse_args(o, allowed, {"input"}, kRemoveBgHelp, argc, argv, start, pos);
    if (pr == ParseResult::Help) return 0;
    if (pr == ParseResult::Error) return 1;
    const std::string input = pos[0];
    // 本命令的语义就是去背景：remove_background 恒为真（无需用户再传 flag）
    o.opts.remove_background = true;
    try {
        validate_split_opts(o);
    } catch (const ArgError& e) {
        std::cerr << "error: " << e.msg << "\n";
        return 1;
    }
    out.json = o.json_format;
    out.quiet = o.quiet;
    if (out.json)
        out.res = {{"status", "ok"},
                   {"command", "remove-background"},
                   {"input", input}};

    try {
        sps::Image image = sps::Image::load_png(input);
        out.note("loaded " + input + " (" + std::to_string(image.width()) + "x" +
                 std::to_string(image.height()) + ")");

        // 背景色参考：用户指定优先；color 后端未指定时取环带估计。
        // （在透明化之前采样，RGB 不受 alpha 置零影响，但保持顺序清晰）
        nlohmann::json bg_color_json;
        if (o.opts.has_bg_color) {
            bg_color_json = {{"r", static_cast<int>(o.opts.bg_color.r)},
                             {"g", static_cast<int>(o.opts.bg_color.g)},
                             {"b", static_cast<int>(o.opts.bg_color.b)}};
        } else if (!o.bg_backend_remote) {
            const auto est = sps::estimate_background(image);
            bg_color_json = {{"r", static_cast<int>(est.color.r)},
                             {"g", static_cast<int>(est.color.g)},
                             {"b", static_cast<int>(est.color.b)}};
        }

        sps::Mask bg_mask;
        bool used_remote = false;
        apply_background_cleanup(image, o, &bg_mask, &used_remote, /*keep_alpha=*/true);

        // 背景像素统计：keep_alpha 成功时无二值 mask，以结果图 alpha==0 为准；
        // 其余路径（color / remote 回退）用 mask 计数。
        long bg_px = 0;
        if (used_remote) {
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    if (image.at(x, y).a == 0) ++bg_px;
                }
            }
        } else if (!bg_mask.empty()) {
            for (int y = 0; y < bg_mask.height(); ++y) {
                for (int x = 0; x < bg_mask.width(); ++x) {
                    if (bg_mask.get(x, y)) ++bg_px;
                }
            }
        }
        const long total = static_cast<long>(image.width()) * image.height();
        const double bg_pct = total ? 100.0 * bg_px / total : 0.0;

        // 输出文件名：<stem>_transparent.png（保持原图尺寸，整图透明导出）
        std::filesystem::create_directories(o.output_dir);
        const std::string stem = std::filesystem::path(input).stem().string();
        const std::string out_name =
            (stem.empty() ? "transparent" : stem) + "_transparent.png";
        const std::string out_path =
            (std::filesystem::path(o.output_dir) / out_name).string();
        sps::save_png(image, out_path);

        const std::string backend = used_remote ? "remote" : "color";
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

// ============================ 入口 ============================

}  // namespace

int main(int argc, char* argv[]) {
    // 注册 extra 库（sps_bg_remote）提供的 Remote 背景后端到 core 注册表
    sps::bg_remote::register_backend();

    if (argc < 2) {
        print_main_help(argv[0]);
        return 1;
    }
    const std::string cmd = argv[1];
    if (cmd == "--version" || cmd == "version") {
        std::cout << "sprite-split " << kVersion << "\n";
        return 0;
    }
    if (cmd == "--help" || cmd == "-h" || cmd == "help") {
        print_main_help(argv[0]);
        return 0;
    }
    if (cmd == "info") return run_info(argc, argv, 2);
    if (cmd == "split") return run_split(argc, argv, 2);
    if (cmd == "manual") return run_manual(argc, argv, 2);
    if (cmd == "from-json") return run_from_json(argc, argv, 2);
    if (cmd == "sheet") return run_sheet(argc, argv, 2);
    if (cmd == "remove-background") return run_remove_background(argc, argv, 2);
    std::cerr << "error: unknown command '" << cmd << "'\n\n";
    print_main_help(argv[0]);
    return 1;
}
