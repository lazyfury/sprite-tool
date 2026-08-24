#include "mask/mask_io.hpp"

#include <algorithm>
#include <stdexcept>

// stb_image 已在 image.cpp 编译；这里复用声明（只 include 头不定义实现）
#include "stb_image.h"

namespace sps {

std::vector<uint8_t> load_mask_alpha(const std::string& path, int width, int height) {
    int w = 0, h = 0, ch = 0;
    // 灰度加载（force_channels=1）：mask 是黑白图
    unsigned char* raw = stbi_load(path.c_str(), &w, &h, &ch, 1);
    if (raw == nullptr) {
        throw std::runtime_error("sps: failed to load mask '" + path + "': " +
                                 (stbi_failure_reason() ? stbi_failure_reason() : "unknown"));
    }
    // 尺寸不匹配：按左上角对齐（缺省区域保留=255）
    std::vector<uint8_t> alpha(static_cast<std::size_t>(width) * height, 255);
    const int copy_w = std::min(w, width);
    const int copy_h = std::min(h, height);
    for (int y = 0; y < copy_h; ++y) {
        const auto* src_row = raw + static_cast<std::size_t>(y) * w;
        auto* dst_row = alpha.data() + static_cast<std::size_t>(y) * width;
        std::copy_n(src_row, copy_w, dst_row);
    }
    stbi_image_free(raw);
    return alpha;
}

void apply_mask(Image& img, const std::vector<uint8_t>& mask_alpha) {
    const int w = img.width();
    const int h = img.height();
    const std::size_t expected = static_cast<std::size_t>(w) * h;
    if (mask_alpha.size() < expected) {
        throw std::invalid_argument("sps: apply_mask: mask smaller than image");
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            // 乘法叠加：最终 alpha = 原 alpha × mask / 255
            // 白(255)=保留原透明信息；黑(0)=擦除；灰(128)=半透明减弱
            // 不能直接赋值（会把原图透明像素强制变不透明 → 背景泛白）
            const uint8_t orig_a = img.at(x, y).a;
            const uint8_t m = mask_alpha[static_cast<std::size_t>(y) * w + x];
            img.at(x, y).a = static_cast<uint8_t>((orig_a * m) / 255);
        }
    }
}

Image make_white_mask(int width, int height) {
    Image img(width, height, 255);  // RGBA 全 255；UI 使用 R 通道作为灰度
    return img;
}

}  // namespace sps
