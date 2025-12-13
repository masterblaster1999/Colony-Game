#pragma once
// ============================================================================
// RiverOrder.h
// Build a normalized "river order / flow importance" field (0..1 per cell)
// from procgen::HydrologyResult.
//
// Why useful in THIS repo:
// - HydrologyResult already provides:
//     * water classification (Sea/River/Lake)
//     * per-cell flow accumulation
//     * per-cell river width class
//   so you can derive a stable "river importance" scalar without more sim.
// - worldgen::RoadNetworkGenerator accepts an optional river_order01 layer
//   and adds it into A* cost (bigger rivers -> higher crossing cost).
// - Your current WorldGen.cpp wiring passes river_order01 as nullptr; this
//   file provides a drop-in layer you can pass instead.
//
// Header-only. No new .cpp required.
// ============================================================================

#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <limits>

#include "Hydrology.h" // procgen::HydrologyResult, procgen::WaterKind

namespace procgen {

struct RiverOrderParams {
    enum class Mode : std::uint8_t {
        // Use HydrologyResult::riverWidth (1..maxWidth) for rivers.
        // Fast, stable, and matches "visual width" pretty well.
        FromWidth = 0,

        // Use HydrologyResult::accumulation (linear normalize).
        FromAccumulation = 1,

        // Use log1p(accumulation) normalize (compresses the huge range).
        FromAccumulationLog = 2,
    };

    Mode mode = Mode::FromWidth;

    // Which water classes receive non-zero order.
    // Defaults: rivers only (best match for "river crossing" difficulty).
    bool includeRivers = true;
    bool includeLakes  = false;
    bool includeSea    = false;

    // If true, normalization (max) is computed only over included tiles.
    // If false, normalization uses all non-sea tiles (land + lakes + rivers).
    bool normalizeUsingIncludedTilesOnly = true;

    // When rivers are included, ensure tiny streams still have some cost.
    // (Keeps roads from treating small rivers as "free" compared to land.)
    float minOrder01 = 0.05f;

    // Optional: spread some "river influence" onto nearby tiles (banks).
    // Useful if you want roads to prefer a few clear crossing points rather
    // than hugging riverbanks everywhere.
    //
    // Implementation: N iterations of 8-neighbor max-propagation with decay.
    // Radius=0 disables.
    int bankDilateRadius = 0;   // 0..8 is typical
    float bankFalloff    = 0.6f; // 0..1 (per-step multiplier)
};

// Small local clamp to avoid relying on other headers' helpers.
namespace riverorder_detail {
    inline float clamp01(float v) {
        return (v < 0.f) ? 0.f : (v > 1.f ? 1.f : v);
    }

    inline bool is_included(WaterKind k, const RiverOrderParams& p) {
        switch (k) {
            case WaterKind::River: return p.includeRivers;
            case WaterKind::Lake:  return p.includeLakes;
            case WaterKind::Sea:   return p.includeSea;
            default:               return false;
        }
    }

