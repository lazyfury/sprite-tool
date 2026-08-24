#pragma once

#include "image/image.hpp"
#include "model/split_result.hpp"

#include <string>

namespace sps {

// 橡皮擦 mask 工具：黑白灰度图（白=保留 / 黑=透明，中间值=半透明）
// mask 尺寸必须与目标图像一致；不一致时按左上角对齐（可裁剪/扩展）。

// 从文件加载灰度 mask 为 alpha 通道（0~255）。失败抛 std::runtime_error。
std::vector<uint8_t> load_mask_alpha(const std::string& path, int width, int height);

// 将 mask alpha 应用到图像：img 的 alpha 通道 = mask 灰度（255=保留）
// 不缩放：mask 与 img 同尺寸时全量应用；否则左上角对齐（缺省区域保留）。
void apply_mask(Image& img, const std::vector<uint8_t>& mask_alpha);

// 生成全白 mask（255 保留），尺寸与 sprite 一致，用于 UI 橡皮擦起点。
Image make_white_mask(int width, int height);

}  // namespace sps
