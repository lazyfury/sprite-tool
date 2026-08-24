#pragma once

#include "image/image.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace sps::bg_remote {

// 通过远程背景清理服务（如 examples/rembg-api）去除背景：
// 将原图（PNG 字节）multipart 上传到 {url}/api/remove-background，
// 返回解码后的透明 PNG Image。
// 服务不可达 / HTTP 非 200 / 响应不是可解码图片 → 抛 std::runtime_error。
Image remove_background(const std::vector<uint8_t>& png_bytes, const std::string& url);

}  // namespace sps::bg_remote
