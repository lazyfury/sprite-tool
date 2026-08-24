#pragma once

#include "mask/mask.hpp"
#include "model/sprite_rect.hpp"

#include <vector>

namespace sps {

// 一个连通分量：包围盒 + 前景像素面积
struct Component {
    SpriteRect bounds;
    int area = 0;
};

// Two-pass connected component labeling（O(W×H)）：
// 第一遍用 4-邻域 给前景像素标临时 label 并记录等价关系，
// 第二遍经 union-find 压缩后统计各分量的包围盒与面积。
std::vector<Component> connected_components(const Mask& mask);

}  // namespace sps
