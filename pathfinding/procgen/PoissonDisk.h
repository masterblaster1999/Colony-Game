// pathfinding/procgen/PoissonDisk.h
#pragma once
#include <vector>
#include <random>
#include <cmath>
#include <limits>
#include <utility>

namespace colony::pathfinding::procgen {

struct Int2 { int x, y; };

inline float frand(std::mt19937& rng) { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }

// Bridson's fast Poisson disk sampling in 2D. Returns points in [0,w) x [0,h).
// See Bridson SIGGRAPH '07. :contentReference[oaicite:6]{index=6}
inline std::vector<Int2> poissonDisk(int w, int h, float minDist, int maxAttempts, uint32_t seed)
{
    if (w <= 0 || h <= 0 || minDist <= 0.0f) return {};
    std::mt19937 rng(seed);

    const float cellSize = minDist / std::sqrt(2.0f);
    const int gw = std::max(1, static_cast<int>(std::ceil(w / cellSize)));
    const int gh = std::max(1, static_cast<int>(std::ceil(h / cellSize)));

    auto gridIdx = [gw](int gx, int gy) { return gy * gw + gx; };
    std::vector<int> grid(static_cast<size_t>(gw) * gh, -1);
    std::vector<Int2> points;
    std::vector<Int2> active;

    auto inBounds = [w, h](float fx, float fy) { return fx >= 0.0f && fy >= 0.0f && fx < w && fy < h; };

    // Initial point
    {
        float fx = frand(rng) * (w - 1);
        float fy = frand(rng) * (h - 1);
        Int2 p{ static_cast<int>(fx), static_cast<int>(fy) };
        points.push_back(p);
        active.push_back(p);
        int gx = static_cast<int>(fx / cellSize);
        int gy = static_cast<int>(fy / cellSize);
        grid[gridIdx(gx, gy)] = 0;
    }

    auto farEnough = [&](float fx, float fy) -> bool {
        int gx = static_cast<int>(fx / cellSize);
        int gy = static_cast<int>(fy / cellSize);
        for (int oy = -2; oy <= 2; ++oy)
            for (int ox = -2; ox <= 2; ++ox) {
                int ngx = gx + ox, ngy = gy + oy;
                if (ngx < 0 || ngy < 0 || ngx >= gw || ngy >= gh) continue;
                int idx = grid[gridIdx(ngx, ngy)];
                if (idx >= 0) {
                    float dx = fx - points[static_cast<size_t>(idx)].x;
                    float dy = fy - points[static_cast<size_t>(idx)].y;
                    if ((dx * dx + dy * dy) < (minDist * minDist)) return false;
                }
            }
        return true;
    };

    std::uniform_real_distribution<float> ang(0.0f, 6.28318530718f);
    for (size_t i = 0; i < active.size(); ++i) {
        Int2 base = active[i];
        bool added = false;
        for (int k = 0; k < maxAttempts; ++k) {
            float r = minDist * (1.0f + frand(rng)); // sample in annulus [r, 2r)
            float theta = ang(rng);
            float fx = base.x + std::cos(theta) * r;
            float fy = base.y + std::sin(theta) * r;
            if (!inBounds(fx, fy)) continue;
            if (!farEnough(fx, fy)) continue;
            Int2 p{ static_cast<int>(fx), static_cast<int>(fy) };
            points.push_back(p);
            active.push_back(p);
            int gx = static_cast<int>(fx / cellSize);
            int gy = static_cast<int>(fy / cellSize);
            grid[gridIdx(gx, gy)] = static_cast<int>(points.size()) - 1;
            added = true;
        }
        if (!added) {
            // retire this active point
            active[i] = active.back();
            active.pop_back();
            --i;
        }
    }
    return points;
}

} // namespace colony::pathfinding::procgen
