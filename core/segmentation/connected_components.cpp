#include "segmentation/connected_components.hpp"

#include <algorithm>
#include <limits>
#include <vector>

namespace sps {

namespace {

// 简化并查集：只支持 union + 路径压缩 find
class UnionFind {
public:
    explicit UnionFind(int n) : parent_(n) {
        for (int i = 0; i < n; ++i) parent_[i] = i;
    }
    int find(int x) {
        while (parent_[x] != x) {
            parent_[x] = parent_[parent_[x]];  // 路径压缩
            x = parent_[x];
        }
        return x;
    }
    void unite(int a, int b) {
        int ra = find(a), rb = find(b);
        if (ra != rb) parent_[ra] = rb;
    }

private:
    std::vector<int> parent_;
};

}  // namespace

std::vector<Component> connected_components(const Mask& mask) {
    const int w = mask.width();
    const int h = mask.height();
    std::vector<Component> out;
    if (w <= 0 || h <= 0 || !mask.any_foreground()) return out;

    // ---- 第一遍：标号 ----
    std::vector<int> labels(static_cast<std::size_t>(w) * h, 0);
    UnionFind uf(w * h + 1);  // 0 保留给背景，label 从 1 开始
    int next_label = 1;

    auto label_at = [&](int x, int y) -> int& {
        return labels[static_cast<std::size_t>(y) * w + x];
    };

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!mask.get(x, y)) continue;

            int left = (x > 0 && mask.get(x - 1, y)) ? label_at(x - 1, y) : 0;
            int up = (y > 0 && mask.get(x, y - 1)) ? label_at(x, y - 1) : 0;

            if (left == 0 && up == 0) {
                label_at(x, y) = next_label++;
            } else if (left != 0 && up == 0) {
                label_at(x, y) = left;
            } else if (left == 0 && up != 0) {
                label_at(x, y) = up;
            } else {
                label_at(x, y) = left;
                uf.unite(left, up);  // 左/上属于同一分量，记录等价
            }
        }
    }

    // ---- 第二遍：压缩标签并统计 ----
    const int n_labels = next_label;
    std::vector<int> root_of(static_cast<std::size_t>(n_labels), 0);
    for (int l = 1; l < n_labels; ++l) root_of[l] = uf.find(l);

    // 为每个 root 收集统计信息
    std::vector<int> root_index(static_cast<std::size_t>(n_labels), -1);
    struct Acc {
        int min_x = std::numeric_limits<int>::max();
        int min_y = std::numeric_limits<int>::max();
        int max_x = std::numeric_limits<int>::min();
        int max_y = std::numeric_limits<int>::min();
        int area = 0;
    };
    std::vector<Acc> accs;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int raw = label_at(x, y);
            if (raw == 0) continue;
            int root = root_of[raw];
            int& idx = root_index[root];
            if (idx < 0) {
                idx = static_cast<int>(accs.size());
                accs.emplace_back();
            }
            Acc& a = accs[idx];
            a.min_x = std::min(a.min_x, x);
            a.min_y = std::min(a.min_y, y);
            a.max_x = std::max(a.max_x, x);
            a.max_y = std::max(a.max_y, y);
            ++a.area;
        }
    }

    out.reserve(accs.size());
    for (const Acc& a : accs) {
        Component c;
        c.bounds = SpriteRect{a.min_x, a.min_y, a.max_x - a.min_x + 1, a.max_y - a.min_y + 1};
        c.area = a.area;
        out.push_back(c);
    }

    // 排序：先按 y 再按 x，输出稳定可预期
    std::sort(out.begin(), out.end(), [](const Component& a, const Component& b) {
        if (a.bounds.y != b.bounds.y) return a.bounds.y < b.bounds.y;
        return a.bounds.x < b.bounds.x;
    });
    return out;
}

}  // namespace sps
