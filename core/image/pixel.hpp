#pragma once

#include <cstdint>

namespace sps {

// 8-bit RGBA 像素
struct Pixel {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
};

}  // namespace sps
