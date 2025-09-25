// src/worldgen/PoissonDisk.h
#pragma once
#include <vector>
#include <functional>
#include <cstdint>
#include <random>
#include <optional>

namespace colony::worldgen {

struct Rect { float x0, y0, x1, y1; };
struct Sample { float x, y; };

// Optional hooks allow slope/biome/wetness-aware scatters.
// - density(x,y): 0..1 multiplier (higher => denser => smaller radius)
// - mask(x,y): return false to forbid placement (e.g., deep water)
struct PDSettings {
    Rect bounds{0,0,1,1};
    float radius = 1.0f;          // base Poisson radius (world units)
    int   k = 30;                 // attempts per active sample (Bridson)
    uint64_t seed = 0;            // deterministic seed
    std::function<float(float,float)> density; // optional
    std::function<bool(float,float)>  mask;    // optional
};

// Uniform Poisson-disk (Bridson 2007) with optional variable density.
// Variable density uses an "effective local radius" r_local = radius / sqrt(max(d, eps))
// and enforces min distance = min(r_local(p), r_local(q)) between points.
// This is a pragmatic game-friendly variant (see also recent variable-density work).
std::vector<Sample> PoissonDisk(const PDSettings& s);

} // namespace colony::worldgen
