#pragma once

#include "image/image.hpp"
#include "model/split_options.hpp"
#include "model/split_result.hpp"

namespace sps {

class Mask;

// 主入口：Image → Mask → Connected Components → Rects（过滤/裁剪到图像边界）
// CLI 与 Godot frontend 都只调用这一个函数。
//
// bg_mask：可选外部背景 mask（true = 背景）。仅在 options.remove_background 且
// 非空时生效：替代内部 color 算法（background_mask）作为前景判定依据，供
// remote/AI 后端（如 extra/bg_remote）把分割结果接入切分管线。
// 传 nullptr（默认）时行为与原来完全一致（内部 color 计算，零回归）。
SplitResult split_image(const Image& image, const SplitOptions& options,
                        const Mask* bg_mask = nullptr);

}  // namespace sps
