#include "segmentation/background_remover.hpp"

#include <map>
#include <utility>

namespace sps {

ColorBackgroundRemover::ColorBackgroundRemover(BackgroundOptions options)
    : options_(std::move(options)) {}

Mask ColorBackgroundRemover::process(const Image& image) const {
    return background_mask(image, options_);
}

namespace {

using Registry = std::map<BackgroundBackend, BackgroundRemoverFactory>;

// 注册表：core 默认内置 Color（延迟初始化，规避静态初始化顺序问题）。
Registry& registry() {
    static Registry r = [] {
        Registry m;
        m[BackgroundBackend::Color] = [](const BackgroundRemoverOptions& o) {
            return std::make_unique<ColorBackgroundRemover>(o.color);
        };
        return m;
    }();
    return r;
}

}  // namespace

void register_background_remover(BackgroundBackend kind, BackgroundRemoverFactory factory) {
    registry()[kind] = std::move(factory);
}

std::unique_ptr<BackgroundRemover> create_background_remover(
    BackgroundBackend kind, const BackgroundRemoverOptions& options) {
    const auto& r = registry();
    const auto it = r.find(kind);
    if (it == r.end()) return nullptr;
    return it->second(options);
}

}  // namespace sps
