#pragma once

#include <cstdint>
#include <vector>

namespace sps {

// 二值前景/背景掩码（一等公民）：
// 所有分割算法（alpha/color/floodfill/AI）最终都输出 Mask，
// 再由 connected_components 消费，解耦「如何判定前景」与「如何切分」
class Mask {
public:
    Mask() = default;
    Mask(int width, int height, bool fill = false);

    int width() const { return width_; }
    int height() const { return height_; }
    bool empty() const { return width_ == 0 || height_ == 0; }

    bool get(int x, int y) const;
    void set(int x, int y, bool value);

    // 是否存在任何前景像素
    bool any_foreground() const;

private:
    std::size_t idx(int x, int y) const {
        return static_cast<std::size_t>(y) * width_ + x;
    }

    int width_ = 0;
    int height_ = 0;
    std::vector<uint8_t> bits_;  // 0/1，字节数组便于未来 SIMD
};

}  // namespace sps
