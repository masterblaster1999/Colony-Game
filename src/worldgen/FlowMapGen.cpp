// src/worldgen/FlowMapGen.cpp

#include "FlowMapGen.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

#ifdef max
  #undef max
#endif
#ifdef min
  #undef min
#endif

namespace cg
{
    static inline void normalize2(float& x, float& y) noexcept
    {
        const float len = std::sqrt(x * x + y * y);
        if (len > 0.0f)
        {
            x /= len;
            y /= len;
        }
        else
        {
            x = 0.0f;
            y = 0.0f;
        }
    }

    FlowMap buildFlowMapFromD8(const std::vector<std::uint8_t>& dir,
                               const std::vector<float>& acc,
                               int W,
                               int H)
    {
        FlowMap F{};
        F.w = W;
        F.h = H;

        if (W <= 0 || H <= 0)
            return F;

        const std::size_t count = static_cast<std::size_t>(W) * static_cast<std::size_t>(H);
        F.rgba.assign(count * 4u, 255u);

        if (dir.size() < count || acc.size() < count)
            return F;

        // Function-scope D8 tables (unity-build safe; no namespace-scope collisions)
        static constexpr int kDX8[8] = { +1, +1,  0, -1, -1, -1,  0, +1 };
        static constexpr int kDY8[8] = {  0, -1, -1, -1,  0, +1, +1, +1 };

        // Normalize accumulation to [0,1] (log-ish mapping helps)
        float aMin = std::numeric_limits<float>::infinity();
        float aMax = 0.0f;

        for (std::size_t i = 0; i < count; ++i)
        {
            const float v = acc[i];
            if (v > 0.0f)
            {
                aMin = std::min(aMin, v);
                aMax = std::max(aMax, v);
            }
        }

        if (!std::isfinite(aMin) || !(aMax > aMin))
        {
            aMin = 0.0f;
            aMax = 1.0f;
        }

        const float invRange = (aMax > aMin) ? (1.0f / (aMax - aMin)) : 1.0f;

        auto pack = [&](int x, int y, float dx, float dy, float s)
        {
            // encode dir from [-1..1] to [0..255]
            const float rx = (dx * 0.5f + 0.5f) * 255.0f;
            const float ry = (dy * 0.5f + 0.5f) * 255.0f;
            const float bz = s * 255.0f;

            const std::uint8_t R = static_cast<std::uint8_t>(
                std::lround(std::clamp(rx, 0.0f, 255.0f)));
            const std::uint8_t G = static_cast<std::uint8_t>(
                std::lround(std::clamp(ry, 0.0f, 255.0f)));
            const std::uint8_t B = static_cast<std::uint8_t>(
                std::lround(std::clamp(bz, 0.0f, 255.0f)));

            const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(W) +
                                   static_cast<std::size_t>(x)) * 4u;

            F.rgba[i + 0] = R;
            F.rgba[i + 1] = G;
            F.rgba[i + 2] = B;
            F.rgba[i + 3] = 255u;
        };

        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(W) +
                                      static_cast<std::size_t>(x);

                const std::uint8_t k = dir[i];

                float dx = 0.0f;
                float dy = 0.0f;

                if (k != 255u && k < 8u)
                {
                    dx = static_cast<float>(kDX8[k]);
                    dy = static_cast<float>(kDY8[k]);
                    normalize2(dx, dy);
                }

                float s = 0.0f;
                if (acc[i] > 0.0f)
                {
                    s = std::clamp((acc[i] - aMin) * invRange, 0.0f, 1.0f);

                    // Optional log tone-map for better dynamic range in visualization
                    s = std::clamp(std::log2(1.0f + 15.0f * s) / 4.0f, 0.0f, 1.0f);
                }

                pack(x, y, dx, dy, s);
            }
        }

        return F;
    }

} // namespace cg
