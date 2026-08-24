#pragma once

#include "image/image.hpp"
#include "model/sprite_rect.hpp"
#include "model/split_result.hpp"

#include <string>
#include <vector>

namespace sps {

// 将裁剪后的精灵重排成规整网格 sprite sheet（每格居中，格尺寸=最大精灵+padding）。
// 返回新画布；新坐标写入 out_rects（与输入 sprites 同序）。
// cols: 每行精灵数；padding: 精灵间距（像素，默认 4）。
Image repack_sheet(const std::vector<Image>& sprites, int cols, int padding,
                   std::vector<SpriteRect>& out_rects);

// 从原图裁剪出所有精灵（按 rects；可应用 mask_alpha 列表，空=无 mask）
std::vector<Image> crop_sprites(const Image& source,
                                const std::vector<SpriteRect>& rects,
                                const std::vector<std::vector<uint8_t>>& masks = {});

}  // namespace sps
