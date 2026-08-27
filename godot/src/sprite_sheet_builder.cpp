#include "sprite_sheet_builder.h"

#include "conversion.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "core/export/sheet.hpp"

#include <nlohmann/json.hpp>

#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace godot {

namespace {

// 精灵列表（已裁剪 / 已加载的小图）→ 重排网格。
// 返回 {sheet: Image, rects: Array[Rect2i], clipped: int}；失败（空列表/重排失败）空字典。
Dictionary repack_sprites(const std::vector<sps::Image> &sprites, int cols, int padding,
                          int cell_w, int cell_h) {
    Dictionary result;
    if (sprites.empty()) {
        UtilityFunctions::push_warning("SpriteSheetBuilder: no sprites to repack");
        return result;
    }
    // src 记录精灵自身（0,0 + 尺寸）：clipped 判定 = dst 可见尺寸 < 精灵尺寸
    std::vector<sps::SpriteRect> src;
    src.reserve(sprites.size());
    for (const auto &s : sprites) {
        src.push_back({0, 0, s.width(), s.height()});
    }
    std::vector<sps::SpriteRect> new_rects;
    const sps::Image sheet = sps::repack_sheet(sprites, cols, padding, new_rects, cell_w, cell_h);
    if (sheet.empty()) {
        return result;
    }
    int clipped = 0;
    for (std::size_t i = 0; i < new_rects.size(); i++) {
        if (new_rects[i].width < src[i].width || new_rects[i].height < src[i].height) {
            clipped++;
        }
    }
    Array rects;
    rects.resize(static_cast<int>(new_rects.size()));
    for (int i = 0; i < static_cast<int>(new_rects.size()); i++) {
        rects[i] = to_rect2i(new_rects[static_cast<std::size_t>(i)]);
    }
    result["sheet"] = to_godot_image(sheet);
    result["rects"] = rects;
    result["clipped"] = clipped;
    return result;
}

// 解析最终落盘主名：空 = "sheet"；overwrite=false 时同名递增 <stem>_2、_3…（png 存在即冲突）
String resolve_stem(const String &out_dir, const String &file_stem, bool overwrite) {
    String stem = file_stem.strip_edges();
    if (stem.is_empty()) {
        stem = "sheet";
    }
    if (!overwrite) {
        int n = 1;
        String candidate = stem;
        while (FileAccess::file_exists(out_dir.path_join(candidate + String(".png")))) {
            n++;
            candidate = stem + String("_") + String::num_int64(n);
        }
        stem = candidate;
    }
    return stem;
}

// 写 sheet.png + sheet_meta.json（src_rects 与 dst_rects 同序；src_files 可选透传进 meta 顶部）。
// 成功返回 true；失败 push_error 并返回 false。
bool write_sheet_files(const Ref<Image> &sheet, const Array &src_rects, const Array &dst_rects,
                       const String &sheet_path, const String &meta_path,
                       const Array &src_files) {
    const Error png_err = sheet->save_png(sheet_path);
    if (png_err != OK) {
        UtilityFunctions::push_error(
                String("SpriteSheetBuilder: save_png failed (err {0})")
                        .format(static_cast<int64_t>(png_err)));
        return false;
    }
    // sheet_meta.json：与 CLI sheet 命令同构（sheet/width/height/sprites[src,dst]）
    nlohmann::json j;
    j["sheet"] = sheet_path.utf8().get_data();
    j["width"] = sheet->get_width();
    j["height"] = sheet->get_height();
    j["sprites"] = nlohmann::json::array();
    for (int i = 0; i < dst_rects.size(); i++) {
        const sps::SpriteRect s = to_sprite_rect(src_rects[i]);
        const sps::SpriteRect d = to_sprite_rect(dst_rects[i]);
        j["sprites"].push_back({{"src", {{"x", s.x}, {"y", s.y},
                                         {"width", s.width}, {"height", s.height}}},
                                {"dst", {{"x", d.x}, {"y", d.y},
                                         {"width", d.width}, {"height", d.height}}}});
    }
    if (!src_files.is_empty()) {
        j["src_files"] = nlohmann::json::array();
        for (int i = 0; i < src_files.size(); i++) {
            j["src_files"].push_back(String(src_files[i]).utf8().get_data());
        }
    }
    Ref<FileAccess> fa = FileAccess::open(meta_path, FileAccess::WRITE);
    if (fa.is_null()) {
        UtilityFunctions::push_error(
                String("SpriteSheetBuilder: cannot open {0}").format(meta_path));
        return false;
    }
    fa->store_string(String(j.dump(2).c_str()));
    if (fa->get_error() != OK) {
        UtilityFunctions::push_error(
                String("SpriteSheetBuilder: write failed {0}").format(meta_path));
        return false;
    }
    return true;
}

} // namespace

