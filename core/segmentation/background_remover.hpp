#pragma once

#include "image/image.hpp"
#include "mask/mask.hpp"
#include "segmentation/background.hpp"

#include <functional>
#include <memory>
#include <string>

namespace sps {

// 背景清理后端标识。core 只认标识；实现可来自 core（Color）或 extra 库
// （Remote 由 sps_bg_remote::register_backend 注册）。
enum class BackgroundBackend { Color, Remote };

// 各后端参数。只填当前所选后端需要的字段即可。
struct BackgroundRemoverOptions {
    BackgroundOptions color{};  // Color 后端：纯算法参数（threshold / bg_color / edge_passes）
    std::string remote_url;     // Remote 后端：服务 base URL（如 http://127.0.0.1:8000）
};

// 抽象接口：移除背景功能块。
// 输入原图，输出背景 mask（true = 背景，false = 前景）。
// 下游统一复用 make_background_transparent / CCL 管线，
// 不关心具体实现是纯算法还是远程服务。
// 注：CLI 已解耦为独立命令 remove-background（--stdout 管道输出透明图），
// 切分命令（split/sheet）只接受透明图做 alpha 切分。
class BackgroundRemover {
public:
    virtual ~BackgroundRemover() = default;
    virtual Mask process(const Image& image) const = 0;

    // 直接输出透明图（可选接口）：保留 alpha 细节（如 AI 软边 / alpha matting）。
    // 默认实现 = process() 二值 mask + make_background_transparent（硬边，兼容旧行为）；
    // Remote 等后端可 override 直接返回服务端透明图，质量更高。
    // 调用方须保证返回图与原图同尺寸（后端不一致时应自检或回退）。
    virtual Image process_transparent(const Image& image) const {
        const Mask m = process(image);
        Image out = image;
        make_background_transparent(out, m);
        return out;
    }
};

// 后端注册表 + 工厂：core 默认注册 Color；Remote 由 extra 库
// （sps_bg_remote::register_backend）在 create 之前显式注册。
using BackgroundRemoverFactory =
    std::function<std::unique_ptr<BackgroundRemover>(const BackgroundRemoverOptions&)>;
void register_background_remover(BackgroundBackend kind, BackgroundRemoverFactory factory);
// 后端未注册（extra 未链接）时返回 nullptr
std::unique_ptr<BackgroundRemover> create_background_remover(
    BackgroundBackend kind, const BackgroundRemoverOptions& options = {});

// 纯算法版（core 内置，默认注册）：外圈环带采样 + 自适应阈值 flood fill + 边缘清扫。
class ColorBackgroundRemover final : public BackgroundRemover {
public:
    explicit ColorBackgroundRemover(BackgroundOptions options);
    Mask process(const Image& image) const override;

private:
    BackgroundOptions options_;
};

}  // namespace sps