    inline float default_nonriver_value(WaterKind k) {
        // Only used if you explicitly include lakes/sea.
        // Roads already have a separate water penalty via water_mask, so
        // these are meant as "extra importance" (e.g., big lakes / open sea).
        switch (k) {
            case WaterKind::Lake: return 0.6f;
            case WaterKind::Sea:  return 1.0f;
            default:              return 0.0f;
        }
    }
} // namespace riverorder_detail

// -----------------------------------------------------------------------------
// makeWaterMaskFromHydrology
// Convenience helper: 1 = water, 0 = land, using HydrologyResult::water.
// (Many worldgen utilities take a binary water mask.)
// -----------------------------------------------------------------------------
inline std::vector<std::uint8_t> makeWaterMaskFromHydrology(
    const HydrologyResult& H,
    bool includeSea   = true,
    bool includeRivers= true,
    bool includeLakes = true)
{
    std::vector<std::uint8_t> mask;
    if (H.w <= 0 || H.h <= 0) return mask;
    const size_t N = static_cast<size_t>(H.w) * static_cast<size_t>(H.h);
    if (H.water.size() != N) return mask;

    mask.assign(N, 0u);
    for (size_t i = 0; i < N; ++i) {
        const WaterKind k = H.water[i];
        bool isWater = false;
        if (k == WaterKind::Sea  && includeSea)    isWater = true;
        if (k == WaterKind::River&& includeRivers) isWater = true;
        if (k == WaterKind::Lake && includeLakes)  isWater = true;
        mask[i] = isWater ? 1u : 0u;
    }
    return mask;
}

// -----------------------------------------------------------------------------
// makeRiverOrder01
// Output: vector<float> size W*H
//   0.0 for non-included tiles, otherwise ~[0..1] importance.
// Intended to be passed as river_order01 into worldgen road/connectors.
// -----------------------------------------------------------------------------
inline std::vector<float> makeRiverOrder01(const HydrologyResult& H, RiverOrderParams p = {}) {
    std::vector<float> out;
    if (H.w <= 0 || H.h <= 0) return out;

    const size_t N = static_cast<size_t>(H.w) * static_cast<size_t>(H.h);
    if (H.water.size()        != N) return out;
    if (H.accumulation.size() != N) return out;
    if (H.riverWidth.size()   != N) return out;

    out.assign(N, 0.0f);

    // 1) Establish normalization max (depends on mode).
    float maxV = 0.0f;

    if (p.mode == RiverOrderParams::Mode::FromWidth) {
        int maxW = 0;

        // Compute max width among included river tiles (or all rivers if lakes/sea included).
        for (size_t i = 0; i < N; ++i) {
            if (!riverorder_detail::is_included(H.water[i], p)) continue;
            if (H.water[i] != WaterKind::River) continue;
            maxW = std::max(maxW, static_cast<int>(H.riverWidth[i]));
        }
        maxW = std::max(maxW, 1);

        for (size_t i = 0; i < N; ++i) {
            const WaterKind k = H.water[i];
            if (!riverorder_detail::is_included(k, p)) continue;

            float v = 0.0f;
            if (k == WaterKind::River) {
                const int wClass = std::max(0, static_cast<int>(H.riverWidth[i]));
                if (wClass <= 0) {
                    v = 0.0f;
                } else if (maxW <= 1) {
                    v = 1.0f;
                } else {
                    v = static_cast<float>(wClass - 1) / static_cast<float>(maxW - 1);
                }
                v = riverorder_detail::clamp01(v);
                if (v > 0.0f) v = std::max(v, riverorder_detail::clamp01(p.minOrder01));
            } else {
                // If you choose to include lakes/sea, give them a fixed baseline.
                v = riverorder_detail::default_nonriver_value(k);
            }

            out[i] = v;
            maxV = std::max(maxV, v);
        }

        // If everything is 0 (e.g., you disabled rivers), leave out as all zeros.
    } else {
        // Accumulation modes: compute max over chosen normalization set.
        auto include_for_norm = [&](size_t i) -> bool {
            if (p.normalizeUsingIncludedTilesOnly) {
                return riverorder_detail::is_included(H.water[i], p);
            }
            // else normalize using all non-sea tiles (land + lakes + rivers).
            return H.water[i] != WaterKind::Sea;
        };

        for (size_t i = 0; i < N; ++i) {
            if (!include_for_norm(i)) continue;
            float a = H.accumulation[i];
            if (!(a > 0.0f)) continue; // handles 0/NaN safely
            if (p.mode == RiverOrderParams::Mode::FromAccumulationLog) {
                a = static_cast<float>(std::log1p(static_cast<double>(a)));
            }
            maxV = std::max(maxV, a);
        }
        maxV = std::max(maxV, 1e-6f);

        for (size_t i = 0; i < N; ++i) {
            const WaterKind k = H.water[i];
            if (!riverorder_detail::is_included(k, p)) continue;

            float v = 0.0f;
            if (k == WaterKind::River || k == WaterKind::Lake || k == WaterKind::Sea) {
                float a = H.accumulation[i];
                if (p.mode == RiverOrderParams::Mode::FromAccumulationLog) {
                    a = static_cast<float>(std::log1p(static_cast<double>(std::max(0.0f, a))));
                } else {
                    a = std::max(0.0f, a);
                }
                v = riverorder_detail::clamp01(a / maxV);
                if (k == WaterKind::River && v > 0.0f) {
                    v = std::max(v, riverorder_detail::clamp01(p.minOrder01));
                }
                // If you include lakes/sea, this makes their value proportional
                // to accumulation (often lower than major rivers); if you want
                // them to be "always big", prefer Mode::FromWidth and includeSea.
            }
            out[i] = v;
        }
    }

    // 2) Optional bank dilation (spread influence onto neighbors with decay).
    if (p.bankDilateRadius > 0) {
        const int W = H.w;
        const int Hh = H.h;
        const float fall = riverorder_detail::clamp01(p.bankFalloff);

        // 8-neighborhood propagation.
        static constexpr int DX[8] = { 1, 1, 0,-1,-1,-1, 0, 1 };
        static constexpr int DY[8] = { 0,-1,-1,-1, 0, 1, 1, 1 };

        std::vector<float> cur = out;
        std::vector<float> nxt = out;

        for (int it = 0; it < p.bankDilateRadius; ++it) {
            nxt = cur; // start from current values
            for (int y = 0; y < Hh; ++y) {
                for (int x = 0; x < W; ++x) {
                    const size_t i = static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x);
                    const float v = cur[i];
                    if (v <= 0.0f) continue;

                    const float spread = v * fall;
                    if (spread <= 0.0f) continue;

                    for (int k = 0; k < 8; ++k) {
                        const int nx = x + DX[k];
                        const int ny = y + DY[k];
                        if (nx < 0 || ny < 0 || nx >= W || ny >= Hh) continue;
                        const size_t j = static_cast<size_t>(ny) * static_cast<size_t>(W) + static_cast<size_t>(nx);

                        // Spread onto land primarily (don’t amplify water crossings).
                        if (H.water[j] == WaterKind::Sea || H.water[j] == WaterKind::River || H.water[j] == WaterKind::Lake) {
                            continue;
                        }
                        nxt[j] = std::max(nxt[j], spread);
                    }
                }
            }
            cur.swap(nxt);
        }

        // Merge dilated land influence back into the base river field.
        // Keep original river cells unchanged (so rivers stay at their true order).
        for (size_t i = 0; i < N; ++i) {
            if (H.water[i] == WaterKind::River) continue;
            out[i] = std::max(out[i], cur[i]);
            out[i] = riverorder_detail::clamp01(out[i]);
        }
    }

    return out;
}

} // namespace procgen
