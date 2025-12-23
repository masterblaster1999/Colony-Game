#pragma once
// ProceduralGraph.hpp — declarations only. Implementation is in ProceduralGraphRun.cpp
// Keeps /WX happy by avoiding unused internal definitions in non-caller TUs.

#include <vector>
#include <cstdint>
#include <cstddef>

namespace pg {

// ---------------- data containers ----------------
struct Map2D {
    int w = 0, h = 0;
    std::vector<float> v; // row-major

    Map2D() = default;
    Map2D(int W, int H, float fill = 0.f) : w(W), h(H), v(static_cast<size_t>(W) * H, fill) {}

    inline float& at(int x, int y)             { return v[static_cast<size_t>(y) * w + x]; }
    inline float  at(int x, int y) const       { return v[static_cast<size_t>(y) * w + x]; }
};

struct U8Map {
    int w = 0, h = 0;
    std::vector<std::uint8_t> v;

    U8Map() = default;
    U8Map(int W, int H, std::uint8_t fill = 0) : w(W), h(H), v(static_cast<size_t>(W) * H, fill) {}

    inline std::uint8_t& at(int x, int y)      { return v[static_cast<size_t>(y) * w + x]; }
    inline std::uint8_t  at(int x, int y) const{ return v[static_cast<size_t>(y) * w + x]; }
};

struct Vec2 { float x{}, y{}; };

// ---------------- params & outputs -------------
enum class Biome : std::uint8_t {
    Ocean=0, Beach, Desert, Savanna, Grassland, Shrubland,
    TemperateForest, BorealForest, TropicalForest, Tundra, Bare
};

struct Params {
    int      width  = 512;
    int      height = 512;
    std::uint32_t seed = 1337u;

    // fBM noise
    int   octaves     = 6;
    float base_freq   = 2.0f / 512.0f;
    float lacunarity  = 2.0f;
    float gain        = 0.5f;

    // domain warp
    float warp_amp_px = 30.0f;
    float warp_freq   = 1.0f / 128.0f;
    int   warp_oct    = 4;

    // height mapping
    float height_scale = 80.0f;
    float sea_level    = 0.40f;

    // thermal erosion
    int   thermal_iters    = 30;
    float talus            = 0.8f;
    float thermal_strength = 0.5f;

    // rivers
    float river_threshold = 200.0f;
    float river_depth     = 4.0f;

    // moisture/biomes
    float moisture_freq = 1.0f / 256.0f;
    int   moisture_oct  = 5;

    // scattering (trees/rocks)
    float scatter_radius = 8.0f;

    // --- Optional: improved hydrology (lakes) + moisture-from-water ---
    bool  enable_depression_fill     = true;
    float fill_epsilon               = 0.01f;  // world units; prevents tiny float fills
    int   lake_min_area              = 40;     // cells (connected component size)
    float lake_min_depth             = 0.75f;  // world units of fill to classify as lake

    bool  moisture_from_water        = true;
    float moisture_water_strength    = 0.35f;  // blend factor [0..1]
    float moisture_water_radius      = 64.0f;  // in cells (distance falloff)
    bool  moisture_include_ocean     = false;  // if true, coasts get very wet

    // --- Optional: terrain stamps (craters/volcanoes/etc.) ---
    bool  enable_stamps              = false;

    int   crater_count               = 0;
    float crater_radius_min          = 10.0f;  // cells
    float crater_radius_max          = 35.0f;  // cells
    float crater_depth               = 8.0f;   // world units
    float crater_rim_height          = 2.5f;   // world units

    int   volcano_count              = 0;
    float volcano_radius_min         = 15.0f;  // cells
    float volcano_radius_max         = 50.0f;  // cells
    float volcano_height             = 18.0f;  // world units
    float volcano_crater_ratio       = 0.22f;  // crater radius = ratio * volcano radius

    float stamp_min_spacing          = 0.80f;  // multiplier for (rA + rB) center separation
};

struct Instance { float x{}, y{}; std::uint8_t kind{}; };

// Water classification for optional hydrology overlays.
enum class WaterKind : std::uint8_t { Land=0, Ocean=1, River=2, Lake=3 };

// Optional terrain stamp metadata (craters/volcanoes/etc.)
struct Stamp {
    float x{}, y{};      // center in grid coordinates
    float radius{};      // radius in cells
    float strength{};    // height delta scale (interpret by type)
    std::uint8_t type{}; // 0=crater, 1=volcano
};

struct Outputs {
    Map2D  height;
    Map2D  flow;
    Map2D  moisture;
    Map2D  temperature;
    U8Map  biomes;
    U8Map  water;          // WaterKind per cell (optional overlay)
    std::vector<Vec2> trees;
    std::vector<Stamp> stamps;
};

struct GraphResult { Outputs out; };

// --------------- public API (declaration only) ---------------
Outputs run_procedural_graph(const Params& P);

} // namespace pg
