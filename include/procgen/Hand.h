#pragma once
// ============================================================================
// Hand.h
// HAND = Height Above Nearest Drainage (approx) in *meters*.
//
// Why this is useful in THIS repo:
// - procgen::HydrologyResult already provides D8 flow directions and a water
//   classification (Sea/River/Lake) from your heightfield.
// - worldgen::SettlementSitingGenerator optionally accepts `hand_m`
//   ("meters above nearest drainage") and applies a flood penalty.
// - This header computes that `hand_m` layer from HydrologyResult + height.
//   Zero extra .cpp needed.
//
// Notes / assumptions:
// - Your heightfield is typically normalized ~[0..1]. We convert height deltas
//   to meters via `metersPerHeightUnit` (tune to match your game's scale).
// - We define "nearest drainage" along the flow path: follow flowDir until a
//   drainage tile (Sea/River/Lake) or a sink/outflow break.
// - If a cell drains into a closed basin (sink), we treat the sink as drainage
//   by default (HAND = 0 at the sink).
//
// Typical usage (inside world gen):
//   #include "procgen/Hydrology.h"
//   #include "procgen/Hand.h"
//
//   procgen::HydrologySettings hs;
//   auto H = procgen::GenerateHydrology(w, h, height01, hs);
//
//   procgen::HandParams hp;
//   hp.metersPerHeightUnit = 250.0f; // tune!
//   auto hand_m = procgen::computeHAND_meters(H, height01, hp);
//
//   // Then pass &hand_m into worldgen::GenerateSettlementSites(..., hand_m=&hand_m)
// ============================================================================

#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <limits>

#include "Hydrology.h"
#include "Types.h"

namespace procgen {

struct HandParams {
    // Convert "height units" to meters. If your height is 0..1, then
    // metersPerHeightUnit ~ total relief of your world in meters.
    float metersPerHeightUnit = 200.0f;

    // Which water kinds count as "drainage" (HAND=0 at these tiles).
    bool seaIsDrainage   = true;
    bool riverIsDrainage = true;
    bool lakeIsDrainage  = true;

    // If a tile is a sink/no-outflow (flowDir == -1) AND not classified as water,
    // should we treat it as a drainage endpoint (HAND=0 there)?
    bool sinksAreDrainage = true;

    // Optional clamp for numerical safety / visualization. <= 0 disables.
    float clampMaxMeters = 0.0f;

    // Optional smoothing (3x3 box blur iterations). 0 disables.
    // If enabled, drainage tiles are forced back to 0 each iteration.
    int smoothIters = 0;
};

namespace hand_detail {

inline int idx(int x, int y, int w) { return y * w + x; }

inline bool inb(int x, int y, int w, int h) {
    return (x >= 0 && y >= 0 && x < w && y < h);
}

// Must match Hydrology.h direction order:
// E, NE, N, NW, W, SW, S, SE
static constexpr int DX[8] = {  1,  1,  0, -1, -1, -1,  0,  1 };
static constexpr int DY[8] = {  0, -1, -1, -1,  0,  1,  1,  1 };

inline float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

inline bool isDrainageKind(WaterKind k, const HandParams& p) {
    switch (k) {
        case WaterKind::Sea:   return p.seaIsDrainage;
        case WaterKind::River: return p.riverIsDrainage;
        case WaterKind::Lake:  return p.lakeIsDrainage;
        default:               return false;
    }
}

inline int nextIndexFromFlowDir(int i, int w, int h, std::int8_t dir) {
    if (dir < 0 || dir > 7) return -1;
    const int x = i % w;
    const int y = i / w;
    const int nx = x + DX[(int)dir];
    const int ny = y + DY[(int)dir];
    if (!inb(nx, ny, w, h)) return -1;
    return idx(nx, ny, w);
}

inline void boxBlur3x3(std::vector<float>& dst, const std::vector<float>& src, int w, int h) {
    dst.assign((size_t)w * (size_t)h, 0.0f);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float sum = 0.0f;
            int cnt = 0;
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    const int nx = x + ox;
                    const int ny = y + oy;
                    if (!inb(nx, ny, w, h)) continue;
                    sum += src[(size_t)idx(nx, ny, w)];
                    ++cnt;
                }
            }
            dst[(size_t)idx(x, y, w)] = (cnt > 0) ? (sum / (float)cnt) : src[(size_t)idx(x, y, w)];
        }
    }
}

} // namespace hand_detail

