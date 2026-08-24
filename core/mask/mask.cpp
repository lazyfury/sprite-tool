#include "mask/mask.hpp"

namespace sps {

Mask::Mask(int width, int height, bool fill)
    : width_(width), height_(height), bits_(static_cast<std::size_t>(width) * height, fill ? 1 : 0) {}

bool Mask::get(int x, int y) const {
    return bits_[idx(x, y)] != 0;
}

void Mask::set(int x, int y, bool value) {
    bits_[idx(x, y)] = value ? 1 : 0;
}

bool Mask::any_foreground() const {
    for (uint8_t b : bits_) {
        if (b) return true;
    }
    return false;
}

}  // namespace sps
