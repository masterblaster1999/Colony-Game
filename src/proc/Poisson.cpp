// src/proc/Poisson.cpp
#include "Poisson.h"

#include <random>
#include <array>
#include <algorithm> // std::min / std::max
#include <cmath>     // std::cos, std::sin, std::sqrt, std::ceil
#include <vector>

using std::min;
using std::max;

std::vector<P2> Poisson2D(float W, float H, float r, uint32_t seed, int k)
{
    std::mt19937 rng(seed);
    const float cell = r / std::sqrt(2.f);
    const int gw = static_cast<int>(std::ceil(W / cell));
    const int gh = static_cast<int>(std::ceil(H / cell));

    std::vector<int> grid(gw * gh, -1);
    std::vector<P2> samples; samples.reserve(4096);
    std::vector<int> active;

    auto gridAt = [&](int gx, int gy) -> int& { return grid[gy * gw + gx]; };

    auto fits = [&](const P2& p)
    {
        if (p.x < 0 || p.y < 0 || p.x >= W || p.y >= H) return false;
        const int gx = static_cast<int>(p.x / cell);
        const int gy = static_cast<int>(p.y / cell);

        for (int y = max(0, gy - 2); y <= min(gh - 1, gy + 2); ++y)
        for (int x = max(0, gx - 2); x <= min(gw - 1, gx + 2); ++x)
        {
            int i = gridAt(x, y);
            if (i < 0) continue;
            if (d2(samples[i], p) < r * r) return false;
        }
        return true;
    };

    std::uniform_real_distribution<float> Ux(0, W), Uy(0, H);
    std::uniform_real_distribution<float> Ua(0, 6.28318f);     // [0, 2π)
    std::uniform_real_distribution<float> Ur(r, 2 * r);        // [r, 2r)

    // Seed
    P2 s{ Ux(rng), Uy(rng) };
    samples.push_back(s);
    gridAt(int(s.x / cell), int(s.y / cell)) = 0;
    active.push_back(0);

    while (!active.empty())
    {
        std::uniform_int_distribution<int> Ui(0, static_cast<int>(active.size()) - 1);
        const int idx = Ui(rng);
        const P2 base = samples[active[idx]];

        bool found = false;
        for (int t = 0; t < k; ++t)
        {
            const float a = Ua(rng);
            const float rr = Ur(rng);
            const P2 c{ base.x + std::cos(a) * rr, base.y + std::sin(a) * rr };

            if (fits(c))
            {
                samples.push_back(c);
                gridAt(int(c.x / cell), int(c.y / cell)) = static_cast<int>(samples.size()) - 1;
                active.push_back(static_cast<int>(samples.size()) - 1);
                found = true;
                break;
            }
        }

        if (!found)
        {
            active[idx] = active.back();
            active.pop_back();
        }
    }

    return samples;
}