// -----------------------------------------------------------------------------
// computeHAND_meters
// Returns: hand_m (size W*H). 0 at drainage tiles and sinks (by default).
// -----------------------------------------------------------------------------
template <class HeightContainer>
inline std::vector<float> computeHAND_meters(
    const HydrologyResult& H,
    const HeightContainer& height,
    const HandParams& p = {})
{
    std::vector<float> out;
    const int w = H.w;
    const int h = H.h;
    if (w <= 0 || h <= 0) return out;

    const size_t N = (size_t)w * (size_t)h;
    if (height.size() != N) return out;
    if (H.flowDir.size() != N) return out;
    if (H.water.size() != N) return out;

    // Precompute a drainage mask once.
    std::vector<std::uint8_t> drainageMask(N, 0u);
    for (size_t i = 0; i < N; ++i) {
        const auto wk = H.water[i];
        const bool isDrain = hand_detail::isDrainageKind(wk, p);
        drainageMask[i] = isDrain ? 1u : 0u;
    }

    // Root drainage index along flow path (path-compressed).
    std::vector<int> root(N, -1);
    std::vector<std::uint32_t> seen(N, 0u);
    std::uint32_t tag = 1u;

    std::vector<int> path;
    path.reserve(64);

    auto isDrainageIdx = [&](int i) -> bool {
        if (i < 0) return false;
        const size_t ui = (size_t)i;
        if (drainageMask[ui]) return true;

        // If the hydrology says "sea tile", treat as drainage if enabled.
        if (H.flowDir[ui] == (std::int8_t)-2) return p.seaIsDrainage;

        // Sinks/no-outflow: optional drainage endpoints
        if (H.flowDir[ui] == (std::int8_t)-1) return p.sinksAreDrainage;

        return false;
    };

    auto nextIdx = [&](int i) -> int {
        const std::int8_t d = H.flowDir[(size_t)i];
        return hand_detail::nextIndexFromFlowDir(i, w, h, d);
    };

    for (size_t startU = 0; startU < N; ++startU) {
        if (root[startU] != -1) continue;

        path.clear();
        int cur = (int)startU;

        int drain = cur;

        // Walk until we hit known root, drainage, or break.
        for (size_t steps = 0; steps < N; ++steps) {
            const int rcur = root[(size_t)cur];
            if (rcur != -1) { drain = rcur; break; }

            if (isDrainageIdx(cur)) { drain = cur; break; }

            // Cycle protection (shouldn't happen if hydrology is acyclic).
            if (seen[(size_t)cur] == tag) { drain = cur; break; }
            seen[(size_t)cur] = tag;

            path.push_back(cur);

            const int nxt = nextIdx(cur);
            if (nxt < 0) { drain = cur; break; }

            cur = nxt;
        }

        // Assign roots for visited path cells.
        for (int v : path) root[(size_t)v] = drain;
        root[(size_t)drain] = drain;

        // Advance tag; handle wrap (extremely unlikely).
        ++tag;
        if (tag == 0u) {
            std::fill(seen.begin(), seen.end(), 0u);
            tag = 1u;
        }
    }

    // Compute HAND in meters: (height - height_at_drainage) * scale
    out.assign(N, 0.0f);
    for (size_t i = 0; i < N; ++i) {
        const int r = (root[i] >= 0) ? root[i] : (int)i;
        const float hi = (float)height[i];
        const float hr = (float)height[(size_t)r];
        float dH = hi - hr;
        if (dH < 0.0f) dH = 0.0f;

        float hand_m = dH * std::max(0.0f, p.metersPerHeightUnit);

        if (p.clampMaxMeters > 0.0f) {
            hand_m = std::min(hand_m, p.clampMaxMeters);
        }

        // Drainage tiles should be 0 by definition.
        if (drainageMask[i]) hand_m = 0.0f;

        out[i] = hand_m;
    }

    // Optional smoothing (keep drains pinned to 0).
    if (p.smoothIters > 0) {
        std::vector<float> tmp;
        for (int it = 0; it < p.smoothIters; ++it) {
            hand_detail::boxBlur3x3(tmp, out, w, h);
            out.swap(tmp);
            for (size_t i = 0; i < N; ++i) {
                if (drainageMask[i]) out[i] = 0.0f;
                if (p.clampMaxMeters > 0.0f) out[i] = std::min(out[i], p.clampMaxMeters);
                if (out[i] < 0.0f) out[i] = 0.0f;
            }
        }
    }

    return out;
}

// Convenience overload for procgen::WorldData (uses w.height).
inline std::vector<float> computeHAND_meters(
    const HydrologyResult& H,
    const WorldData& w,
    HandParams p = {})
{
    return computeHAND_meters(H, w.height, p);
}

// Optional helper: convert HAND to flood risk in [0..1].
// fullFlood_m: HAND <= 0 => risk 1, HAND >= fullFlood_m => risk 0.
inline std::vector<float> computeFloodRisk01(
    const std::vector<float>& hand_m,
    float fullFlood_m = 3.0f)
{
    std::vector<float> risk;
    if (hand_m.empty()) return risk;
    risk.resize(hand_m.size(), 0.0f);

    const float denom = std::max(1e-6f, fullFlood_m);
    for (size_t i = 0; i < hand_m.size(); ++i) {
        const float t = 1.0f - (hand_m[i] / denom);
        risk[i] = hand_detail::clamp01(t);
    }
    return risk;
}

// Optional helper: grayscale preview for HAND (RGBA8).
// visMax_m maps to white; 0 maps to black.
inline std::vector<std::uint8_t> makeHANDPreviewRGBA(
    const std::vector<float>& hand_m,
    int w, int h,
    float visMax_m = 20.0f)
{
    std::vector<std::uint8_t> img;
    if (w <= 0 || h <= 0) return img;
    const size_t N = (size_t)w * (size_t)h;
    if (hand_m.size() != N) return img;

    img.resize(N * 4);
    const float denom = std::max(1e-6f, visMax_m);
    for (size_t i = 0; i < N; ++i) {
        const float v01 = hand_detail::clamp01(hand_m[i] / denom);
        const std::uint8_t c = (std::uint8_t)std::lround(v01 * 255.0f);
        img[i * 4 + 0] = c;
        img[i * 4 + 1] = c;
        img[i * 4 + 2] = c;
        img[i * 4 + 3] = 255;
    }
    return img;
}

} // namespace procgen
