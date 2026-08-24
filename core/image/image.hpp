#pragma once

#include "pixel.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace sps {

// 核心图像类型：8-bit RGBA，行优先，每像素 4 字节
// 与 Godot::Image / stb 均通过该内存布局互转
class Image {
public:
    Image() = default;
    Image(int width, int height, uint8_t fill = 0);

    // 从 PNG 文件读取（stb_image），失败抛 std::runtime_error
    static Image load_png(const std::string& path);

    int width() const { return width_; }
    int height() const { return height_; }
    bool empty() const { return width_ == 0 || height_ == 0; }

    Pixel& at(int x, int y) { return reinterpret_cast<Pixel&>(data_[row(y) + x * 4]); }
    const Pixel& at(int x, int y) const {
        return reinterpret_cast<const Pixel&>(data_[row(y) + x * 4]);
    }

    const uint8_t* data() const { return data_.data(); }
    uint8_t* data() { return data_.data(); }
    std::size_t byte_size() const { return data_.size(); }

    // 裁剪（越界部分不裁剪：调用方保证范围，见 SpriteRect clamp 逻辑）
    Image cropped(int x, int y, int w, int h) const;

private:
    std::size_t row(int y) const { return static_cast<std::size_t>(y) * width_ * 4; }

    int width_ = 0;
    int height_ = 0;
    std::vector<uint8_t> data_;
};

}  // namespace sps
