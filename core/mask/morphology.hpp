#pragma once

#include "mask/mask.hpp"

namespace sps {

// 形态学膨胀：前景像素向 4 邻域扩展 radius 圈。
// 用于合并同角色被透明缝隙拆开的部件（膨胀→CCL→腐蚀回原边界）。
Mask dilate(const Mask& mask, int radius);

// 形态学腐蚀：前景像素向内收缩 radius 圈（radius=0 时原样返回）。
Mask erode(const Mask& mask, int radius);

}  // namespace sps
