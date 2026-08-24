#pragma once

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/rect2i.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

// SpriteSplitter：GDScript 可调用的精灵表切分工具（薄封装 core 的 split_image）。
// 用法：
//   var ss = SpriteSplitter.new()
//   var rects: Array = ss.split(image, { "mode": "auto", "min_width": 4, "min_height": 4 })
//   var files: PackedStringArray = ss.split_and_export(image, opts, "res://sprites")
//
// options 字典键（缺省取默认值）：
//   alpha_threshold      int    前景判定 alpha 阈值，默认 1
//   remove_background    bool   是否先去背景再切分，默认 false
//   background_threshold int    RGB 曼哈顿距离阈值，默认 12
//   min_width/min_height int    过滤最小尺寸，默认 1
//   merge_nearby         bool   邻近合并开关，默认 false
//   merge_distance       int    合并距离（px），默认 0
//   mode                 String "components" | "grid" | "auto"，默认 "components"
//   grid_cell_size       int    grid 模式格子尺寸，默认 16
class SpriteSplitter : public RefCounted {
    GDCLASS(SpriteSplitter, RefCounted)

protected:
    static void _bind_methods();

public:
    // 切分：返回 Array[Rect2i]（按原图坐标）。image 为 null 或解析失败返回空数组。
    Array split(const Ref<Image> &p_image, const Dictionary &p_options);

    // 分析：返回 Dictionary 统计（alpha 分布 / 分量数 / 推荐 min 尺寸），供 UI 参数推荐。
    Dictionary analyze(const Ref<Image> &p_image, int p_background_threshold = 12);

    // 裁剪：返回子图（godot::Image），rect 越界自动 clamp。image 为 null 返回 null。
    Ref<Image> crop(const Ref<Image> &p_image, const Rect2i &p_rect);

    // 导出单个精灵：裁剪 + 编码 PNG + 写盘到 p_path（支持 res:// 与绝对路径）。
    // 成功返回 OK，失败返回 ERR_CANT_CREATE / ERR_INVALID_DATA 等。
    Error export_sprite(const Ref<Image> &p_image, const Rect2i &p_rect, const String &p_path);

    // 切分并导出全部精灵到 p_out_dir（自动建目录）。
    // 返回 PackedStringArray（导出的 PNG 路径列表）；失败精灵跳过并在 stderr 提示。
    PackedStringArray split_and_export(const Ref<Image> &p_image, const Dictionary &p_options,
                                       const String &p_out_dir);

    // 导出切分元数据：p_rects（Array[Rect2i]，通常来自 split()）→ JSON 写盘到 p_path。
    // 结构与 CLI 的 meta.json 完全一致（core export_json 同源：image/width/height/sprites[]）。
    // 成功返回 OK；p_image 为 null / 路径为空返回 ERR_INVALID_PARAMETER，写盘失败返回对应 Error。
    Error export_metadata(const Ref<Image> &p_image, const Array &p_rects,
                          const String &p_image_name, const String &p_path);
};

} // namespace godot
