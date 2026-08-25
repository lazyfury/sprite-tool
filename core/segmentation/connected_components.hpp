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

// 精灵级组件：包围盒 + 中心点 + 面积 + cell 归属（Auto 管线使用）
struct ComponentSprite {
    SpriteRect bounds;
    int cx = 0;  // bbox 中心 X
    int cy = 0;  // bbox 中心 Y
    int area = 0;
    int cell_x = -1;  // Component→Cell mapping 后填充；-1 = 未映射
    int cell_y = -1;
};

// Two-pass connected component labeling（O(W×H)）：
// 第一遍用 4-邻域 给前景像素标临时 label 并记录等价关系，
// 第二遍经 union-find 压缩后统计各分量的包围盒与面积。
std::vector<Component> connected_components(const Mask& mask);

// 精灵级组件检测：CCL 后按 min_width / min_height / min_pixels 过滤，
// 返回 ComponentSprite（cx/cy 为中心点，cell_x/cell_y 保持 -1 待 mapping）。
std::vector<ComponentSprite> detect_components(const Mask& mask, int min_width,
                                               int min_height, int min_pixels);

}  // namespace sps