void SpriteSheetBuilder::_bind_methods() {
    ClassDB::bind_method(D_METHOD("build", "image", "rects", "cols", "padding", "cell_w", "cell_h"),
                         &SpriteSheetBuilder::build, DEFVAL(4), DEFVAL(0), DEFVAL(0));
    ClassDB::bind_method(D_METHOD("save_sheet", "image", "rects", "cols", "padding", "out_dir",
                                  "cell_w", "cell_h", "file_stem", "overwrite"),
                         &SpriteSheetBuilder::save_sheet, DEFVAL(4), DEFVAL(0), DEFVAL(0),
                         DEFVAL(String()), DEFVAL(true));
    ClassDB::bind_method(D_METHOD("build_from_images", "images", "cols", "padding", "cell_w",
                                  "cell_h"),
                         &SpriteSheetBuilder::build_from_images, DEFVAL(4), DEFVAL(0), DEFVAL(0));
    ClassDB::bind_method(D_METHOD("save_from_images", "images", "cols", "padding", "out_dir",
                                  "cell_w", "cell_h", "file_stem", "overwrite", "src_files"),
                         &SpriteSheetBuilder::save_from_images, DEFVAL(4), DEFVAL(0), DEFVAL(0),
                         DEFVAL(String()), DEFVAL(true), DEFVAL(Array()));
}

Dictionary SpriteSheetBuilder::build(const Ref<Image> &p_image, const Array &p_rects,
                                     int p_cols, int p_padding, int p_cell_w, int p_cell_h) {
    Dictionary result;
    if (p_image.is_null()) {
        UtilityFunctions::push_warning("SpriteSheetBuilder.build: image is null");
        return result;
    }
    if (p_rects.is_empty()) {
        UtilityFunctions::push_warning("SpriteSheetBuilder.build: rects is empty");
        return result;
    }
    if (p_cols <= 0) {
        UtilityFunctions::push_warning("SpriteSheetBuilder.build: cols must be > 0");
        return result;
    }
    try {
        const sps::Image img = to_sps_image(p_image);
        if (img.empty()) {
            UtilityFunctions::push_warning(
                    "SpriteSheetBuilder.build: unsupported image (empty after conversion)");
            return result;
        }
        std::vector<sps::SpriteRect> src;
        src.reserve(static_cast<std::size_t>(p_rects.size()));
        for (int i = 0; i < p_rects.size(); i++) {
            src.push_back(to_sprite_rect(p_rects[i]));
        }
        const std::vector<sps::Image> sprites = sps::crop_sprites(img, src);
        return repack_sprites(sprites, p_cols, p_padding, p_cell_w, p_cell_h);
    } catch (const std::exception &e) {
        UtilityFunctions::push_error(
                String("SpriteSheetBuilder.build: {0}").format(String(e.what())));
    }
    return result;
}

Dictionary SpriteSheetBuilder::build_from_images(const Array &p_images, int p_cols, int p_padding,
                                                 int p_cell_w, int p_cell_h) {
    Dictionary result;
    if (p_images.is_empty()) {
        UtilityFunctions::push_warning("SpriteSheetBuilder.build_from_images: images is empty");
        return result;
    }
    if (p_cols <= 0) {
        UtilityFunctions::push_warning(
                "SpriteSheetBuilder.build_from_images: cols must be > 0");
        return result;
    }
    try {
        std::vector<sps::Image> sprites;
        sprites.reserve(static_cast<std::size_t>(p_images.size()));
        for (int i = 0; i < p_images.size(); i++) {
            const Ref<Image> im = p_images[i];
            if (im.is_null()) {
                continue;
            }
            const sps::Image s = to_sps_image(im);
            if (s.empty()) {
                continue;
            }
            sprites.push_back(s);
        }
        if (sprites.empty()) {
            UtilityFunctions::push_warning(
                    "SpriteSheetBuilder.build_from_images: no valid images");
            return result;
        }
        return repack_sprites(sprites, p_cols, p_padding, p_cell_w, p_cell_h);
    } catch (const std::exception &e) {
        UtilityFunctions::push_error(
                String("SpriteSheetBuilder.build_from_images: {0}").format(String(e.what())));
    }
    return result;
}

