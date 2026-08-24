#pragma once

#include "image/image.hpp"
#include "model/split_result.hpp"

#include <string>

namespace sps {

// 将切分结果导出为 JSON 元数据（nlohmann/json）。
// 结构：
// {
//   "image": "input.png",
//   "width": 1234, "height": 1274,
//   "sprites": [ {"x":..,"y":..,"width":..,"height":..}, ... ]
// }
// 返回 JSON 文本（不落盘，由调用方决定写到哪里）。
std::string export_json(const Image& image, const SplitResult& result,
                        const std::string& image_name);

// 从 meta.json 文本加载精灵矩形列表。
// 兼容 export_json 的输出结构；缺失的 sprites 字段视为空列表。
// 越界矩形会被 clamp 到图像范围（宽高至少 1），非法条目跳过。
// 返回 false 表示 JSON 解析失败。
bool load_json(const std::string& json_text, int image_width, int image_height,
               SplitResult& out);

}  // namespace sps
