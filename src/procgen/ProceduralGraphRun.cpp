#include "procgen/ProceduralGraph.hpp" // adjust include path if needed

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <random>
#include <tuple>
#include <utility>
#include <vector>

namespace { // TU-local helpers (no header emission) ---------------------------------

// math helpers
inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
inline float lerpf(float a, float b, float t)  { return a + (b - a) * t; }
inline float fade(float t)                     { return t*t*t*(t*(t*6.0f - 15.0f) + 10.0f); }
inline int   ifloor(float x)                   { return static_cast<int>(std::floor(x)); }

// RNG & hashing
struct RNG {
    std::mt19937 rng;
    explicit RNG(std::uint32_t seed) : rng(seed) {}
    float uniform(float a, float b){ std::uniform_real_distribution<float>d(a,b); return d(rng); }
    int   randint(int a, int b)    { std::uniform_int_distribution<int>  d(a,b); return d(rng); }
};

inline std::uint32_t pcg_hash(std::uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
inline std::uint32_t hash2i(int x, int y, std::uint32_t seed) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 0x1f123bb5U
                    ^ static_cast<std::uint32_t>(y) * 0x59c3f5a7U
                    ^ (seed * 0x94d049bbU);
    return pcg_hash(h);
}

// noise
inline float grad_dot(int ix, int iy, std::uint32_t seed, float fx, float fy) {
    const std::uint32_t h = hash2i(ix, iy, seed);
    const float a = static_cast<float>(h & 0xffff) / 65535.0f * 6.28318530718f;
    const float gx = std::cos(a), gy = std::sin(a);
    return gx * fx + gy * fy;
}
inline float perlin2(float x, float y, std::uint32_t seed) {
    const int x0 = ifloor(x), y0 = ifloor(y);
    const float tx = x - static_cast<float>(x0), ty = y - static_cast<float>(y0);
    const float n00 = grad_dot(x0,   y0,   seed, tx,       ty);
    const float n10 = grad_dot(x0+1, y0,   seed, tx-1.0f,  ty);
    const float n01 = grad_dot(x0,   y0+1, seed, tx,       ty-1.0f);
    const float n11 = grad_dot(x0+1, y0+1, seed, tx-1.0f,  ty-1.0f);
    const float sx = fade(tx), sy = fade(ty);
    return lerpf(lerpf(n00, n10, sx), lerpf(n01, n11, sx), sy);
}
inline float fbm2(float x, float y, int oct, float lac, float gain, std::uint32_t seed) {
    float amp = 0.5f, freq = 1.0f, sum = 0.0f;
    for (int i=0; i<oct; ++i) {
        sum += perlin2(x*freq, y*freq, seed + static_cast<std::uint32_t>(i*131)) * amp;
        freq *= lac; amp *= gain;
    }
    return sum;
}

} // anonymous namespace

