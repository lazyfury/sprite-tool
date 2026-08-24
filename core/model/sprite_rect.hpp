#pragma once

namespace sps {

// 单个精灵的包围盒（相对原图左上角的像素坐标）
struct SpriteRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

}  // namespace sps
