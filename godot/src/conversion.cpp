#include "conversion.h"

#include <godot_cpp/variant/packed_byte_array.hpp>

#include <cstring>

namespace godot {

sps::Image to_sps_image(const Ref<Image> &src) {
    sps::Image out;
    if (src.is_null()) {
        return out;
    }

    Ref<Image> img = src;
    if (img->get_format() != Image::FORMAT_RGBA8) {
        // 重建一份同格式副本再就地转换，避免污染调用方持有的 Image。
        Vector2i sz = img->get_size();
        PackedByteArray raw = img->get_data();
        Ref<Image> copy =
                Image::create_from_data(sz.x, sz.y, false, img->get_format(), raw);
        if (copy.is_null()) {
            return out;
        }
        copy->convert(Image::FORMAT_RGBA8);
        img = copy;
    }

    Vector2i size = img->get_size();
    PackedByteArray data = img->get_data();
    const uint8_t *p = data.ptr();
    const int64_t pixel_bytes = static_cast<int64_t>(size.x) * size.y * 4;
    if (pixel_bytes <= 0 || data.size() < pixel_bytes) {
        return out;
    }

    out = sps::Image(size.x, size.y);
    std::memcpy(out.data(), p, static_cast<std::size_t>(pixel_bytes));
    return out;
}

Ref<Image> to_godot_image(const sps::Image &src) {
    if (src.empty()) {
        return Ref<Image>();
    }
    const int64_t pixel_bytes = static_cast<int64_t>(src.width()) * src.height() * 4;
    PackedByteArray data;
    data.resize(pixel_bytes);
    uint8_t *dst = data.ptrw();
    std::memcpy(dst, src.data(), static_cast<std::size_t>(pixel_bytes));
    return Image::create_from_data(src.width(), src.height(), false, Image::FORMAT_RGBA8, data);
}

sps::SpriteRect to_sprite_rect(const Rect2i &r) {
    sps::SpriteRect out;
    out.x = r.position.x;
    out.y = r.position.y;
    out.width = r.size.x;
    out.height = r.size.y;
    return out;
}

Rect2i to_rect2i(const sps::SpriteRect &r) {
    return Rect2i(r.x, r.y, r.width, r.height);
}

} // namespace godot
