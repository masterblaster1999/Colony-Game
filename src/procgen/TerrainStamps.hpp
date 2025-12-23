#pragma once
// TerrainStamps.hpp
// -----------------------------------------------------------------------------
// Small, deterministic "stamp" system for adding large-scale landmarks
// (craters + volcanoes) to an existing heightfield.
//
// Why stamps?
//   - Very cheap compared to full simulation (erosion/climate)
//   - Produces recognisable landmarks that help navigation + storytelling
//   - Easy to parameterize + toggle per world seed
//
// This header is written to be a drop-in add-on for the existing
// pg::ProceduralGraph pipeline.
// -----------------------------------------------------------------------------

#include "procgen/ProceduralGraph.hpp"

#include <random>
#include <vector>
#include <cmath>
#include <algorithm>

namespace pg::stamps {

struct StampParams {
    bool  enable = false;
    std::uint32_t seed = 1u;

    // Craters
    int   crater_count      = 0;
    float crater_radius_min = 10.0f; // cells
    float crater_radius_max = 35.0f; // cells
    float crater_depth      = 8.0f;  // world units
    float crater_rim_height = 2.5f;  // world units

    // Volcanoes
    int   volcano_count       = 0;
    float volcano_radius_min  = 15.0f; // cells
    float volcano_radius_max  = 50.0f; // cells
    float volcano_height      = 18.0f; // world units
    float volcano_crater_ratio= 0.22f; // crater radius = ratio * volcano radius

    // Placement
    float min_spacing = 0.80f; // multiplier for (rA + rB) center separation

    // Safety
    int   attempts_per_stamp = 48;
};

static inline float clampf(float v, float lo, float hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline float sqr(float v) { return v * v; }

static inline bool try_place_stamp(std::vector<Stamp>& out,
                                  int W, int H,
                                  float r,
                                  std::uint8_t type,
                                  float strength,
                                  float minSpacing,
                                  std::mt19937& rng,
                                  int attempts)
{
    if (W <= 0 || H <= 0) return false;

    // Keep stamp fully inside map.
    const float pad = r + 2.0f;
    if (pad * 2.0f >= (float)W || pad * 2.0f >= (float)H) return false;

    std::uniform_real_distribution<float> ux(pad, (float)(W - 1) - pad);
    std::uniform_real_distribution<float> uy(pad, (float)(H - 1) - pad);

    for (int a = 0; a < attempts; ++a) {
        float x = ux(rng);
        float y = uy(rng);

        bool ok = true;
        for (const auto& s : out) {
            const float dx = s.x - x;
            const float dy = s.y - y;
            const float minD = (s.radius + r) * minSpacing;
            if (dx * dx + dy * dy < minD * minD) { ok = false; break; }
        }
        if (!ok) continue;

        out.push_back(Stamp{ x, y, r, strength, type });
        return true;
    }

    return false;
}

// Generate a set of stamps with non-overlap constraints.
static inline std::vector<Stamp> generate(int W, int H, const StampParams& P) {
    std::vector<Stamp> S;
    if (!P.enable) return S;

    std::mt19937 rng((unsigned)P.seed);

    auto urand = [&](float a, float b) {
        std::uniform_real_distribution<float> u(a, b);
        return u(rng);
    };

    // Craters
    for (int i = 0; i < P.crater_count; ++i) {
        float r = urand(P.crater_radius_min, P.crater_radius_max);
        (void)try_place_stamp(S, W, H, r, /*type*/0u, P.crater_depth, P.min_spacing, rng, P.attempts_per_stamp);
    }

    // Volcanoes
    for (int i = 0; i < P.volcano_count; ++i) {
        float r = urand(P.volcano_radius_min, P.volcano_radius_max);
        (void)try_place_stamp(S, W, H, r, /*type*/1u, P.volcano_height, P.min_spacing, rng, P.attempts_per_stamp);
    }

    return S;
}

// Apply stamps into the height field in-place.
// Notes:
//   - Craters: bowl depression + rim ring.
//   - Volcanoes: cone + summit crater.
static inline void apply(Map2D& H, const std::vector<Stamp>& stamps, const StampParams& P) {
    if (stamps.empty()) return;

    const int W = H.w;
    const int Hh = H.h;

    for (const auto& s : stamps) {
        const int x0 = (int)std::floor(s.x - s.radius - 2.0f);
        const int x1 = (int)std::ceil (s.x + s.radius + 2.0f);
        const int y0 = (int)std::floor(s.y - s.radius - 2.0f);
        const int y1 = (int)std::ceil (s.y + s.radius + 2.0f);

        const float r = std::max(1.0f, s.radius);
        const float invR = 1.0f / r;

        // Rim width as a fraction of radius (kept modest).
        const float rimSigma = 0.12f; // tuned for a nice ring

        // Summit crater for volcano
        const float volcCraterR = std::max(2.0f, r * clampf(P.volcano_crater_ratio, 0.05f, 0.6f));
        const float invVolcCraterR = 1.0f / volcCraterR;

        for (int y = y0; y <= y1; ++y) {
            if ((unsigned)y >= (unsigned)Hh) continue;
            for (int x = x0; x <= x1; ++x) {
                if ((unsigned)x >= (unsigned)W) continue;

                const float dx = (x + 0.5f) - s.x;
                const float dy = (y + 0.5f) - s.y;
                const float d  = std::sqrt(dx * dx + dy * dy);

                if (d > r + 2.0f) continue;

                const float t = d * invR; // 0..1 within radius
                float delta = 0.0f;

                if (s.type == 0u) {
                    // --- Crater ---
                    if (t <= 1.0f) {
                        const float bowl = -P.crater_depth * sqr(1.0f - t); // smooth bowl
                        const float rim  =  P.crater_rim_height * std::exp(-sqr((t - 1.0f) / rimSigma));
                        delta = bowl + rim;
                    }
                } else {
                    // --- Volcano ---
                    if (t <= 1.0f) {
                        // Simple cone (could replace with a more complex profile)
                        float cone = P.volcano_height * (1.0f - t);

                        // Summit crater (reuse crater_depth as a fraction of volcano height)
                        const float craterDepth = std::min(P.crater_depth, P.volcano_height * 0.45f);
                        if (d <= volcCraterR) {
                            const float ct = d * invVolcCraterR;
                            // depression + tiny rim
                            const float bowl = -craterDepth * sqr(1.0f - ct);
                            const float rim  =  (0.35f * craterDepth) * std::exp(-sqr((ct - 1.0f) / 0.22f));
                            cone += bowl + rim;
                        }

                        delta = cone;
                    }
                }

                if (delta != 0.0f) {
                    float& h = H.at(x, y);
                    h = std::max(0.0f, h + delta);
                }
            }
        }
    }
}

} // namespace pg::stamps
