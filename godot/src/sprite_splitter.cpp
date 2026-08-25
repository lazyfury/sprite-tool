#include "sprite_splitter.h"

#include "conversion.h"
#include "bg_remote.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "core/analyzer.hpp"
#include "core/export/json_exporter.hpp"
#include "core/segmentation/background_remover.hpp"
#include "core/segmentation/splitter.hpp"

#include <algorithm>
#include <exception>
#include <memory>

namespace godot {

// ---------- options 解析（GDScript Dictionary → sps::SplitOptions） ----------
static sps::SplitOptions parse_options(const Dictionary &p_options) {
    sps::SplitOptions opts;

    if (p_options.has("alpha_threshold")) {
        opts.alpha_threshold = static_cast<int>(p_options["alpha_threshold"]);
    }
    if (p_options.has("remove_background")) {
        opts.remove_background = p_options["remove_background"];
    }
    if (p_options.has("background_threshold")) {
        opts.background_threshold = static_cast<int>(p_options["background_threshold"]);
    }
    if (p_options.has("min_width")) {
        opts.min_width = static_cast<int>(p_options["min_width"]);
    }
    if (p_options.has("min_height")) {
        opts.min_height = static_cast<int>(p_options["min_height"]);
    }
    if (p_options.has("merge_nearby")) {
        opts.merge_nearby = p_options["merge_nearby"];
    }
    if (p_options.has("merge_distance")) {
        opts.merge_distance = static_cast<int>(p_options["merge_distance"]);
    }
    if (p_options.has("mode")) {
        const String mode = p_options["mode"];
        if (mode == "grid") {
            opts.mode = sps::DetectionMode::Grid;
        } else if (mode == "auto") {
            opts.mode = sps::DetectionMode::Auto;
        } else {
            opts.mode = sps::DetectionMode::ConnectedComponents;
        }
    }
    if (p_options.has("grid_cell_size")) {
        opts.grid_cell_size = static_cast<int>(p_options["grid_cell_size"]);
    }
    if (p_options.has("padding")) {
        opts.padding = static_cast<int>(p_options["padding"]);
    }
    if (p_options.has("slice_policy")) {
        const String sp = p_options["slice_policy"];
        if (sp == "components") {
            opts.slice_policy = 1;
        } else if (sp == "grid") {
            opts.slice_policy = 2;
        } else {
            opts.slice_policy = 0;  // "auto" / 未知值 → 自动决策
        }
    }
    return opts;
}

void SpriteSplitter::_bind_methods() {
    ClassDB::bind_method(D_METHOD("split", "image", "options"), &SpriteSplitter::split);
    ClassDB::bind_method(D_METHOD("split_detailed", "image", "options"),
                         &SpriteSplitter::split_detailed);
    ClassDB::bind_method(D_METHOD("analyze", "image", "background_threshold"),
                         &SpriteSplitter::analyze, DEFVAL(12));
    ClassDB::bind_method(D_METHOD("remove_background", "image", "options"),
                         &SpriteSplitter::remove_background);
    ClassDB::bind_method(D_METHOD("crop", "image", "rect"), &SpriteSplitter::crop);
    ClassDB::bind_method(D_METHOD("export_sprite", "image", "rect", "path"),
                         &SpriteSplitter::export_sprite);
    ClassDB::bind_method(D_METHOD("split_and_export", "image", "options", "out_dir"),
                         &SpriteSplitter::split_and_export);
    ClassDB::bind_method(D_METHOD("export_metadata", "image", "rects", "image_name", "path"),
                         &SpriteSplitter::export_metadata);
}

Array SpriteSplitter::split(const Ref<Image> &p_image, const Dictionary &p_options) {
    const Dictionary d = split_detailed(p_image, p_options);
    const Variant rects = d.get("rects", Array());
    return rects;
}

Dictionary SpriteSplitter::split_detailed(const Ref<Image> &p_image,
                                          const Dictionary &p_options) {
    Dictionary result;
    result["rects"] = Array();
    result["auto_mode"] = -1;
    if (p_image.is_null()) {
        UtilityFunctions::push_error("SpriteSplitter.split_detailed: image is null");
        return result;
    }
    try {
        const sps::Image img = to_sps_image(p_image);
        if (img.empty()) {
            UtilityFunctions::push_error(
                    "SpriteSplitter.split_detailed: unsupported image (empty after conversion)");
            return result;
        }
        const sps::SplitOptions opts = parse_options(p_options);
        const sps::SplitResult sr = sps::split_image(img, opts);
        Array rects;
        rects.resize(sr.sprites.size());
        for (int i = 0; i < static_cast<int>(sr.sprites.size()); i++) {
            rects[i] = to_rect2i(sr.sprites[static_cast<std::size_t>(i)]);
        }
        result["rects"] = rects;
        // Auto 诊断透传（UI 展示 + 画布 grid overlay）
        result["auto_mode"] = sr.auto_mode;
        result["auto_confidence"] = static_cast<double>(sr.auto_confidence);
        result["auto_raw_components"] = sr.auto_raw_components;
        result["auto_filtered_components"] = sr.auto_filtered_components;
        result["auto_merged_components"] = sr.auto_merged_components;
        result["auto_grid_columns"] = sr.auto_grid_columns;
        result["auto_grid_rows"] = sr.auto_grid_rows;
        result["auto_grid_cell_w"] = sr.auto_grid_cell_w;
        result["auto_grid_cell_h"] = sr.auto_grid_cell_h;
        result["auto_grid_offset_x"] = sr.auto_grid_offset_x;
        result["auto_grid_offset_y"] = sr.auto_grid_offset_y;
        result["auto_occupied_cells"] = sr.auto_occupied_cells;
        result["auto_cells_with_multi"] = sr.auto_cells_with_multi;
    } catch (const std::exception &e) {
        UtilityFunctions::push_error(
                String("SpriteSplitter.split_detailed: {0}").format(String(e.what())));
    }
    return result;
}

Dictionary SpriteSplitter::analyze(const Ref<Image> &p_image, int p_background_threshold) {
    Dictionary d;
    if (p_image.is_null()) {
        return d;
    }
    try {
        const sps::Image img = to_sps_image(p_image);
        if (img.empty()) {
            return d;
        }
        const sps::ImageStats st = sps::analyze_image(img, p_background_threshold);
        d["width"] = st.width;
        d["height"] = st.height;
        d["total_pixels"] = static_cast<int64_t>(st.total_pixels);
        d["opaque_pixels"] = static_cast<int64_t>(st.opaque_pixels);
        d["transparent_pixels"] = static_cast<int64_t>(st.transparent_pixels);
        d["semi_pixels"] = static_cast<int64_t>(st.semi_pixels);
        d["has_transparency"] = st.has_transparency;
        d["uniform_alpha"] = st.uniform_alpha;
        d["component_count"] = st.component_count;
        d["largest_component"] = to_rect2i(st.largest_component);
        d["suggested_min_width"] = st.suggested_min_width;
        d["suggested_min_height"] = st.suggested_min_height;
        d["foreground_percent"] = st.foreground_percent;
    } catch (const std::exception &e) {
        UtilityFunctions::push_error(
                String("SpriteSplitter.analyze: {0}").format(String(e.what())));
    }
    return d;
}

Ref<Image> SpriteSplitter::remove_background(const Ref<Image> &p_image,
                                             const Dictionary &p_options) {
    if (p_image.is_null()) {
        UtilityFunctions::push_error("SpriteSplitter.remove_background: image is null");
        return Ref<Image>();
    }
    try {
        const sps::Image img = to_sps_image(p_image);
        if (img.empty()) {
            UtilityFunctions::push_error(
                    "SpriteSplitter.remove_background: unsupported image (empty after conversion)");
            return Ref<Image>();
        }
        sps::BackgroundRemoverOptions opts;
        if (p_options.has("background_threshold")) {
            opts.color.threshold = static_cast<int>(p_options["background_threshold"]);
        }
        // 吸色：手动指定背景色（Color 0-1 → sps Pixel 0-255）
        if (p_options.has("use_bg_color") && p_options["use_bg_color"] &&
            p_options.has("bg_color")) {
            const Color c = p_options["bg_color"];
            opts.color.has_bg_color = true;
            opts.color.bg_color = {
                static_cast<uint8_t>(std::clamp(c.r, 0.0f, 1.0f) * 255.0f),
                static_cast<uint8_t>(std::clamp(c.g, 0.0f, 1.0f) * 255.0f),
                static_cast<uint8_t>(std::clamp(c.b, 0.0f, 1.0f) * 255.0f), 255};
        }
        // 后端：color（默认）| remote
        sps::BackgroundBackend backend = sps::BackgroundBackend::Color;
        String backend_name = "color";
        if (p_options.has("backend")) {
            const String b = p_options["backend"];
            if (b == "remote") {
                backend = sps::BackgroundBackend::Remote;
                backend_name = "remote";
                if (p_options.has("bg_url")) {
                    opts.remote_url = String(p_options["bg_url"]).utf8().get_data();
                }
                sps::bg_remote::register_backend();  // 幂等；确保 extra 后端已注册
            }
        }
        std::unique_ptr<sps::BackgroundRemover> remover =
                sps::create_background_remover(backend, opts);
        if (!remover) {
            UtilityFunctions::push_error(
                    String("SpriteSplitter.remove_background: backend not registered ({0})")
                            .format(backend_name));
            return Ref<Image>();
        }
        const sps::Image out = remover->process_transparent(img);
        return to_godot_image(out);
    } catch (const std::exception &e) {
        UtilityFunctions::push_error(
                String("SpriteSplitter.remove_background: ") + String(e.what()));
        return Ref<Image>();
    }
}

Ref<Image> SpriteSplitter::crop(const Ref<Image> &p_image, const Rect2i &p_rect) {
    if (p_image.is_null()) {
        return Ref<Image>();
    }
    const Vector2i size = p_image->get_size();
    const Rect2i bounds(0, 0, size.x, size.y);
    const Rect2i clamped = p_rect.intersection(bounds);
    if (clamped.size.x <= 0 || clamped.size.y <= 0) {
        return Ref<Image>();
    }
    return p_image->get_region(clamped);
}

Error SpriteSplitter::export_sprite(const Ref<Image> &p_image, const Rect2i &p_rect,
                                    const String &p_path) {
    if (p_path.is_empty()) {
        return ERR_INVALID_PARAMETER;
    }
    const Ref<Image> sub = crop(p_image, p_rect);
    if (sub.is_null()) {
        return ERR_INVALID_DATA;
    }
    return sub->save_png(p_path);
}

PackedStringArray SpriteSplitter::split_and_export(const Ref<Image> &p_image,
                                                   const Dictionary &p_options,
                                                   const String &p_out_dir) {
    PackedStringArray files;
    if (p_image.is_null()) {
        UtilityFunctions::push_error("SpriteSplitter.split_and_export: image is null");
        return files;
    }
    if (!p_out_dir.is_empty()) {
        DirAccess::make_dir_recursive_absolute(p_out_dir);
    }

    const Array rects = split(p_image, p_options);
    for (int i = 0; i < rects.size(); i++) {
        const Rect2i r = rects[i];
        const String name = String("sprite_") + String::num_int64(i + 1).pad_zeros(2) + ".png";
        const String path = p_out_dir.path_join(name);
        const Error err = export_sprite(p_image, r, path);
        if (err == OK) {
            files.append(path);
        } else {
            UtilityFunctions::push_warning(
                    String("SpriteSplitter.split_and_export: failed to save {0} (err {1})")
                            .format(Array::make(path, static_cast<int64_t>(err))));
        }
    }
    return files;
}

Error SpriteSplitter::export_metadata(const Ref<Image> &p_image, const Array &p_rects,
                                      const String &p_image_name, const String &p_path) {
    if (p_image.is_null() || p_path.is_empty()) {
        return ERR_INVALID_PARAMETER;
    }
    try {
        const sps::Image img = to_sps_image(p_image);
        if (img.empty()) {
            return ERR_INVALID_DATA;
        }
        sps::SplitResult result;
        result.sprites.reserve(p_rects.size());
        for (int i = 0; i < p_rects.size(); i++) {
            result.sprites.push_back(to_sprite_rect(p_rects[i]));
        }
        const std::string json =
                sps::export_json(img, result, p_image_name.utf8().get_data());
        Ref<FileAccess> fa = FileAccess::open(p_path, FileAccess::WRITE);
        if (fa.is_null()) {
            return ERR_CANT_OPEN;
        }
        fa->store_string(String(json.c_str()));
        return fa->get_error() == OK ? OK : fa->get_error();
    } catch (const std::exception &e) {
        UtilityFunctions::push_error(
                String("SpriteSplitter.export_metadata: {0}").format(String(e.what())));
        return ERR_BUG;
    }
}

} // namespace godot
