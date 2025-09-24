#pragma once
// ProceduralGraph.hpp  —  single-header CPU pipeline for Colony-Game
// Provides: height generation (domain-warped fBM), thermal erosion, D8 flow,
// simple Whittaker-like biome tagging, and Bridson Poisson-disk scattering.
//
// Public API (see bottom): run_procedural_graph(params) -> Outputs
//
// MIT-like; adapt freely for your project.
//
// References (implementation choices):
// - Domain warping + fBM: Inigo Quílez, "Domain warping" & noise articles.
// - D8 flow accumulation: O'Callaghan & Mark (1984) (as implemented widely in GIS).
// - Poisson disk sampling: Bridson (SIGGRAPH 2007).
// - Whittaker biome diagram (temp/precip classification).

#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <random>
#include <queue>
#include <limits>
#include <utility>
#include <tuple>

namespace pg {

// ----------------------- small math helpers -----------------------

static inline float clamp(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
static inline float fade(float t) { return t*t*t*(t*(t*6.0f - 15.0f) + 10.0f); } // Perlin's quintic
static inline int   ifloor(float x) { return (int)std::floor(x); }
static inline float fractf(float x) { return x - std::floor(x); }

// ----------------------- data containers --------------------------

struct Map2D {
    int w = 0, h = 0;
    std::vector<float> v; // row-major
    Map2D() = default;
    Map2D(int W, int H, float fill=0.f) : w(W), h(H), v(size_t(W)*H, fill) {}
    inline float& at(int x, int y)       { return v[size_t(y)*w + x]; }
    inline float  at(int x, int y) const { return v[size_t(y)*w + x]; }
};

struct U8Map {
    int w = 0, h = 0;
    std::vector<uint8_t> v;
    U8Map() = default;
    U8Map(int W, int H, uint8_t fill=0) : w(W), h(H), v(size_t(W)*H, fill) {}
    inline uint8_t& at(int x, int y)       { return v[size_t(y)*w + x]; }
    inline uint8_t  at(int x, int y) const { return v[size_t(y)*w + x]; }
};

struct Vec2 { float x, y; };

// ----------------------- params & outputs -------------------------

enum class Biome : uint8_t {
    Ocean=0, Beach, Desert, Savanna, Grassland, Shrubland,
    TemperateForest, BorealForest, TropicalForest, Tundra, Bare
};

struct Params {
    int      width  = 512;
    int      height = 512;
    uint32_t seed   = 1337u;

    // fBM noise
    int   octaves     = 6;
    float base_freq   = 2.0f / 512.0f;  // "how many features across the map"
    float lacunarity  = 2.0f;
    float gain        = 0.5f;

    // domain warp (applied to the sampling point before fBM)
    float warp_amp_px = 30.0f;          // in pixels (screen-space convenience)
    float warp_freq   = 1.0f / 128.0f;
    int   warp_oct    = 4;

    // height mapping
    float height_scale = 80.0f;         // vertical exaggeration
    float sea_level    = 0.40f;         // 0..1 before scaling

    // thermal erosion
    int   thermal_iters    = 30;
    float talus            = 0.8f;      // critical slope (height units across 1px)
    float thermal_strength = 0.5f;      // 0..1 per iteration mass fraction

    // rivers
    float river_threshold = 200.0f;     // flow accumulation threshold
    float river_depth     = 4.0f;       // carve factor

    // moisture/biomes
    float moisture_freq = 1.0f / 256.0f;
    int   moisture_oct  = 5;

    // scattering (trees/rocks)
    float scatter_radius = 8.0f;        // Poisson radius in pixels
};

struct Instance { float x, y; uint8_t kind; };

struct Outputs {
    Map2D  height;      // final height in world units (0..height_scale ish)
    Map2D  flow;        // accumulated upstream area (>=1)
    Map2D  moisture;    // 0..1
    Map2D  temperature; // 0..1
    U8Map  biomes;      // Biome enum indices
    std::vector<Vec2> trees;  // Poisson points in forest-y biomes
};

// ----------------------- RNG & hashing ----------------------------

struct RNG {
    std::mt19937 rng;
    explicit RNG(uint32_t seed) : rng(seed) {}
    float uniform(float a, float b) { std::uniform_real_distribution<float> d(a, b); return d(rng); }
    int   randint(int a, int b)     { std::uniform_int_distribution<int> d(a, b); return d(rng); }
};

static inline uint32_t pcg_hash(uint32_t x) {
    // PCG-inspired integer hash
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
static inline uint32_t hash2i(int x, int y, uint32_t seed) {
    uint32_t h = uint32_t(x) * 0x1f123bb5U ^ uint32_t(y) * 0x59c3f5a7U ^ (seed * 0x94d049bbU);
    return pcg_hash(h);
}

// ----------------------- gradient noise (Perlin-like) -------------

static inline float grad_dot(int ix, int iy, uint32_t seed, float fx, float fy) {
    uint32_t h = hash2i(ix, iy, seed);
    // use angle from hash for gradient direction:
    float a = float(h & 0xffff) / 65535.0f * 6.28318530718f;
    float gx = std::cos(a), gy = std::sin(a);
    return gx * fx + gy * fy;
}

static inline float perlin2(float x, float y, uint32_t seed) {
    int x0 = ifloor(x), y0 = ifloor(y);
    float tx = x - float(x0), ty = y - float(y0);

    float n00 = grad_dot(x0,   y0,   seed, tx,     ty);
    float n10 = grad_dot(x0+1, y0,   seed, tx-1.0f,ty);
    float n01 = grad_dot(x0,   y0+1, seed, tx,     ty-1.0f);
    float n11 = grad_dot(x0+1, y0+1, seed, tx-1.0f,ty-1.0f);

    float sx = fade(tx), sy = fade(ty);
    float nx0 = lerp(n00, n10, sx);
    float nx1 = lerp(n01, n11, sx);
    return lerp(nx0, nx1, sy); // roughly in [-sqrt(2)/2 .. +sqrt(2)/2]
}

static inline float fbm2(float x, float y, int octaves, float lac, float gain, uint32_t seed) {
    float amp = 0.5f;
    float freq = 1.0f;
    float sum = 0.0f;
    for (int i=0; i<octaves; ++i) {
        sum += perlin2(x*freq, y*freq, seed + uint32_t(i*131)) * amp;
        freq *= lac;
        amp  *= gain;
    }
    return sum; // typically ~[-1, +1]
}

// ----------------------- domain-warped fBM height -----------------

static Map2D generate_height(const Params& P) {
    Map2D H(P.width, P.height, 0.f);

    // precompute pixel->uv scale so warp_amp_px works in pixel space
    float invW = 1.0f / float(P.width);
    float invH = 1.0f / float(P.height);

    for (int y=0; y<P.height; ++y) {
        for (int x=0; x<P.width; ++x) {
            // base coords (pixels -> normalized)
            float u = float(x);
            float v = float(y);

            // domain warp: two low-freq fbm fields give an XY offset
            float wx = fbm2(u*P.warp_freq, v*P.warp_freq, P.warp_oct, 2.0f, 0.5f, P.seed^0xA5A5A5u);
            float wy = fbm2(u*P.warp_freq+100.0f, v*P.warp_freq-100.0f, P.warp_oct, 2.0f, 0.5f, P.seed^0xC0FFEEu);
            wx *= P.warp_amp_px;
            wy *= P.warp_amp_px;

            // final fBM sample (in "pixels" so base_freq is intuitive)
            float n = fbm2( (u+wx) * P.base_freq, (v+wy) * P.base_freq, P.octaves, P.lacunarity, P.gain, P.seed );

            // optional ridging for mountains
            float rn = 1.0f - std::fabs(n); // ridged
            float blend = 0.6f;             // mix between soft and ridged
            float h01 = clamp( lerp( (n*0.5f+0.5f), rn, blend ), 0.0f, 1.0f );

            // bias sea level
            h01 = (h01 - P.sea_level) / (1.0f - P.sea_level);
            h01 = clamp(h01, 0.0f, 1.0f);

            H.at(x,y) = h01 * P.height_scale;
        }
    }
    return H;
}

// ----------------------- thermal erosion --------------------------
// Simple talus-angle based material redistribution.
// For iter: if height difference to neighbor > talus, move a fraction down.

static void thermal_erosion(Map2D& H, int iters, float talus, float strength) {
    if (iters <= 0) return;
    Map2D delta(H.w, H.h, 0.0f);

    const int dx8[8] = { -1,0,1,-1,1,-1,0,1 };
    const int dy8[8] = { -1,-1,-1,0,0,1,1,1  };

    for (int it=0; it<iters; ++it) {
        std::fill(delta.v.begin(), delta.v.end(), 0.0f);

        for (int y=0; y<H.h; ++y) {
            for (int x=0; x<H.w; ++x) {
                float h = H.at(x,y);
                // find neighbors that exceed talus; split material among them
                float over_sum = 0.0f;
                float over[8]  = {0};
                int   cnt = 0;

                for (int k=0; k<8; ++k) {
                    int nx = x + dx8[k], ny = y + dy8[k];
                    if (nx<0 || ny<0 || nx>=H.w || ny>=H.h) continue;
                    float nh = H.at(nx,ny);
                    float dh = h - nh;
                    if (dh > talus) { over[cnt] = dh - talus; cnt++; over_sum += (dh - talus); }
                }
                if (cnt==0 || over_sum<=0.0f) continue;

                float m = strength * over_sum; // amount to move this step
                float moved = 0.0f;
                int idx = 0;
                for (int k=0; k<8 && idx<cnt; ++k) {
                    int nx = x + dx8[k], ny = y + dy8[k];
                    if (nx<0 || ny<0 || nx>=H.w || ny>=H.h) continue;
                    float nh = H.at(nx,ny);
                    float dh = h - nh;
                    if (dh > talus) {
                        float share = m * ((dh - talus) / over_sum);
                        delta.at(x,y)  -= share;
                        delta.at(nx,ny)+= share;
                        moved += share;
                        idx++;
                    }
                }
            }
        }
        // apply
        for (size_t i=0;i<H.v.size();++i) H.v[i] += delta.v[i];
    }
}

// ----------------------- D8 flow accumulation ---------------------

// For each cell, point to steepest lower neighbor; topologically accumulate.
static Map2D flow_accumulation_D8(const Map2D& H) {
    const int W = H.w, L = H.h;
    Map2D flow(W, L, 1.0f); // each cell contributes 1 unit of rain
    std::vector<int> dir(W*L, -1);

    auto idx = [W](int x,int y){ return y*W + x; };

    const int dx8[8] = { -1,0,1,-1,1,-1,0,1 };
    const int dy8[8] = { -1,-1,-1,0,0,1,1,1  };
    const float dist8[8] = { 1.41421356f,1.0f,1.41421356f,1.0f,1.0f,1.41421356f,1.0f,1.41421356f };

    // flow direction (steepest descent)
    for (int y=0; y<L; ++y) {
        for (int x=0; x<W; ++x) {
            float h = H.at(x,y);
            float bestSlope = 0.0f;
            int best = -1;
            for (int k=0; k<8; ++k) {
                int nx=x+dx8[k], ny=y+dy8[k];
                if (nx<0||ny<0||nx>=W||ny>=L) continue;
                float dh = h - H.at(nx,ny);
                if (dh > 0.0f) {
                    float slope = dh / dist8[k];
                    if (slope > bestSlope) { bestSlope = slope; best = idx(nx,ny); }
                }
            }
            dir[idx(x,y)] = best; // -1 if pit
        }
    }

    // indices sorted by height ascending -> ensure "uphill to downhill" accumulation
    std::vector<int> order(W*L);
    for (int i=0;i<W*L;++i) order[i]=i;
    std::sort(order.begin(), order.end(), [&](int a, int b){ return H.v[a] < H.v[b]; });

    for (int i=0;i<W*L;++i) {
        int p = order[i];
        int to = dir[p];
        if (to>=0) flow.v[size_t(to)] += flow.v[size_t(p)];
    }

    return flow;
}

// ----------------------- river carving ----------------------------

static void carve_rivers(Map2D& H, const Map2D& flow, float threshold, float depth) {
    for (int y=0; y<H.h; ++y) {
        for (int x=0; x<H.w; ++x) {
            float f = flow.at(x,y);
            if (f >= threshold) {
                // logarithmic depth scaling to avoid giant trenches
                float d = depth * std::log2( f / threshold + 1.0f );
                H.at(x,y) -= d;
            }
        }
    }
}

// ----------------------- moisture & temperature -------------------

// Moisture: just another (different-seeded) fBM in [0..1].
static Map2D make_moisture(const Params& P) {
    Map2D M(P.width, P.height, 0.f);
    for (int y=0; y<P.height; ++y)
        for (int x=0; x<P.width; ++x) {
            float m = fbm2(x*P.moisture_freq, y*P.moisture_freq, P.moisture_oct, 2.0f, 0.5f, P.seed ^ 0xDEADBEEFu);
            M.at(x,y) = clamp(m*0.5f + 0.5f, 0.0f, 1.0f);
        }
    return M;
}

// Temperature: colder at poles (y) and with altitude. 0..1
static Map2D make_temperature(const Params& P, const Map2D& H) {
    Map2D T(P.width, P.height, 0.f);
    for (int y=0; y<P.height; ++y) {
        float lat = float(y) / float(P.height-1);        // 0..1 south->north
        float equator_dist = std::abs( lat - 0.5f ) * 2; // 0 at equator, 1 at poles
        for (int x=0; x<P.width; ++x) {
            float alt = H.at(x,y) / (P.height_scale + 1e-5f); // 0..~1
            float t = 1.0f - 0.9f*equator_dist - 0.6f*alt;    // tuneable
            T.at(x,y) = clamp(t, 0.0f, 1.0f);
        }
    }
    return T;
}

// ----------------------- Whittaker-lite biome tags ----------------

static U8Map classify_biomes(const Map2D& T, const Map2D& M, const Map2D& H, float sea_level_world) {
    U8Map B(T.w, T.h, uint8_t(Biome::Bare));
    float sea = sea_level_world; // world-unit threshold for water

    for (int y=0; y<T.h; ++y) {
        for (int x=0; x<T.w; ++x) {
            float h = H.at(x,y);
            if (h <= sea) { B.at(x,y) = (uint8_t)Biome::Ocean; continue; }

            float t = T.at(x,y);
            float m = M.at(x,y);

            // very simple bins (t=temperature 0 cold..1 hot, m=moisture 0 dry..1 wet)
            Biome b = Biome::Bare;
            if      (t < 0.20f)               b = (m < 0.50f) ? Biome::Tundra : Biome::BorealForest;
            else if (t < 0.45f)               b = (m < 0.30f) ? Biome::Shrubland : Biome::TemperateForest;
            else if (t < 0.75f && m < 0.25f)  b = Biome::Desert;
            else if (t < 0.75f && m < 0.50f)  b = Biome::Grassland;
            else if (t < 0.75f)               b = Biome::TemperateForest;
            else if (m < 0.25f)               b = Biome::Desert;
            else if (m < 0.45f)               b = Biome::Savanna;
            else                               b = Biome::TropicalForest;

            // beach band (optional)
            if (h > sea && h < sea + 2.0f) b = Biome::Beach;

            B.at(x,y) = (uint8_t)b;
        }
    }
    return B;
}

// ----------------------- Poisson-disk scatter (Bridson) -----------

static std::vector<Vec2> poisson_disk(const U8Map& biomes, float radius, uint32_t seed) {
    const int W = biomes.w, H = biomes.h;
    const float R = radius;
    const float cell = R / std::sqrt(2.0f);

    int gw = int(std::ceil(W / cell));
    int gh = int(std::ceil(H / cell));
    std::vector<int> grid(size_t(gw)*gh, -1);

    std::vector<Vec2> points;
    std::vector<int> active;

    RNG rng(seed);
    auto in_bounds = [&](float x,float y){ return x>=0 && y>=0 && x<float(W) && y<float(H); };

    auto grid_idx = [&](float x,float y){ int gx=int(x/cell); int gy=int(y/cell);
        gx = std::max(0,std::min(gx,gw-1)); gy = std::max(0,std::min(gy,gh-1)); return gy*gw + gx; };

    auto far_enough = [&](float x, float y){
        int gx = int(x / cell), gy = int(y / cell);
        for (int yy = std::max(0, gy-2); yy <= std::min(gh-1, gy+2); ++yy)
            for (int xx = std::max(0, gx-2); xx <= std::min(gw-1, gx+2); ++xx) {
                int id = grid[yy*gw + xx];
                if (id >= 0) {
                    float dx = points[id].x - x, dy = points[id].y - y;
                    if (dx*dx + dy*dy < R*R) return false;
                }
            }
        return true;
    };

    auto biome_allows_tree = [&](int x,int y){
        Biome b = (Biome)biomes.at(x,y);
        return b==Biome::TemperateForest || b==Biome::BorealForest || b==Biome::TropicalForest || b==Biome::Savanna;
    };

    // Seed with a random point in a forest biome (try a few times)
    for (int tries=0; tries<100; ++tries) {
        int sx = rng.randint(0, W-1), sy = rng.randint(0, H-1);
        if (!biome_allows_tree(sx,sy)) continue;
        Vec2 p{ float(sx) + 0.5f, float(sy) + 0.5f };
        points.push_back(p);
        active.push_back(0);
        grid[ grid_idx(p.x, p.y) ] = 0;
        break;
    }

    const int k = 30; // candidates per active point (Bridson)
    while (!active.empty()) {
        int i = active[ rng.randint(0, int(active.size())-1) ];
        Vec2 base = points[size_t(i)];
        bool found = false;

        for (int c=0;c<k;++c) {
            float ang = rng.uniform(0.0f, 6.2831853f);
            float rad = rng.uniform(R, 2.0f*R);
            Vec2 q{ base.x + rad*std::cos(ang), base.y + rad*std::sin(ang) };

            if (!in_bounds(q.x,q.y)) continue;
            if (!biome_allows_tree(int(q.x), int(q.y))) continue;
            if (!far_enough(q.x,q.y)) continue;

            grid[ grid_idx(q.x,q.y) ] = int(points.size());
            points.push_back(q);
            active.push_back(int(points.size())-1);
            found = true;
        }
        if (!found) {
            // remove i
            auto it = std::find(active.begin(), active.end(), i);
            if (it != active.end()) active.erase(it);
        }
    }
    return points;
}

// ----------------------- top-level orchestrator -------------------

struct GraphResult { Outputs out; };

static Outputs run_procedural_graph(const Params& P) {
    // 1) base height
    Map2D height = generate_height(P);

    // 2) thermal erosion
    thermal_erosion(height, P.thermal_iters, P.talus, P.thermal_strength);

    // 3) flow & rivers
    Map2D flow = flow_accumulation_D8(height);
    carve_rivers(height, flow, P.river_threshold, P.river_depth);

    // 4) climate & biomes
    Map2D moisture = make_moisture(P);
    Map2D temp     = make_temperature(P, height);
    U8Map  biomes  = classify_biomes(temp, moisture, height, /*sea level in world units*/ 0.5f);

    // 5) scattering (trees)
    auto trees = poisson_disk(biomes, P.scatter_radius, P.seed ^ 0xBADCAFEu);

    Outputs out;
    out.height = std::move(height);
    out.flow   = std::move(flow);
    out.moisture = std::move(moisture);
    out.temperature = std::move(temp);
    out.biomes = std::move(biomes);
    out.trees  = std::move(trees);
    return out;
}

} // namespace pg
