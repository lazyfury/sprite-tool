#include "mask/alpha_mask.hpp"

#include <cassert>
#include <cstddef>

namespace sps {

AlphaMask::AlphaMask(int width, int height, uint8_t fill)
    : width_(width), height_(height), data_(static_cast<std::size_t>(width) * height, fill) {}

uint8_t AlphaMask::get(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return 0;
    return data_[idx(x, y)];
}

void AlphaMask::set(int x, int y, uint8_t value) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    data_[idx(x, y)] = value;
}

}  // namespace sps
