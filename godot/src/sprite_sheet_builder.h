#pragma once

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace godot {

// SpriteSheetBuilder：精灵重排 → 规整网格 sprite sheet。
// 薄封装 core export/sheet.hpp（crop_sprites + repack_sheet），行为对齐 CLI sheet
// 子命令（输出 sheet.png + sheet_meta.json，src/dst 映射）。独立于 SpriteSplitter
// 的新类新文件——不动既有切分/导出 API，UI 后续按需对接。
//
// 用法：
//   var ssb = SpriteSheetBuilder.new()
//   var r = ssb.build(image, rects, 8, 4)                          # 内存重排（自适应格）
//   var d = ssb.save_sheet(image, rects, 8, 4, "res://out", 128, 128)  # 固定 128×128 格导出
//
// rects: Array[Rect2i]，精灵在原图上的坐标（与切分/导入结果同源）。
// cell_w/cell_h > 0 时强制固定格尺寸（padding 忽略）：精灵居中放入，超出格子部分
// 裁剪；返回 dict 的 "clipped" 字段为被裁精灵数（dst 可见尺寸 < 输入尺寸即被裁）。
// 默认 0 = 自适应（格尺寸 = 最大精灵 + 2*padding），行为与 CLI sheet 一致。
class SpriteSheetBuilder : public RefCounted {
    GDCLASS(SpriteSheetBuilder, RefCounted)

protected:
    static void _bind_methods();

public:
    // 内存重排：裁剪每个精灵并重排到 cols 列网格。
    // 返回 { sheet: Image, rects: Array[Rect2i], clipped: int }（rects = 可见区域，
    // 与输入同序；clipped = 固定格下被裁的精灵数）。失败返回空字典 + 警告。
    Dictionary build(const Ref<Image> &p_image, const Array &p_rects, int p_cols, int p_padding,
                     int p_cell_w, int p_cell_h);

    // 导出：build 后写 <out_dir>/<stem>.png + <out_dir>/<stem>_meta.json（结构与 CLI
    // sheet 命令一致：sheet/width/height/sprites[src,dst]）。支持 res:// 与绝对路径。
    // file_stem：导出文件主名（空 = "sheet"）；overwrite=false 且同名已存在时自动递增
    // <stem>_2、_3…（png/meta 同步），返回 sheet_path 反映实际落盘名。
    // 返回 { sheet_path, sheet_meta_path, width, height, count, clipped }；失败空字典。
    Dictionary save_sheet(const Ref<Image> &p_image, const Array &p_rects, int p_cols,
                          int p_padding, const String &p_out_dir, int p_cell_w, int p_cell_h,
                          const String &p_file_stem, bool p_overwrite);

    // 从多张已切分小图组装 sheet（不依赖源图/rects）：images = Array[Image]，逐张转
    // RGBA8 后按同一重排规则排入网格。返回 { sheet, rects, clipped }；失败空字典 + 警告。
    Dictionary build_from_images(const Array &p_images, int p_cols, int p_padding, int p_cell_w,
                                 int p_cell_h);

    // 从多张小图组装并导出：<out_dir>/<stem>.png + <stem>_meta.json（命名/递增同 save_sheet）。
    // meta 的 src 为每张图自身（0,0 + 尺寸）；src_files（可选 Array[String]）透传进
    // meta 顶部的 "src_files"（记录源文件路径）。返回 { sheet_path, sheet_meta_path,
    // width, height, count, clipped }；失败空字典。
    Dictionary save_from_images(const Array &p_images, int p_cols, int p_padding,
                                const String &p_out_dir, int p_cell_w, int p_cell_h,
                                const String &p_file_stem, bool p_overwrite,
                                const Array &p_src_files);
};

} // namespace godot
