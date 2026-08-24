#pragma once

#include "segmentation/background_remover.hpp"

namespace sps::bg_remote {

// 将 Remote 后端注册进 core 的 BackgroundRemover 注册表
// （在 create_background_remover(Remote, ...) 之前调用一次，CLI main 入口调用）。
// 服务协议见 examples/rembg-api：multipart 上传原图到 {url}/api/remove-background，
// 返回透明 PNG；服务不可达 / HTTP 非 200 / 响应非图片 → process 抛 std::runtime_error，
// 由调用方决定回退。
void register_backend();

}  // namespace sps::bg_remote
