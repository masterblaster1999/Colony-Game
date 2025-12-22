// src/worldgen/DomainWarp.hpp
#pragma once
//
// Public API for domain-warped terrain generation plus useful utilities.
//
// Build-time toggles (set via compiler defs/CMake if desired; implemented in .cpp):
// CG_DW_ENABLE_OPENMP (default 1) : enable OpenMP row parallelism when available
// CG_DW_ENABLE_THREADS (default 1) : fallback to std::thread parallel-for
// CG_DW_AA_SAMPLES (default 1) : supersampling for band-limiting (1, 4, or 8)
// CG_DW_WARP_OF_WARP (default 1) : small warp-of-warp for extra variation
// CG_DW_CURL_BLEND (default 0) : 0..1, blend curl-noise into the warp field

#include "HeightField.hpp"

#include <cstdint>
#include <vector>

namespace cg
{
    // Axis-aligned min/max of a heightfield (useful for auto-scaling, debug)
    struct MinMax
    {
        float minv{};
        float maxv{};

        constexpr MinMax() = default;
        constexpr MinMax(float min_val, float max_val) : minv(min_val), maxv(max_val) {}
    };

    // Simple normal (X right, Y up, Z forward)
    struct Nrm
    {
        float x{};
        float y{};
        float z{};

        constexpr Nrm() = default;
        constexpr Nrm(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    };

    // Parameters controlling domain-warped terrain generation.
    struct DomainWarpParams
    {
        // Final height fBm (terrain detail)
        float baseFrequency  = 1.0f / 256.0f; // cycles per domain unit
        int   baseOctaves    = 6;
        float baseLacunarity = 2.0f;
        float baseGain       = 0.5f;

        // Warp field (domain distortion)
        float warpFrequency  = 1.0f / 128.0f;
        int   warpOctaves    = 3;
        float warpLacunarity = 2.0f;
        float warpGain       = 0.5f;
        float warpStrength   = 25.0f; // in domain units (try 5..50)

        // Post scaling / bias in world units
        float heightScale = 80.0f;
        float heightBias  = 0.0f;

        // Reproducibility
        std::uint32_t seed = 1337u;

        // Optional “ridged” look for the base fractal
        bool ridged = false;
    };

    // -----------------------------
    // Core generation API
    // -----------------------------

    // Generate a domain-warped heightfield of size (width x height) using 'p'.
    HeightField generateDomainWarpHeight(int width, int height, const DomainWarpParams& p);

    // Tileable version: heightfield that repeats every (periodX, periodY) in domain cells.
    HeightField generateDomainWarpHeightTiled(int width,
                                              int height,
                                              const DomainWarpParams& p,
                                              int periodX,
                                              int periodY);

    // -----------------------------
    // Utility maps
    // -----------------------------

    // Compute slope (radians) from a heightfield using central differences.
    // xyScale: world units per texel in X/Z.
    // zScale: vertical scale multiplier used when building the mesh.
    std::vector<float> computeSlopeMap(const HeightField& hf, float xyScale, float zScale);

    // Compute per-texel normals from the heightfield (central differences, normalized).
    std::vector<Nrm> computeNormalMap(const HeightField& hf, float xyScale, float zScale);

    // Scan min/max height values.
    MinMax scanMinMax(const HeightField& hf);

} // namespace cg
