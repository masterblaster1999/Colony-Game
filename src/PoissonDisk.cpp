#include "PoissonDisk.h"
#include <cmath>
#include <limits>

struct GridCell { int index = -1; };

static inline float Rand01(std::mt19937& rng)
{
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
}

std::vector<Float2> PoissonSample(
    const PoissonParams& p,
    std::mt19937& rng,
    const std::function<bool(float, float)>& accept)
{
    const float cellSize = p.minDist / std::sqrt(2.0f);
    const int gw = int(std::ceil(p.width / cellSize));
    const int gh = int(std::ceil(p.height / cellSize));

    auto gridIndex = [&](float x, float y) -> std::pair<int,int>
    {
        int gx = int(x / cellSize);
        int gy = int(y / cellSize);
        return { gx, gy };
    };

    std::vector<GridCell> grid(size_t(gw) * gh);
    std::vector<Float2> samples;
    std::vector<int> active;

    // Initial sample
    Float2 first{ Rand01(rng) * p.width, Rand01(rng) * p.height };
    if (!accept || accept(first.x, first.y))
    {
        samples.push_back(first);
        active.push_back(0);
        auto [gx, gy] = gridIndex(first.x, first.y);
        grid[size_t(gy) * gw + gx].index = 0;
    }

    while (!active.empty())
    {
        std::uniform_int_distribution<int> pick(0, int(active.size()) - 1);
        int a = pick(rng);
        Float2 s = samples[active[a]];
        bool found = false;

        for (int i = 0; i < p.k; ++i)
        {
            float ang = Rand01(rng) * 6.28318530718f;
            float rad = p.minDist * (1.0f + Rand01(rng)); // [r, 2r)
            Float2 c{ s.x + std::cos(ang) * rad, s.y + std::sin(ang) * rad };

            if (c.x < 0 || c.y < 0 || c.x >= p.width || c.y >= p.height)
                continue;

            if (accept && !accept(c.x, c.y))
                continue;

            auto [gx, gy] = gridIndex(c.x, c.y);
            bool ok = true;
            for (int oy = -2; oy <= 2 && ok; ++oy)
            {
                for (int ox = -2; ox <= 2 && ok; ++ox)
                {
                    int nx = gx + ox, ny = gy + oy;
                    if (nx < 0 || ny < 0 || nx >= gw || ny >= gh) continue;
                    int idx = grid[size_t(ny) * gw + nx].index;
                    if (idx >= 0)
                    {
                        Float2 q = samples[size_t(idx)];
                        float dx = q.x - c.x;
                        float dy = q.y - c.y;
                        if ((dx*dx + dy*dy) < (p.minDist * p.minDist))
                            ok = false;
                    }
                }
            }

            if (ok)
            {
                int newIndex = int(samples.size());
                samples.push_back(c);
                active.push_back(newIndex);
                grid[size_t(gy) * gw + gx].index = newIndex;
                found = true;
                break;
            }
        }

        if (!found)
        {
            active[a] = active.back();
            active.pop_back();
        }
    }

    return samples;
}
