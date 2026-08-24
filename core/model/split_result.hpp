#pragma once

#include "sprite_rect.hpp"
#include <string>
#include <vector>

namespace sps {

// 一次切分的结果：所有精灵的包围盒（已按原图坐标）
struct SplitResult {
    std::vector<SpriteRect> sprites;

    // 橡皮擦 mask：与 sprites 平行对齐（同索引）。
    // 每个元素是黑白图文件路径（白=保留/黑=透明），空串 = 该 sprite 无 mask。
    // 仅从 meta.json 加载时填充；算法切分结果为全空串。
    std::vector<std::string> mask_paths;
};

}  // namespace sps
