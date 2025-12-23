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


// ---------------- settlement / roads layer ----------------
// Produces: start site, settlements, road network, fertility/resources masks.
bool  enable_settlement_layer = true;

// start site scoring (0..1 factors combined)
int   site_sample_step      = 4;     // evaluate score every N cells (speed/quality)
int   top_site_candidates   = 2048;  // keep best N scored samples
float water_preferred_dist  = 10.0f; // cells; "too close" penalty inside this
float water_max_dist        = 96.0f; // cells; beyond this water gives no benefit
float max_slope_for_sites   = 0.55f; // normalized; above this score is heavily penalized

// score weights
float w_water    = 1.40f;
float w_slope    = 1.20f;
float w_biome    = 1.00f;
float w_resource = 0.85f;
float w_flood    = 1.10f;

// settlements (Poisson-ish dart throwing from the scored candidates)
int   settlements_min       = 3;
int   settlements_max       = 8;
float settlement_min_dist   = 120.0f; // cells
float settlement_score_cut  = 0.55f;  // normalized [0..1]
int   settlement_try_budget = 60000;

// roads
bool  build_roads       = true;
float road_base_cost    = 1.0f;
float road_slope_cost   = 22.0f;   // slope weight (normalized slope)
float road_river_penalty= 30.0f;   // crossing rivers is allowed but discouraged
float road_ocean_penalty= 1.0e9f;  // effectively blocked
float road_biome_penalty= 4.0f;    // generic "unpleasant biome" penalty
int   road_max_expansions = 900000;

// farmland / forest stamping
bool  stamp_farmland = true;
float farmland_radius = 26.0f;          // cells from freshwater
std::uint8_t farmland_min_fertility = 145; // 0..255

bool  stamp_forest = true;
std::uint8_t forest_min_moisture = 165; // 0..255
};

struct Instance { float x{}, y{}; std::uint8_t kind{}; };



struct SettlementSite {
    Vec2  pos{};
    float score      = 0.0f; // 0..1
    float water_dist = 0.0f; // cells
    float slope_n    = 0.0f; // normalized 0..1
    float fertility  = 0.0f; // 0..1
    std::uint8_t biome = 0;
};

struct RoadSegment {
    Vec2 a{};
    Vec2 b{};
    std::uint8_t kind = 0; // 0=dirt/track (debug)
};

struct Outputs {
    Map2D  height;
    Map2D  flow;
    Map2D  moisture;
    Map2D  temperature;
    U8Map  biomes;
    std::vector<Vec2> trees;


    // ----- settlement / roads layer -----
    SettlementSite start{};
    std::vector<SettlementSite> settlements;
    std::vector<RoadSegment>   roads;

    // debug masks (0..255)
    U8Map fertility;   // fertility score
    U8Map farmland;    // farmland stamp
    U8Map forest;      // forest stamp
    U8Map road_mask;   // rasterized roads

    // simple resource presence masks (0..255)
    U8Map res_wood;
    U8Map res_stone;
    U8Map res_ore;
};

struct GraphResult { Outputs out; };

// --------------- public API (declaration only) ---------------
Outputs run_procedural_graph(const Params& P);

} // namespace pg