Dictionary SpriteSheetBuilder::save_sheet(const Ref<Image> &p_image, const Array &p_rects,
                                          int p_cols, int p_padding, const String &p_out_dir,
                                          int p_cell_w, int p_cell_h, const String &p_file_stem,
                                          bool p_overwrite) {
    Dictionary result;
    if (p_out_dir.is_empty()) {
        UtilityFunctions::push_warning("SpriteSheetBuilder.save_sheet: out_dir is empty");
        return result;
    }
    // 复用 build：裁剪 + 重排（sheet 图 + 新坐标）；失败时 build 已输出具体原因
    const Dictionary built = build(p_image, p_rects, p_cols, p_padding, p_cell_w, p_cell_h);
    if (built.is_empty()) {
        return result;
    }
    try {
        DirAccess::make_dir_recursive_absolute(p_out_dir);
        const String stem = resolve_stem(p_out_dir, p_file_stem, p_overwrite);
        const String sheet_path = p_out_dir.path_join(stem + String(".png"));
        const String meta_path = p_out_dir.path_join(stem + String("_meta.json"));
        const Ref<Image> sheet = built["sheet"];
        const Array dst_rects = built["rects"];
        if (!write_sheet_files(sheet, p_rects, dst_rects, sheet_path, meta_path, Array())) {
            return result;
        }
        result["sheet_path"] = sheet_path;
        result["sheet_meta_path"] = meta_path;
        result["width"] = sheet->get_width();
        result["height"] = sheet->get_height();
        result["count"] = dst_rects.size();
        result["clipped"] = built["clipped"];
    } catch (const std::exception &e) {
        UtilityFunctions::push_error(
                String("SpriteSheetBuilder.save_sheet: {0}").format(String(e.what())));
        return result;
    }
    return result;
}

Dictionary SpriteSheetBuilder::save_from_images(const Array &p_images, int p_cols, int p_padding,
                                                const String &p_out_dir, int p_cell_w, int p_cell_h,
                                                const String &p_file_stem, bool p_overwrite,
                                                const Array &p_src_files) {
    Dictionary result;
    if (p_out_dir.is_empty()) {
        UtilityFunctions::push_warning("SpriteSheetBuilder.save_from_images: out_dir is empty");
        return result;
    }
    const Dictionary built = build_from_images(p_images, p_cols, p_padding, p_cell_w, p_cell_h);
    if (built.is_empty()) {
        return result;   // build_from_images 已输出具体原因
    }
    try {
        DirAccess::make_dir_recursive_absolute(p_out_dir);
        const String stem = resolve_stem(p_out_dir, p_file_stem, p_overwrite);
        const String sheet_path = p_out_dir.path_join(stem + String(".png"));
        const String meta_path = p_out_dir.path_join(stem + String("_meta.json"));
        const Ref<Image> sheet = built["sheet"];
        const Array dst_rects = built["rects"];
        // src = 每张图自身（0,0 + 尺寸）；与 dst 同序
        Array src_rects;
        src_rects.resize(dst_rects.size());
        for (int i = 0; i < dst_rects.size(); i++) {
            const Ref<Image> im = p_images[i];
            if (im.is_null()) {
                src_rects[i] = Rect2i(0, 0, 0, 0);
            } else {
                src_rects[i] = Rect2i(0, 0, im->get_width(), im->get_height());
            }
        }
        if (!write_sheet_files(sheet, src_rects, dst_rects, sheet_path, meta_path, p_src_files)) {
            return result;
        }
        result["sheet_path"] = sheet_path;
        result["sheet_meta_path"] = meta_path;
        result["width"] = sheet->get_width();
        result["height"] = sheet->get_height();
        result["count"] = dst_rects.size();
        result["clipped"] = built["clipped"];
    } catch (const std::exception &e) {
        UtilityFunctions::push_error(
                String("SpriteSheetBuilder.save_from_images: {0}").format(String(e.what())));
        return result;
    }
    return result;
}

} // namespace godot
