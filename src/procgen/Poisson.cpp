#include "procgen/Poisson.h"
#include <random>
#include <cmath>
#include <limits>

namespace procgen {

struct Cell { int x=-1, y=-1, idx=-1; };

std::vector<Vec2> poissonDisk(int width, int height, float radius, uint32_t seed, int k) {
    const float cellSize = radius / std::sqrt(2.f);
    const int gridW = std::max(1, (int)std::ceil(width  / cellSize));
    const int gridH = std::max(1, (int)std::ceil(height / cellSize));
    std::vector<int> grid(gridW * gridH, -1);

    std::vector<Vec2> points;
    points.reserve((size_t)(width*height/(radius*radius)));

    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> ux(0.0f, (float)width);
    std::uniform_real_distribution<float> uy(0.0f, (float)height);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);

    auto gridIndex = [&](float px, float py) {
        int gx = (int)(px / cellSize);
        int gy = (int)(py / cellSize);
        return (gx < 0 || gy < 0 || gx >= gridW || gy >= gridH) ? -1 : (gy*gridW + gx);
    };

    auto fits = [&](float px, float py) {
        int gx = (int)(px / cellSize);
        int gy = (int)(py / cellSize);
        for (int y = std::max(0, gy-2); y <= std::min(gridH-1, gy+2); ++y)
        for (int x = std::max(0, gx-2); x <= std::min(gridW-1, gx+2); ++x) {
            int idx = grid[y*gridW + x];
            if (idx >= 0) {
                float dx = points[idx].x - px;
                float dy = points[idx].y - py;
                if ((dx*dx + dy*dy) < radius*radius) return false;
            }
        }
        return true;
    };

    // Start with a random point
    Vec2 first{ ux(rng), uy(rng) };
    points.push_back(first);
    int gi = gridIndex(first.x, first.y);
    if (gi >= 0) grid[gi] = 0;

    // Active list
    std::vector<int> active = {0};

    while (!active.empty()) {
        std::uniform_int_distribution<int> uactive(0, (int)active.size() - 1);
        int idx = uactive(rng);
        const Vec2 base = points[active[idx]];
        bool found = false;

        for (int i = 0; i < k; ++i) {
            float ang = 2.0f * 3.14159265f * u01(rng);
            float rad = radius * (1.0f + u01(rng));
            float px = base.x + rad * std::cos(ang);
            float py = base.y + rad * std::sin(ang);
            if (px <= 0 || py <= 0 || px >= width || py >= height) continue;
            if (!fits(px, py)) continue;
            int gidx = gridIndex(px, py);
            points.push_back({px, py});
            if (gidx >= 0) grid[gidx] = (int)points.size() - 1;
            active.push_back((int)points.size() - 1);
            found = true;
            break;
        }
        if (!found) {
            active[idx] = active.back();
            active.pop_back();
        }
    }

    return points;
}

} // namespace procgen

/* References:
   Bridson, R. "Fast Poisson Disk Sampling in Arbitrary Dimensions" (SIGGRAPH 2007).
*/
