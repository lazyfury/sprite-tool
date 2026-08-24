#pragma once

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/rect2i.hpp>

#include "core/image/image.hpp"
#include "core/model/sprite_rect.hpp"

namespace godot {

// godot::Image → sps::Image（RGBA8 像素深拷贝）。
// 输入格式非 RGBA8 时先重建副本再转换，不修改调用方持有的 Image 对象。
sps::Image to_sps_image(const Ref<Image> &src);

// sps::Image → godot::Image（RGBA8，无 mipmaps）。
Ref<Image> to_godot_image(const sps::Image &src);

// SpriteRect <-> Rect2i（坐标语义一致：左上角 + 宽高）
sps::SpriteRect to_sprite_rect(const Rect2i &r);
Rect2i to_rect2i(const sps::SpriteRect &r);

} // namespace godot
