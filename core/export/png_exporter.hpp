#pragma once

#include "image/image.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace sps {

// 将 RGBA 图像写为 PNG（stb_image_write），失败抛 std::runtime_error
void save_png(const Image& image, const std::string& path);

// 将 RGBA 图像编码为 PNG 字节（内存缓冲，不落盘）。
// 用于远程上传 / 管道输出等无文件场景，失败抛 std::runtime_error。
std::vector<uint8_t> encode_png(const Image& image);

}  // namespace sps
