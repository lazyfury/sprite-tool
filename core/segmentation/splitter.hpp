#pragma once

#include "image/image.hpp"
#include "model/split_options.hpp"
#include "model/split_result.hpp"

namespace sps {

// 主入口：Image → Mask → Connected Components → Rects（过滤/裁剪到图像边界）
// CLI 与 Godot frontend 都只调用这一个函数。
SplitResult split_image(const Image& image, const SplitOptions& options);

}  // namespace sps
