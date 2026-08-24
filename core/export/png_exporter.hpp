#pragma once

#include "image/image.hpp"

#include <string>

namespace sps {

// 将 RGBA 图像写为 PNG（stb_image_write），失败抛 std::runtime_error
void save_png(const Image& image, const std::string& path);

}  // namespace sps
