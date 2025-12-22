// src/worldgen/Hydrology.cpp

#include "Hydrology.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

// If any translation unit pulled in Windows.h without NOMINMAX,
// these macros can break std::min/std::max usage.
#ifdef max
  #undef max
#endif
#ifdef min
  #undef min
#endif

namespace cg
{
namespace
{
    inline bool dimsMatch(const HeightField& a, const HeightField& b) noexcept
    {
        return a.w == b.w && a.h == b.h;
    }

    inline std::size_t idx(int x, int y, int w) noexcept
    {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
    }

    HeightField makeField(int w, int h, float init)
    {
        HeightField out;
        out.w = w;
        out.h = h;
        out.data.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), init);
        return out;
    }

    float minValue(const HeightField& f)
    {
        if (f.data.empty())
            return 0.0f;
        return *std::min_element(f.data.begin(), f.data.end());
    }

    float maxValue(const HeightField& f)
    {
        if (f.data.empty())
            return 0.0f;
        return *std::max_element(f.data.begin(), f.data.end());
    }

    // Priority-flood depression fill (makes the surface drainage-consistent).
    HeightField fillDepressions(const HeightField& elev)
    {
        const int w = elev.w;
        const int h = elev.h;

        HeightField filled = makeField(w, h, 0.0f);
        if (w <= 0 || h <= 0 || elev.data.size() != static_cast<std::size_t>(w) * static_cast<std::size_t>(h))
            return filled;

        struct Node
        {
            float height = 0.0f;
            int   i      = 0;
        };

        struct Cmp
        {
            bool operator()(const Node& a, const Node& b) const noexcept
            {
                // min-heap by height
                return a.height > b.height;
            }
        };

        std::priority_queue<Node, std::vector<Node>, Cmp> pq;
        std::vector<std::uint8_t> visited(elev.data.size(), 0);

        auto push_cell = [&](int x, int y)
        {
            const int i = static_cast<int>(idx(x, y, w));
            if (visited[static_cast<std::size_t>(i)])
                return;

            visited[static_cast<std::size_t>(i)] = 1;
            const float e = elev.data[static_cast<std::size_t>(i)];
            filled.data[static_cast<std::size_t>(i)] = e;
            pq.push(Node{e, i});
        };

        // Seed borders
        for (int x = 0; x < w; ++x)
        {
            push_cell(x, 0);
            push_cell(x, h - 1);
        }
        for (int y = 0; y < h; ++y)
        {
            push_cell(0, y);
            push_cell(w - 1, y);
        }

        static constexpr int dx4[4] = { 1, -1, 0, 0 };
        static constexpr int dy4[4] = { 0, 0, 1, -1 };

        while (!pq.empty())
        {
            const Node n = pq.top();
            pq.pop();

            const int cx = n.i % w;
            const int cy = n.i / w;

            for (int k = 0; k < 4; ++k)
            {
                const int nx = cx + dx4[k];
                const int ny = cy + dy4[k];

                if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                    continue;

                const int j = static_cast<int>(idx(nx, ny, w));
                if (visited[static_cast<std::size_t>(j)])
                    continue;

                visited[static_cast<std::size_t>(j)] = 1;

                const float orig = elev.data[static_cast<std::size_t>(j)];
                const float outH = std::max(orig, n.height);

                filled.data[static_cast<std::size_t>(j)] = outH;
                pq.push(Node{outH, j});
            }
        }

        return filled;
    }

    // D8 flow accumulation (rainfall added downstream).
    HeightField flowAccumulationD8(const HeightField& filled, const HeightField& rain)
    {
        const int w = filled.w;
        const int h = filled.h;

        HeightField accum = makeField(w, h, 0.0f);

        const std::size_t N = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
        if (w <= 0 || h <= 0 || filled.data.size() != N || rain.data.size() != N)
            return accum;

        // Downstream target for each cell (index into flat array); -1 means sink/outlet.
        std::vector<int> down(N, -1);

        // Local constants only (unity-build safe).
        static constexpr int dx8[8] = { 1,  1,  0, -1, -1, -1,  0,  1 };
        static constexpr int dy8[8] = { 0,  1,  1,  1,  0, -1, -1, -1 };

        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                const std::size_t i = idx(x, y, w);
                const float h0 = filled.data[i];

                float bestH = h0;
                int bestJ = -1;

                for (int k = 0; k < 8; ++k)
                {
                    const int nx = x + dx8[k];
                    const int ny = y + dy8[k];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                        continue;

                    const std::size_t j = idx(nx, ny, w);
                    const float hj = filled.data[j];

                    // Strictly downhill to avoid cycles on flats.
                    if (hj < bestH)
                    {
                        bestH = hj;
                        bestJ = static_cast<int>(j);
                    }
                }

                down[i] = bestJ;
            }
        }

        // Sort cells by height descending so flow pushes downhill.
        std::vector<int> order(N);
        for (std::size_t i = 0; i < N; ++i)
            order[i] = static_cast<int>(i);

        std::sort(order.begin(), order.end(),
                  [&](int a, int b)
                  {
                      return filled.data[static_cast<std::size_t>(a)] > filled.data[static_cast<std::size_t>(b)];
                  });

        // Start with rainfall at each cell.
        accum.data = rain.data;

        for (int i : order)
        {
            const int d = down[static_cast<std::size_t>(i)];
            if (d >= 0)
            {
                accum.data[static_cast<std::size_t>(d)] += accum.data[static_cast<std::size_t>(i)];
            }
        }

        return accum;
    }

} // anonymous namespace

