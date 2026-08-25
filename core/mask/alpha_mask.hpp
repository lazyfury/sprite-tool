#pragma once

#include <cstdint>
#include <vector>

namespace sps {

// 灰度掩码（alpha 羽化产物）：0-255 表示「背景密度」，
// 255 = 完全背景（alpha 置 0），0 = 完全前景，中间值 = 半透明过渡（羽化边缘）。
// 与二值 Mask 互补：Mask 供分割/CCL 消费，AlphaMask 仅供 alpha 应用（软边去背景）。
class AlphaMask {
public:
    AlphaMask() = default;
    AlphaMask(int width, int height, uint8_t fill = 0);

    int width() const { return width_; }
    int height() const { return height_; }
    bool empty() const { return width_ == 0 || height_ == 0; }

    // 0-255：0 = 前景，255 = 背景
    uint8_t get(int x, int y) const;
    void set(int x, int y, uint8_t value);

private:
    std::size_t idx(int x, int y) const {
        return static_cast<std::size_t>(y) * width_ + x;
    }

    int width_ = 0;
    int height_ = 0;
    std::vector<uint8_t> data_;
};

}  // namespace sps
