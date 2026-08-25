#include "sprite_splitter.h"

#include "conversion.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "core/analyzer.hpp"
#include "core/export/json_exporter.hpp"
#include "core/segmentation/background_remover.hpp"
#include "core/segmentation/splitter.hpp"

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
    return opts;
}

void SpriteSplitter::_bind_methods() {
    ClassDB::bind_method(D_METHOD("split", "image", "options"), &SpriteSplitter::split);
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
    Array result;
    if (p_image.is_null()) {
        UtilityFunctions::push_error("SpriteSplitter.split: image is null");
        return result;
    }
    try {
        const sps::Image img = to_sps_image(p_image);
        if (img.empty()) {
            UtilityFunctions::push_error("SpriteSplitter.split: unsupported image (empty after conversion)");
            return result;
        }
        const sps::SplitOptions opts = parse_options(p_options);
        const sps::SplitResult sr = sps::split_image(img, opts);
        result.resize(sr.sprites.size());
        for (int i = 0; i < static_cast<int>(sr.sprites.size()); i++) {
            result[i] = to_rect2i(sr.sprites[static_cast<std::size_t>(i)]);
        }
    } catch (const std::exception &e) {
        UtilityFunctions::push_error(
                String("SpriteSplitter.split: {0}").format(String(e.what())));
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
        std::unique_ptr<sps::BackgroundRemover> remover =
                sps::create_background_remover(sps::BackgroundBackend::Color, opts);
        if (!remover) {
            UtilityFunctions::push_error(
                    "SpriteSplitter.remove_background: Color backend not registered");
            return Ref<Image>();
        }
        const sps::Image out = remover->process_transparent(img);
        return to_godot_image(out);
    } catch (const std::exception &e) {
        UtilityFunctions::push_error(
                String("SpriteSplitter.remove_background: {0}").format(String(e.what())));
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
