#include "bg_remote.hpp"

#include <stdexcept>
#include <string>
#include <vector>

// 第三方头文件警告隔离（与 core/image/image.cpp 对 stb 的处理一致）
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#include "httplib.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace sps::bg_remote {

Image remove_background(const std::vector<uint8_t>& png_bytes, const std::string& url) {
    httplib::Client cli(url);
    cli.set_connection_timeout(10, 0);  // 连接超时 10s
    cli.set_read_timeout(120, 0);       // 读取超时 120s（AI 推理可能较慢）

    httplib::UploadFormDataItems items = {
        {"image", std::string(png_bytes.begin(), png_bytes.end()), "input.png", "image/png"}};

    auto res = cli.Post("/api/remove-background", items);
    if (!res) {
        throw std::runtime_error("remote bg service unreachable at '" + url + "': " +
                                 httplib::to_string(res.error()));
    }
    if (res->status != 200) {
        const std::string detail = res->body.size() > 300 ? res->body.substr(0, 300) + "..." : res->body;
        throw std::runtime_error("remote bg service returned HTTP " +
                                 std::to_string(res->status) + ": " + detail);
    }
    if (res->body.empty()) {
        throw std::runtime_error("remote bg service returned empty body");
    }
    return Image::load_png_from_memory(
        reinterpret_cast<const uint8_t*>(res->body.data()), res->body.size());
}

}  // namespace sps::bg_remote
