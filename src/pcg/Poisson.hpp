#pragma once
#include <vector>
#include <cmath>
#include <limits>
#include "SeededRng.hpp"

namespace pcg {

struct Vec2 { float x, y; };

inline float sqr(float v){return v*v;}

inline std::vector<Vec2> poisson_disk(float width, float height, float r, int k, Rng& rng) {
    // Bridson's algorithm in 2D
    const float cell = r / std::sqrt(2.0f);
    int gridW = int(std::ceil(width / cell));
    int gridH = int(std::ceil(height / cell));
    std::vector<int> grid(gridW * gridH, -1);
    std::vector<Vec2> pts, active;

    auto grid_idx = [&](float x, float y) {
        return std::pair<int,int>(int(x / cell), int(y / cell));
    };
    auto in_bounds = [&](float x,float y){ return x>=0 && y>=0 && x<width && y<height; };
    auto fits = [&](const Vec2& p) {
        auto [gi, gj] = grid_idx(p.x, p.y);
        for (int j = std::max(0, gj-2); j <= std::min(gridH-1, gj+2); ++j) {
            for (int i = std::max(0, gi-2); i <= std::min(gridW-1, gi+2); ++i) {
                int idx = grid[j*gridW + i];
                if (idx >= 0) {
                    Vec2 q = pts[idx];
                    if (sqr(q.x - p.x) + sqr(q.y - p.y) < r*r) return false;
                }
            }
        }
        return true;
    };

    // seed
    Vec2 p0{ rng.rangef(0, width), rng.rangef(0, height) };
    pts.push_back(p0); active.push_back(p0);
    auto [gi0, gj0] = grid_idx(p0.x, p0.y);
    grid[gj0*gridW + gi0] = 0;

    while (!active.empty()) {
        int idx = rng.rangei(0, (int)active.size()-1);
        Vec2 p = active[idx]; bool found = false;
        for (int n=0; n<k; ++n) {
            float ang = rng.rangef(0.0f, 6.2831853f);
            float rad = r * (1.0f + rng.rangef(0.0f, 1.0f));
            Vec2 cand{ p.x + std::cos(ang)*rad, p.y + std::sin(ang)*rad };
            if (in_bounds(cand.x, cand.y) && fits(cand)) {
                pts.push_back(cand);
                active.push_back(cand);
                auto [gi, gj] = grid_idx(cand.x, cand.y);
                grid[gj*gridW + gi] = (int)pts.size()-1;
                found = true; break;
            }
        }
        if (!found) { active[idx] = active.back(); active.pop_back(); }
    }
    return pts;
}

} // namespace pcg
