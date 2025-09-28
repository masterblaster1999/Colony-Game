#include "FlowMapGen.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>

namespace cg
{

// -----------------------------------------------------------------------------
// CPU-side replacements for HLSL conveniences used previously in this file.
// -----------------------------------------------------------------------------
struct float2
{
    float x, y;
    constexpr float2() : x(0.0f), y(0.0f) {}
    constexpr float2(float _x, float _y) : x(_x), y(_y) {}
};

inline float2 normalize(const float2& v) noexcept
{
    const float len = std::sqrt(v.x * v.x + v.y * v.y);
    if (len > 0.0f) return { v.x / len, v.y / len };
    return { 0.0f, 0.0f };
}

// -----------------------------------------------------------------------------

static const int DX8[8] = { +1,+1, 0,-1,-1,-1, 0,+1 };
static const int DY8[8] = {  0,-1,-1,-1, 0,+1,+1,+1 };

FlowMap buildFlowMapFromD8(const std::vector<uint8_t>& dir,
                           const std::vector<float>&   acc,
                           int W, int H)
{
    FlowMap F;
    F.w = W;
    F.h = H;
    F.rgba.assign(static_cast<size_t>(W) * static_cast<size_t>(H) * 4u, 255u);

    // Normalize accumulation to [0,1] (log-ish mapping helps)
    float aMin = 1e30f, aMax = 0.0f;
    for (float v : acc)
    {
        if (v > 0.0f)
        {
            aMin = std::min(aMin, v);
            aMax = std::max(aMax, v);
        }
    }
    if (!(aMax > aMin)) { aMin = 0.0f; aMax = 1.0f; }
    const float invRange = (aMax > aMin) ? 1.0f / (aMax - aMin) : 1.0f;

    auto pack = [&](int x, int y, float2 d, float s)
    {
        const float rx = (d.x * 0.5f + 0.5f) * 255.0f;
        const float ry = (d.y * 0.5f + 0.5f) * 255.0f;
        const float bz = s * 255.0f;

        const uint8_t R = static_cast<uint8_t>(std::lround(std::clamp(rx, 0.0f, 255.0f)));
        const uint8_t G = static_cast<uint8_t>(std::lround(std::clamp(ry, 0.0f, 255.0f)));
        const uint8_t B = static_cast<uint8_t>(std::lround(std::clamp(bz, 0.0f, 255.0f)));

        const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x)) * 4u;
        F.rgba[i + 0] = R;
        F.rgba[i + 1] = G;
        F.rgba[i + 2] = B;
        F.rgba[i + 3] = 255u;
    };

    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            const int i = y * W + x;
            const uint8_t k = dir[static_cast<size_t>(i)];

            float2 d(0.0f, 0.0f);
            if (k != 255u)
            {
                d = normalize(float2(static_cast<float>(DX8[k]), static_cast<float>(DY8[k])));
            }

            const float a = acc[static_cast<size_t>(i)];
            float s = (a > 0.0f) ? std::clamp((a - aMin) * invRange, 0.0f, 1.0f) : 0.0f;

            // Optional log tone-map for better dynamic range in visualization
            s = std::clamp(static_cast<float>(std::log2(1.0f + 15.0f * s) / 4.0f), 0.0f, 1.0f);

            pack(x, y, d, s);
        }
    }

    return F;
}

} // namespace cg
