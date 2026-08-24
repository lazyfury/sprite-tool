#include "segmentation/background_remover.hpp"

#include "bg_remote.hpp"  // extra：sps_bg_remote 注册

#include "catch_amalgamated.hpp"

using namespace sps;

namespace {

// 生成一张简单测试图：白色背景 + 中央灰色前景方块
Image make_test_image(int w, int h) {
    Image img(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const bool fg = (x >= 6 && x < w - 6 && y >= 6 && y < h - 6);
            img.at(x, y) = fg ? Pixel{200, 200, 200, 255} : Pixel{255, 255, 255, 255};
        }
    }
    return img;
}

int foreground_count(const Mask& m) {
    int n = 0;
    for (int y = 0; y < m.height(); ++y) {
        for (int x = 0; x < m.width(); ++x) {
            if (!m.get(x, y)) ++n;  // mask 中 true=背景，false=前景
        }
    }
    return n;
}

}  // namespace

TEST_CASE("color backend via factory equals direct background_mask", "[bg_remover]") {
    const Image img = make_test_image(32, 32);

    BackgroundOptions bg;
    bg.threshold = 12;
    auto remover = create_background_remover(BackgroundBackend::Color,
                                             BackgroundRemoverOptions{std::move(bg), ""});
    REQUIRE(remover != nullptr);

    const Mask via_factory = remover->process(img);
    const Mask direct = background_mask(img, bg);
    REQUIRE(via_factory.width() == direct.width());
    REQUIRE(via_factory.height() == direct.height());
    for (int y = 0; y < direct.height(); ++y) {
        for (int x = 0; x < direct.width(); ++x) {
            REQUIRE(via_factory.get(x, y) == direct.get(x, y));
        }
    }
    // 中央前景保留（mask=false），外围背景被标记（mask=true）
    REQUIRE(!via_factory.get(16, 16));
    REQUIRE(via_factory.get(2, 2));
}

TEST_CASE("remote backend registers into core factory (no network call)", "[bg_remover]") {
    // 注册由 extra 库提供（CLI main 入口调用同一函数）；此处只验证注册后
    // 工厂能创建实例，不调用 process（避免真实网络请求）。
    sps::bg_remote::register_backend();
    auto remover = create_background_remover(BackgroundBackend::Remote,
                                             BackgroundRemoverOptions{{}, "http://127.0.0.1:8000"});
    REQUIRE(remover != nullptr);
}

TEST_CASE("process_transparent default impl equals mask-based transparency", "[bg_remover]") {
    const Image img = make_test_image(32, 32);

    BackgroundOptions bg;
    bg.threshold = 12;
    auto remover = create_background_remover(BackgroundBackend::Color,
                                             BackgroundRemoverOptions{std::move(bg), ""});
    REQUIRE(remover != nullptr);

    const Image via_interface = remover->process_transparent(img);
    Image manual = img;
    make_background_transparent(manual, remover->process(img));
    REQUIRE(via_interface.width() == manual.width());
    REQUIRE(via_interface.height() == manual.height());
    for (int y = 0; y < manual.height(); ++y) {
        for (int x = 0; x < manual.width(); ++x) {
            REQUIRE(via_interface.at(x, y).a == manual.at(x, y).a);
        }
    }
    // 默认实现 = 二值透明化：背景 alpha=0、前景 alpha 保留
    REQUIRE(via_interface.at(2, 2).a == 0);
    REQUIRE(via_interface.at(16, 16).a == 255);
}

TEST_CASE("process_transparent override can preserve soft alpha", "[bg_remover]") {
    // Mock 后端：process 给二值 mask（硬边语义），process_transparent 给软边 alpha，
    // 验证接口允许后端直接输出高质量透明图（remove-background 整图导出路径）。
    struct SoftRemover final : BackgroundRemover {
        Mask process(const Image& img) const override {
            Mask m(img.width(), img.height());
            for (int y = 0; y < img.height(); ++y) {
                for (int x = 0; x < img.width(); ++x) {
                    if (x < 2 || x >= img.width() - 2 || y < 2 ||
                        y >= img.height() - 2) {
                        m.set(x, y, true);  // 外圈背景
                    }
                }
            }
            return m;
        }
        Image process_transparent(const Image& img) const override {
            Image out = img;
            for (int y = 0; y < img.height(); ++y) {
                for (int x = 0; x < img.width(); ++x) {
                    if (x < 2 || x >= img.width() - 2 || y < 2 ||
                        y >= img.height() - 2) {
                        out.at(x, y).a = 0;  // 背景全透明
                    } else if (x == 2 || y == 2 || x == img.width() - 3 ||
                               y == img.height() - 3) {
                        out.at(x, y).a = 100;  // 软边半透明（二值 mask 会丢失）
                    }
                }
            }
            return out;
        }
    };

    SoftRemover remover;
    const Image img = make_test_image(16, 16);
    const Image soft = remover.process_transparent(img);
    REQUIRE(soft.at(0, 0).a == 0);
    REQUIRE(soft.at(2, 2).a == 100);      // 软边保留
    REQUIRE(soft.at(8, 8).a == 255);      // 前景不透明

    const Mask bin = remover.process(img);
    REQUIRE(bin.get(0, 0));               // 二值语义仍是背景
    REQUIRE(!bin.get(2, 2));              // 软边在二值 mask 中归前景（信息丢失点）
}

TEST_CASE("registry accepts overriding factory and dispatches through interface",
          "[bg_remover]") {
    // 用 mock 覆盖 Remote 注册：验证注册表可替换、工厂创建的对象走同一接口
    struct MockRemover final : BackgroundRemover {
        Mask process(const Image&) const override {
            Mask m(4, 4);
            m.set(0, 0, true);
            m.set(3, 3, true);
            return m;
        }
    };
    register_background_remover(BackgroundBackend::Remote,
                                [](const BackgroundRemoverOptions&) {
                                    return std::make_unique<MockRemover>();
                                });
    auto remover = create_background_remover(BackgroundBackend::Remote);
    REQUIRE(remover != nullptr);
    const Image img = make_test_image(4, 4);
    const Mask m = remover->process(img);
    REQUIRE(foreground_count(m) == 14);  // 4x4 - 2 个背景点
    REQUIRE(m.get(0, 0));
    REQUIRE(!m.get(1, 1));

    // 还原真实 remote 注册，避免影响其它测试
    sps::bg_remote::register_backend();
}