namespace { // high-level helpers that use pg:: types --------------------------------

using pg::Map2D; using pg::U8Map; using pg::Params; using pg::Biome; using pg::Vec2;

Map2D generate_height(const Params& P) {
    Map2D H(P.width, P.height, 0.f);
    for (int y=0; y<P.height; ++y) {
        for (int x=0; x<P.width; ++x) {
            const float u = static_cast<float>(x);
            const float v = static_cast<float>(y);
            float wx = fbm2(u*P.warp_freq, v*P.warp_freq, P.warp_oct, 2.0f, 0.5f, P.seed ^ 0xA5A5A5u);
            float wy = fbm2(u*P.warp_freq + 100.0f, v*P.warp_freq - 100.0f, P.warp_oct, 2.0f, 0.5f, P.seed ^ 0xC0FFEEu);
            wx *= P.warp_amp_px; wy *= P.warp_amp_px;
            const float n  = fbm2((u+wx) * P.base_freq, (v+wy) * P.base_freq, P.octaves, P.lacunarity, P.gain, P.seed);
            const float rn = 1.0f - std::fabs(n);
            const float h01 = clampf( lerpf((n*0.5f+0.5f), rn, 0.6f), 0.0f, 1.0f );
            float e = (h01 - P.sea_level) / (1.0f - P.sea_level);
            H.at(x,y) = clampf(e, 0.0f, 1.0f) * P.height_scale;
        }
    }
    return H;
}

void thermal_erosion(Map2D& H, int iters, float talus, float strength) {
    if (iters <= 0) return;
    Map2D delta(H.w, H.h, 0.0f);
    const int dx8[8] = { -1,0,1,-1,1,-1,0,1 };
    const int dy8[8] = { -1,-1,-1,0,0,1,1,1  };
    for (int it=0; it<iters; ++it) {
        std::fill(delta.v.begin(), delta.v.end(), 0.0f);
        for (int y=0; y<H.h; ++y) for (int x=0; x<H.w; ++x) {
            const float h = H.at(x,y);
            float over_sum = 0.0f, over[8] = {0}; int cnt = 0;
            for (int k=0; k<8; ++k) {
                const int nx=x+dx8[k], ny=y+dy8[k];
                if (nx<0||ny<0||nx>=H.w||ny>=H.h) continue;
                const float dh = h - H.at(nx,ny);
                if (dh > talus) { over[cnt] = dh - talus; cnt++; over_sum += (dh - talus); }
            }
            if (cnt==0 || over_sum<=0.0f) continue;
            const float m = strength * over_sum;
            int idx=0;
            for (int k=0; k<8 && idx<cnt; ++k) {
                const int nx=x+dx8[k], ny=y+dy8[k];
                if (nx<0||ny<0||nx>=H.w||ny>=H.h) continue;
                const float dh = h - H.at(nx,ny);
                if (dh > talus) {
                    const float share = m * ((dh - talus) / over_sum);
                    delta.at(x,y)  -= share;
                    delta.at(nx,ny)+= share;
                    idx++;
                }
            }
        }
        for (size_t i=0; i<H.v.size(); ++i) H.v[i] += delta.v[i];
    }
}

Map2D flow_accumulation_D8(const Map2D& H) {
    const int W = H.w, L = H.h;
    Map2D flow(W, L, 1.0f);
    std::vector<int> dir(static_cast<size_t>(W) * L, -1);
    auto idx = [W](int x,int y){ return y*W + x; };
    const int   dx8[8] = { -1,0,1,-1,1,-1,0,1 };
    const int   dy8[8] = { -1,-1,-1,0,0,1,1,1  };
    const float dist8[8] = { 1.41421356f,1.0f,1.41421356f,1.0f,1.0f,1.41421356f,1.0f,1.41421356f };
    for (int y=0; y<L; ++y) for (int x=0; x<W; ++x) {
        const float h = H.at(x,y);
        float bestSlope = 0.0f; int best = -1;
        for (int k=0; k<8; ++k) {
            const int nx=x+dx8[k], ny=y+dy8[k];
            if (nx<0||ny<0||nx>=W||ny>=L) continue;
            const float dh = h - H.at(nx,ny);
            if (dh > 0.0f) {
                const float slope = dh / dist8[k];
                if (slope > bestSlope) { bestSlope = slope; best = idx(nx,ny); }
            }
        }
        dir[idx(x,y)] = best;
    }
    std::vector<int> order(static_cast<size_t>(W) * L);
    for (int i=0; i<W*L; ++i) order[static_cast<size_t>(i)] = i;
    std::sort(order.begin(), order.end(), [&](int a,int b){ return H.v[static_cast<size_t>(a)] < H.v[static_cast<size_t>(b)]; });
    for (int p : order) {
        const int to = dir[static_cast<size_t>(p)];
        if (to>=0) flow.v[static_cast<size_t>(to)] += flow.v[static_cast<size_t>(p)];
    }
    return flow;
}

void carve_rivers(Map2D& H, const Map2D& flow, float threshold, float depth) {
    for (int y=0; y<H.h; ++y) for (int x=0; x<H.w; ++x) {
        const float f = flow.at(x,y);
        if (f >= threshold) {
            const float d = depth * std::log2(f / threshold + 1.0f);
            H.at(x,y) -= d;
        }
    }
}

Map2D make_moisture(const Params& P) {
    Map2D M(P.width, P.height, 0.f);
    for (int y=0; y<P.height; ++y)
        for (int x=0; x<P.width; ++x) {
            const float m = fbm2(x*P.moisture_freq, y*P.moisture_freq, P.moisture_oct, 2.0f, 0.5f, P.seed ^ 0xDEADBEEFu);
            M.at(x,y) = clampf(m*0.5f + 0.5f, 0.0f, 1.0f);
        }
    return M;
}

Map2D make_temperature(const Params& P, const Map2D& H) {
    Map2D T(P.width, P.height, 0.f);
    for (int y=0; y<P.height; ++y) {
        const float lat = static_cast<float>(y) / static_cast<float>(P.height-1);
        const float equator_dist = std::abs(lat - 0.5f) * 2.0f;
        for (int x=0; x<P.width; ++x) {
            const float alt = H.at(x,y) / (P.height_scale + 1e-5f);
            const float t   = 1.0f - 0.9f*equator_dist - 0.6f*alt;
            T.at(x,y) = clampf(t, 0.0f, 1.0f);
        }
    }
    return T;
}

pg::U8Map classify_biomes(const Map2D& T, const Map2D& M, const Map2D& H, float sea_level_world) {
    pg::U8Map B(T.w, T.h, static_cast<std::uint8_t>(pg::Biome::Bare));
    const float sea = sea_level_world;
    for (int y=0; y<T.h; ++y) for (int x=0; x<T.w; ++x) {
        const float h = H.at(x,y);
        if (h <= sea) { B.at(x,y) = static_cast<std::uint8_t>(pg::Biome::Ocean); continue; }
        const float t = T.at(x,y), m = M.at(x,y);
        pg::Biome b = pg::Biome::Bare;
        if      (t < 0.20f)               b = (m < 0.50f) ? pg::Biome::Tundra : pg::Biome::BorealForest;
        else if (t < 0.45f)               b = (m < 0.30f) ? pg::Biome::Shrubland : pg::Biome::TemperateForest;
        else if (t < 0.75f && m < 0.25f)  b = pg::Biome::Desert;
        else if (t < 0.75f && m < 0.50f)  b = pg::Biome::Grassland;
        else if (t < 0.75f)               b = pg::Biome::TemperateForest;
        else if (m < 0.25f)               b = pg::Biome::Desert;
        else if (m < 0.45f)               b = pg::Biome::Savanna;
        else                               b = pg::Biome::TropicalForest;
        if (h > sea && h < sea + 2.0f)    b = pg::Biome::Beach;
        B.at(x,y) = static_cast<std::uint8_t>(b);
    }
    return B;
}

std::vector<Vec2> poisson_disk(const pg::U8Map& biomes, float radius, std::uint32_t seed) {
    const int W = biomes.w, H = biomes.h;
    const float R = radius, cell = R / std::sqrt(2.0f);
    const int gw = static_cast<int>(std::ceil(W / cell)), gh = static_cast<int>(std::ceil(H / cell));
    std::vector<int> grid(static_cast<size_t>(gw) * gh, -1);
    std::vector<Vec2> points; std::vector<int> active;
    RNG rng(seed);
    auto in_bounds = [&](float x,float y){ return x>=0 && y>=0 && x<float(W) && y<float(H); };
    auto grid_idx  = [&](float x,float y){
        int gx = static_cast<int>(x/cell), gy = static_cast<int>(y/cell);
        gx = std::max(0,std::min(gx,gw-1)); gy = std::max(0,std::min(gy,gh-1)); return gy*gw + gx; };
    auto far_enough = [&](float x, float y){
        int gx = static_cast<int>(x / cell), gy = static_cast<int>(y / cell);
        for (int yy = std::max(0, gy-2); yy <= std::min(gh-1, gy+2); ++yy)
        for (int xx = std::max(0, gx-2); xx <= std::min(gw-1, gx+2); ++xx) {
            const int id = grid[yy*gw + xx];
            if (id >= 0) {
                const float dx = points[static_cast<size_t>(id)].x - x;
                const float dy = points[static_cast<size_t>(id)].y - y;
                if (dx*dx + dy*dy < R*R) return false;
            }
        }
        return true;
    };
    auto biome_allows_tree = [&](int x,int y){
        const pg::Biome b = static_cast<pg::Biome>(biomes.at(x,y));
        return b==pg::Biome::TemperateForest || b==pg::Biome::BorealForest || b==pg::Biome::TropicalForest || b==pg::Biome::Savanna;
    };
    // seed
    for (int tries=0; tries<100; ++tries) {
        const int sx = rng.randint(0, W-1), sy = rng.randint(0, H-1);
        if (!biome_allows_tree(sx,sy)) continue;
        const Vec2 p{ static_cast<float>(sx) + 0.5f, static_cast<float>(sy) + 0.5f };
        points.push_back(p); active.push_back(0); grid[grid_idx(p.x, p.y)] = 0; break;
    }
    const int k = 30;
    while (!active.empty()) {
        const int i = active[rng.randint(0, static_cast<int>(active.size())-1)];
        const Vec2 base = points[static_cast<size_t>(i)];
        bool found = false;
        for (int c=0; c<k; ++c) {
            const float ang = rng.uniform(0.0f, 6.2831853f);
            const float rad = rng.uniform(R, 2.0f*R);
            const Vec2 q{ base.x + rad*std::cos(ang), base.y + rad*std::sin(ang) };
            if (!in_bounds(q.x,q.y)) continue;
            if (!biome_allows_tree(static_cast<int>(q.x), static_cast<int>(q.y))) continue;
            if (!far_enough(q.x,q.y)) continue;
            grid[ grid_idx(q.x,q.y) ] = static_cast<int>(points.size());
            points.push_back(q);
            active.push_back(static_cast<int>(points.size())-1);
            found = true;
        }
        if (!found) {
            auto it = std::find(active.begin(), active.end(), i);
            if (it != active.end()) active.erase(it);
        }
    }
    return points;
}

} // anonymous namespace

// ---------------- public API (matches header exactly) ----------------
namespace pg {

Outputs run_procedural_graph(const Params& P) {
    Map2D height = generate_height(P);
    thermal_erosion(height, P.thermal_iters, P.talus, P.thermal_strength);

    Map2D flow = flow_accumulation_D8(height);
    carve_rivers(height, flow, P.river_threshold, P.river_depth);

    Map2D moisture = make_moisture(P);
    Map2D temp     = make_temperature(P, height);
    U8Map  biomes  = classify_biomes(temp, moisture, height, /*sea (world units)*/ 0.5f);

    Outputs out;
    out.height      = std::move(height);
    out.flow        = std::move(flow);
    out.moisture    = std::move(moisture);
    out.temperature = std::move(temp);
    out.biomes      = std::move(biomes);
    out.trees       = poisson_disk(out.biomes, P.scatter_radius, P.seed ^ 0xBADCAFEu);
    return out;
}

} // namespace pg
