#include "export/png_exporter.hpp"

#include <stdexcept>

// stb_image_write 实现只在 png_exporter.cpp 编译一次
// 第三方代码警告隔离：不污染项目自身的 -Wall -Wextra 检查
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace sps {

void save_png(const Image& image, const std::string& path) {
    if (image.empty()) {
        throw std::runtime_error("sps: cannot save empty image to '" + path + "'");
    }
    const int ok = stbi_write_png(path.c_str(), image.width(), image.height(), 4,
                                  image.data(), image.width() * 4);
    if (ok == 0) {
        throw std::runtime_error("sps: failed to write PNG '" + path + "'");
    }
}

}  // namespace sps
