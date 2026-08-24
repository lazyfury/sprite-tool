#include "image/image.hpp"

#include <stdexcept>
#include <string>

// stb_image 实现只在 image.cpp 编译一次
// 第三方代码警告隔离：不污染项目自身的 -Wall -Wextra 检查
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace sps {

Image::Image(int width, int height, uint8_t fill)
    : width_(width),
      height_(height),
      data_(static_cast<std::size_t>(width) * height * 4, fill) {}

Image Image::load_png(const std::string& path) {
    int w = 0, h = 0, channels = 0;
    // force_channels=4：统一转为 RGBA，stb 负责灰度/调色板等格式转换
    unsigned char* raw =
        stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (raw == nullptr) {
        throw std::runtime_error("sps: failed to load image '" + path + "': " +
                                 (stbi_failure_reason() ? stbi_failure_reason() : "unknown"));
    }

    Image img(w, h);
    const std::size_t n = static_cast<std::size_t>(w) * h * 4;
    std::copy(raw, raw + n, img.data());
    stbi_image_free(raw);
    return img;
}

Image Image::load_png_from_memory(const uint8_t* data, std::size_t size) {
    int w = 0, h = 0, channels = 0;
    unsigned char* raw =
        stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &channels, 4);
    if (raw == nullptr) {
        throw std::runtime_error(std::string("sps: failed to decode image from memory: ") +
                                 (stbi_failure_reason() ? stbi_failure_reason() : "unknown"));
    }

    Image img(w, h);
    const std::size_t n = static_cast<std::size_t>(w) * h * 4;
    std::copy(raw, raw + n, img.data());
    stbi_image_free(raw);
    return img;
}

Image Image::cropped(int x, int y, int w, int h) const {
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > width_ || y + h > height_) {
        throw std::out_of_range("sps: crop rectangle out of image bounds");
    }
    Image out(w, h);
    for (int yy = 0; yy < h; ++yy) {
        std::copy_n(data_.begin() + row(y + yy) + x * 4, static_cast<std::size_t>(w) * 4,
                    out.data_.begin() + static_cast<std::size_t>(yy) * w * 4);
    }
    return out;
}

}  // namespace sps
