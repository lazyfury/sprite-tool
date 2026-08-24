#include "bg_remote.hpp"

#include "export/png_exporter.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
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

namespace {

// 调用远程服务，返回解码后的透明 PNG Image。
// 服务不可达 / HTTP 非 200 / 响应不是可解码图片 → 抛 std::runtime_error。
Image fetch_transparent(const Image& image, const std::string& url) {
    const std::vector<uint8_t> png = encode_png(image);

    httplib::Client cli(url);
    cli.set_connection_timeout(10, 0);  // 连接超时 10s
    cli.set_read_timeout(120, 0);       // 读取超时 120s（AI 推理可能较慢）

    httplib::UploadFormDataItems items = {
        {"image", std::string(png.begin(), png.end()), "input.png", "image/png"}};

    auto res = cli.Post("/api/remove-background", items);
    if (!res) {
        throw std::runtime_error("remote bg service unreachable at '" + url + "': " +
                                 httplib::to_string(res.error()));
    }
    if (res->status != 200) {
        const std::string detail =
            res->body.size() > 300 ? res->body.substr(0, 300) + "..." : res->body;
        throw std::runtime_error("remote bg service returned HTTP " +
                                 std::to_string(res->status) + ": " + detail);
    }
    if (res->body.empty()) {
        throw std::runtime_error("remote bg service returned empty body");
    }
    return Image::load_png_from_memory(
        reinterpret_cast<const uint8_t*>(res->body.data()), res->body.size());
}

// 从远程透明图反推背景 mask：alpha 低于阈值 → 背景。
// 阈值 8：rembg 低分辨率 mask 上采样回原尺寸后，背景区会残留
// 1~7 的残噪 alpha（实测 ~3% 像素），全部按前景会导致成百上千个
// 噪点连通分量；8 正好清掉该噪点带，同时保留图标边缘的真实抗锯齿。
// 半透明边缘像素视为前景（保留原图 alpha），使下游统一 mask 语义。
inline constexpr int kAlphaBackgroundThreshold = 8;

Mask alpha_background_mask(const Image& image) {
    Mask m(image.width(), image.height());
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.at(x, y).a < kAlphaBackgroundThreshold) m.set(x, y, true);
        }
    }
    return m;
}

class RemoteBackgroundRemover final : public BackgroundRemover {
public:
    explicit RemoteBackgroundRemover(std::string url) : url_(std::move(url)) {}

    Mask process(const Image& image) const override {
        return alpha_background_mask(fetch_transparent(image, url_));
    }

    // 直接返回服务端透明图：保留 AI 软边 / alpha matting 的渐变 alpha，
    // 质量优于二值 mask 回放。供整图透明导出的 remove-background 使用；
    // split/sheet 仍需 process() 的二值 mask 语义（CCL 依赖）。
    Image process_transparent(const Image& image) const override {
        return fetch_transparent(image, url_);
    }

private:
    std::string url_;
};

}  // namespace

void register_backend() {
    register_background_remover(BackgroundBackend::Remote,
                                [](const BackgroundRemoverOptions& o) {
                                    return std::make_unique<RemoteBackgroundRemover>(o.remote_url);
                                });
}

}  // namespace sps::bg_remote
