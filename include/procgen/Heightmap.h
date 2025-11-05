#pragma once
#include <vector>
#include <algorithm>
#include <cassert>
#include "Types.h"

namespace procgen {

struct Heightmap {
    int width = 0, height = 0;
    std::vector<float> data; // row-major, [0..1] recommended

    Heightmap() = default;
    Heightmap(int w, int h, float fill = 0.0f) : width(w), height(h), data(w*h, fill) {}

    inline int idx(int x, int y) const { return y * width + x; }
    inline float& at(int x, int y) { return data[idx(x,y)]; }
    inline float  at(int x, int y) const { return data[idx(x,y)]; }

    float sample_clamped(int x, int y) const {
        x = std::max(0, std::min(width  - 1, x));
        y = std::max(0, std::min(height - 1, y));
        return at(x,y);
    }

    void normalize() {
        if (data.empty()) return;
        auto [mnIt, mxIt] = std::minmax_element(data.begin(), data.end());
        float mn = *mnIt, mx = *mxIt;
        float d = (mx - mn);
        if (d <= 1e-9f) { std::fill(data.begin(), data.end(), 0.0f); return; }
        for (float &v : data) v = (v - mn) / d;
    }
};

} // namespace procgen
