// src/worldgen/PoissonDisk.cpp
#include "PoissonDisk.h"
#include <cmath>
#include <array>

namespace colony::worldgen {

static inline float sqr(float v){ return v*v; }

std::vector<Sample> PoissonDisk(const PDSettings& s)
{
    const float TWO_PI = 6.283185307179586f;
    const float eps = 1e-6f;

    const float x0 = s.bounds.x0, y0 = s.bounds.y0;
    const float x1 = s.bounds.x1, y1 = s.bounds.y1;
    const float W  = x1 - x0,     H  = y1 - y0;

    if (W <= 0 || H <= 0 || s.radius <= 0) return {};

    // Bridson grid cell size (r/sqrt(2)) — works for uniform radius.
    // For variable density we still use base radius for grid; local checks guard exact distance.
    const float cell = s.radius * 0.70710678f;
    const int gx = std::max(1, int(std::ceil(W / cell)));
    const int gy = std::max(1, int(std::ceil(H / cell)));

    std::vector<int> grid(gx * gy, -1);
    std::vector<Sample> points;
    points.reserve(size_t((W*H)/(s.radius*s.radius)));

    std::vector<int> active;
    active.reserve(1024);

    std::mt19937_64 rng(s.seed);
    std::uniform_real_distribution<float> urand(0.0f, 1.0f);

    auto localRadius = [&](float x, float y) -> float {
        float d = 1.0f;
        if (s.density) {
            d = std::max(eps, std::min(1.0f, s.density(x,y)));
        }
        // higher density => smaller radius
        return s.radius / std::sqrt(d);
    };

    auto fits = [&](const Sample& p) -> bool {
        if (p.x < x0 || p.x >= x1 || p.y < y0 || p.y >= y1) return false;
        if (s.mask && !s.mask(p.x, p.y)) return false;

        const float rl = localRadius(p.x, p.y);
        const int gx0 = std::max(0, int((p.x - rl - x0)/cell));
        const int gy0 = std::max(0, int((p.y - rl - y0)/cell));
        const int gx1 = std::min(gx-1, int((p.x + rl - x0)/cell));
        const int gy1 = std::min(gy-1, int((p.y + rl - y0)/cell));

        for (int yy = gy0; yy <= gy1; ++yy) {
            for (int xx = gx0; xx <= gx1; ++xx) {
                int idx = grid[yy*gx + xx];
                if (idx < 0) continue;
                const Sample& q = points[size_t(idx)];
                const float rq = localRadius(q.x, q.y);
                const float mind = std::min(rl, rq);
                if (sqr(p.x - q.x) + sqr(p.y - q.y) < sqr(mind)) {
                    return false;
                }
            }
        }
        return true;
    };

    auto gridInsert = [&](const Sample& p, int id) {
        int gx_i = std::min(gx-1, std::max(0, int((p.x - x0)/cell)));
        int gy_i = std::min(gy-1, std::max(0, int((p.y - y0)/cell)));
        grid[gy_i*gx + gx_i] = id;
    };

    auto genAround = [&](const Sample& c) -> std::optional<Sample> {
        const float rl = localRadius(c.x, c.y);
        for (int i = 0; i < s.k; ++i) {
            // random angle, radius in [rl, 2*rl)
            float ang = urand(rng) * TWO_PI;
            float rad = rl * (1.0f + urand(rng));
            Sample p{ c.x + rad * std::cos(ang), c.y + rad * std::sin(ang) };
            if (fits(p)) return p;
        }
        return std::nullopt;
    };

    // Initial point
    Sample p0{ x0 + urand(rng)*W, y0 + urand(rng)*H };
    if (s.mask) {
        int guard = 4096;
        while (!s.mask(p0.x, p0.y) && guard--) {
            p0 = Sample{ x0 + urand(rng)*W, y0 + urand(rng)*H };
        }
        if (guard <= 0) return points;
    }
    if (!fits(p0)) {
        // fall back to uniform random until we find a fit
        int guard = 4096;
        do {
            p0 = Sample{ x0 + urand(rng)*W, y0 + urand(rng)*H };
        } while (!fits(p0) && --guard > 0);
        if (guard <= 0) return points;
    }

    points.push_back(p0);
    gridInsert(p0, 0);
    active.push_back(0);

    // Bridson main loop
    while (!active.empty()) {
        // pick random active index
        std::uniform_int_distribution<size_t> ui(0, active.size()-1);
        size_t aidx = ui(rng);
        Sample c = points[size_t(active[aidx])];

        auto cand = genAround(c);
        if (cand.has_value()) {
            int id = int(points.size());
            points.push_back(*cand);
            gridInsert(*cand, id);
            active.push_back(id);
        } else {
            // retire this active point
            active[aidx] = active.back();
            active.pop_back();
        }
    }

    return points;
}

} // namespace colony::worldgen
