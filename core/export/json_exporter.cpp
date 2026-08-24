#include "export/json_exporter.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace sps {

namespace {

// 从条目读取 int 字段，缺省返回 false
bool read_int(const nlohmann::json& obj, const char* key, int& out) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_number_integer()) return false;
    out = it->get<int>();
    return true;
}

}  // namespace

std::string export_json(const Image& image, const SplitResult& result,
                        const std::string& image_name) {
    nlohmann::json j;
    j["image"] = image_name;
    j["width"] = image.width();
    j["height"] = image.height();
    j["sprites"] = nlohmann::json::array();
    for (std::size_t i = 0; i < result.sprites.size(); ++i) {
        const auto& r = result.sprites[i];
        nlohmann::json item = {
            {"x", r.x}, {"y", r.y}, {"width", r.width}, {"height", r.height}};
        if (i < result.mask_paths.size() && !result.mask_paths[i].empty()) {
            item["mask"] = result.mask_paths[i];
        }
        j["sprites"].push_back(item);
    }
    return j.dump(2);
}

bool load_json(const std::string& json_text, int image_width, int image_height,
               SplitResult& out) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_text);
    } catch (const nlohmann::json::parse_error&) {
        return false;
    }

    auto sprites_it = j.find("sprites");
    if (sprites_it == j.end() || !sprites_it->is_array()) {
        return false;  // 缺少 sprites 字段视为无效（区别于空数组）
    }

    out.sprites.clear();
    out.mask_paths.clear();
    for (const auto& item : *sprites_it) {
        if (!item.is_object()) continue;
        int x = 0, y = 0, w = 0, h = 0;
        if (!read_int(item, "x", x) || !read_int(item, "y", y) ||
            !read_int(item, "width", w) || !read_int(item, "height", h)) {
            continue;  // 字段缺失/非整数 → 跳过
        }
        if (w <= 0 || h <= 0) continue;

        // clamp 到图像范围
        SpriteRect r;
        r.x = std::clamp(x, 0, image_width - 1);
        r.y = std::clamp(y, 0, image_height - 1);
        r.width = std::min(w, image_width - r.x);
        r.height = std::min(h, image_height - r.y);
        if (r.width <= 0 || r.height <= 0) continue;
        out.sprites.push_back(r);

        // 可选 mask 字段（相对路径或绝对路径）
        auto mask_it = item.find("mask");
        if (mask_it != item.end() && mask_it->is_string()) {
            out.mask_paths.push_back(mask_it->get<std::string>());
        } else {
            out.mask_paths.emplace_back();  // 无 mask
        }
    }
    return true;
}

}  // namespace sps