HydroOutputs simulateHydrology(const HeightField& elev,
                              const HeightField& precip,
                              int iterations)
{
    HydroOutputs out{};

    // Basic validation
    if (!dimsMatch(elev, precip) || elev.w <= 0 || elev.h <= 0)
        return out;

    const std::size_t N = static_cast<std::size_t>(elev.w) * static_cast<std::size_t>(elev.h);
    if (elev.data.size() != N || precip.data.size() != N)
        return out;

    // Keep these directly.
    out.precip = precip;

    // Temperature heuristic: cooler at higher elevation.
    out.temperature = makeField(elev.w, elev.h, 0.0f);
    {
        const float mn = minValue(elev);
        const float mx = maxValue(elev);
        const float denom = (mx > mn) ? (mx - mn) : 1.0f;

        for (std::size_t i = 0; i < N; ++i)
        {
            const float t = (elev.data[i] - mn) / denom;   // 0..1
            out.temperature.data[i] = 1.0f - t;            // 1..0
        }
    }

    // Depression fill -> drainage-consistent surface.
    out.filled = fillDepressions(elev);

    // D8 accumulation.
    out.flowAccum = flowAccumulationD8(out.filled, precip);

    // Simple carving: subtract a small amount where flow is large.
    out.carved = out.filled;
    {
        const float maxA = maxValue(out.flowAccum);
        const float denom = (maxA > 0.0f) ? std::log1p(maxA) : 1.0f;

        // Scale carving by iterations (kept bounded).
        // FIX (/WX C4244): make the int->float conversion explicit.
        const float it = static_cast<float>(std::max(0, iterations));
        const float carveScale = 0.02f * std::min(it / 50.0f, 4.0f);

        for (std::size_t i = 0; i < N; ++i)
        {
            const float a = out.flowAccum.data[i];
            const float n = (a > 0.0f) ? (std::log1p(a) / denom) : 0.0f; // 0..1
            out.carved.data[i] = out.filled.data[i] - n * carveScale;
        }
    }

    // Water level (placeholder but useful): here we just reuse filled.
    // You can later add a proper lake/river water surface solve.
    out.waterLevel = out.filled;

    return out;
}

HeightField distanceToCoast(const HeightField& landmask)
{
    const int w = landmask.w;
    const int h = landmask.h;

    HeightField out = makeField(w, h, 0.0f);
    if (w <= 0 || h <= 0)
        return out;

    const std::size_t N = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    if (landmask.data.size() != N)
        return out;

    auto isLand = [&](std::size_t i) -> bool
    {
        return landmask.data[i] > 0.5f;
    };

    static constexpr int dx4[4] = { 1, -1, 0, 0 };
    static constexpr int dy4[4] = { 0, 0, 1, -1 };

    std::vector<int> dist(N, -1);
    std::queue<int> q;

    // Coast: land cell adjacent to any water (or boundary treated as water).
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const std::size_t i = idx(x, y, w);
            if (!isLand(i))
                continue;

            bool coast = false;
            for (int k = 0; k < 4; ++k)
            {
                const int nx = x + dx4[k];
                const int ny = y + dy4[k];

                if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                {
                    coast = true;
                    break;
                }

                const std::size_t j = idx(nx, ny, w);
                if (!isLand(j))
                {
                    coast = true;
                    break;
                }
            }

            if (coast)
            {
                dist[i] = 0;
                q.push(static_cast<int>(i));
            }
        }
    }

    // BFS into the interior (land only).
    while (!q.empty())
    {
        const int i = q.front();
        q.pop();

        const int cx = i % w;
        const int cy = i / w;

        for (int k = 0; k < 4; ++k)
        {
            const int nx = cx + dx4[k];
            const int ny = cy + dy4[k];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                continue;

            const std::size_t j = idx(nx, ny, w);
            if (!isLand(j))
                continue;

            if (dist[j] != -1)
                continue;

            dist[j] = dist[static_cast<std::size_t>(i)] + 1;
            q.push(static_cast<int>(j));
        }
    }

    for (std::size_t i = 0; i < N; ++i)
        out.data[i] = (dist[i] >= 0) ? static_cast<float>(dist[i]) : 0.0f;

    return out;
}

} // namespace cg
