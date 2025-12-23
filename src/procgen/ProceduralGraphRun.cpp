#include "procgen/ProceduralGraph.hpp" // adjust include path if needed
#include "procgen/PriorityFlood.hpp"
#include "procgen/TerrainStamps.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <deque>
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

using pg::Map2D; using pg::U8Map; using pg::Params; using pg::Biome; using pg::WaterKind; using pg::Vec2; using pg::Stamp;

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
    std::sort(order.begin(), order.end(), [&](int a,int b){ return H.v[static_cast<size_t>(a)] > H.v[static_cast<size_t>(b)]; });
    for (int p : order) {
        const int to = dir[static_cast<size_t>(p)];
        if (to>=0) flow.v[static_cast<size_t>(to)] += flow.v[static_cast<size_t>(p)];
    }
    return flow;
}

void carve_rivers(Map2D& H,
                 const Map2D& flow,
                 float threshold,
                 float depth,
                 const U8Map* water_skip = nullptr)
{
    // Carve only on land unless caller opts out.
    // (Prevents turning lakes/ocean into deep trenches.)
    for (int y=0; y<H.h; ++y) for (int x=0; x<H.w; ++x) {
        if (water_skip) {
            const auto wk = static_cast<WaterKind>(water_skip->at(x,y));
            if (wk != WaterKind::Land) continue;
        }

        const float f = flow.at(x,y);
        if (f >= threshold) {
            const float d = depth * std::log2(f / threshold + 1.0f);
            H.at(x,y) -= d;
        }
    }
}

// Convert the Priority-Flood "filled mask" into a more usable "lake mask":
//  - only cells above ocean_level
//  - only where (filled - original) >= min_depth
//  - only components with area >= min_area
U8Map compute_lake_mask(const Map2D& original,
                        const Map2D& filled,
                        const U8Map& filled_mask,
                        float ocean_level,
                        float min_depth,
                        int   min_area)
{
    const int W = original.w, Hh = original.h;
    U8Map lake(W, Hh, 0);
    if (W<=0 || Hh<=0) return lake;
    if (filled.w != W || filled.h != Hh || filled_mask.w != W || filled_mask.h != Hh) return lake;
    if (min_area <= 0) return lake;

    std::vector<std::uint8_t> seen(static_cast<size_t>(W) * Hh, 0);
    std::vector<int> q;
    q.reserve(static_cast<size_t>(W) * Hh / 16);

    auto idx = [W](int x,int y){ return y*W + x; };
    auto inside = [W,Hh](int x,int y){ return (unsigned)x < (unsigned)W && (unsigned)y < (unsigned)Hh; };

    for (int y=0; y<Hh; ++y) for (int x=0; x<W; ++x) {
        const int i = idx(x,y);
        if (seen[static_cast<size_t>(i)]) continue;
        seen[static_cast<size_t>(i)] = 1;

        if (!filled_mask.at(x,y)) continue;
        if (original.at(x,y) <= ocean_level) continue;
        const float depth = filled.at(x,y) - original.at(x,y);
        if (depth < min_depth) continue;

        // BFS component
        std::vector<int> comp;
        comp.reserve(256);
        comp.push_back(i);

        q.clear();
        q.push_back(i);

        for (size_t qi=0; qi<q.size(); ++qi) {
            const int cur = q[qi];
            const int cx = cur % W;
            const int cy = cur / W;

            static const int dx4[4] = {+1,-1,0,0};
            static const int dy4[4] = {0,0,+1,-1};

            for (int k=0; k<4; ++k) {
                const int nx = cx + dx4[k];
                const int ny = cy + dy4[k];
                if (!inside(nx,ny)) continue;
                const int ni = idx(nx,ny);
                if (seen[static_cast<size_t>(ni)]) continue;

                seen[static_cast<size_t>(ni)] = 1;

                if (!filled_mask.at(nx,ny)) continue;
                if (original.at(nx,ny) <= ocean_level) continue;
                const float d2 = filled.at(nx,ny) - original.at(nx,ny);
                if (d2 < min_depth) continue;

                q.push_back(ni);
                comp.push_back(ni);
            }
        }

        if ((int)comp.size() >= min_area) {
            for (int ci : comp) lake.v[static_cast<size_t>(ci)] = 1;
        }
    }

    return lake;
}

U8Map build_water_map(const Map2D& height_before_carve,
                      const Map2D& flow,
                      const U8Map& lake_mask,
                      float ocean_level,
                      float river_threshold)
{
    const int W = height_before_carve.w, Hh = height_before_carve.h;
    U8Map water(W, Hh, static_cast<std::uint8_t>(WaterKind::Land));
    if (W<=0 || Hh<=0) return water;

    // Ocean first
    for (int y=0; y<Hh; ++y) for (int x=0; x<W; ++x) {
        if (height_before_carve.at(x,y) <= ocean_level) {
            water.at(x,y) = static_cast<std::uint8_t>(WaterKind::Ocean);
        }
    }

    // Lakes
    if (lake_mask.w == W && lake_mask.h == Hh) {
        for (int y=0; y<Hh; ++y) for (int x=0; x<W; ++x) {
            if (lake_mask.at(x,y) && water.at(x,y) == static_cast<std::uint8_t>(WaterKind::Land)) {
                water.at(x,y) = static_cast<std::uint8_t>(WaterKind::Lake);
            }
        }
    }

    // Rivers
    for (int y=0; y<Hh; ++y) for (int x=0; x<W; ++x) {
        if (water.at(x,y) != static_cast<std::uint8_t>(WaterKind::Land)) continue;
        if (flow.at(x,y) >= river_threshold) {
            water.at(x,y) = static_cast<std::uint8_t>(WaterKind::River);
        }
    }

    return water;
}

void apply_moisture_from_water(Map2D& moisture,
                               const U8Map& water,
                               float strength,
                               float radius_cells,
                               bool include_ocean)
{
    if (strength <= 0.0f || radius_cells <= 0.0f) return;
    const int W = moisture.w, Hh = moisture.h;
    if (W<=0 || Hh<=0) return;
    if (water.w != W || water.h != Hh) return;

    // Multi-source BFS distance transform (Manhattan distance).
    std::vector<int> dist(static_cast<size_t>(W) * Hh, -1);
    std::deque<int> q;

    auto push = [&](int i){
        dist[static_cast<size_t>(i)] = 0;
        q.push_back(i);
    };

    for (int y=0; y<Hh; ++y) for (int x=0; x<W; ++x) {
        const int i = y*W + x;
        const auto wk = static_cast<WaterKind>(water.at(x,y));
        const bool is_source =
            (wk == WaterKind::River) || (wk == WaterKind::Lake) || (include_ocean && wk == WaterKind::Ocean);
        if (is_source) push(i);
    }

    if (q.empty()) return;

    static const int dx4[4] = {+1,-1,0,0};
    static const int dy4[4] = {0,0,+1,-1};

    while (!q.empty()) {
        const int cur = q.front(); q.pop_front();
        const int cx = cur % W;
        const int cy = cur / W;
        const int cd = dist[static_cast<size_t>(cur)];

        for (int k=0; k<4; ++k) {
            const int nx = cx + dx4[k];
            const int ny = cy + dy4[k];
            if ((unsigned)nx >= (unsigned)W || (unsigned)ny >= (unsigned)Hh) continue;
            const int ni = ny*W + nx;
            if (dist[static_cast<size_t>(ni)] != -1) continue;
            dist[static_cast<size_t>(ni)] = cd + 1;
            q.push_back(ni);
        }
    }

    // Blend in an exponential falloff from water.
    const float invR = 1.0f / radius_cells;
    for (int y=0; y<Hh; ++y) for (int x=0; x<W; ++x) {
        const int i = y*W + x;
        const int d = dist[static_cast<size_t>(i)];
        if (d < 0) continue;
        const float w = std::exp(-static_cast<float>(d) * invR);
        moisture.at(x,y) = clampf(lerpf(moisture.at(x,y), w, strength), 0.0f, 1.0f);
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

std::vector<Vec2> poisson_disk(const pg::U8Map& biomes, const pg::U8Map* water, float radius, std::uint32_t seed) {
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
        if (water) {
            const auto wk = static_cast<WaterKind>(water->at(x,y));
            if (wk != WaterKind::Land) return false;
        }
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
    // 1) Base height
    Map2D height = generate_height(P);

    // 1b) Optional landmark stamps (craters/volcanoes)
    std::vector<Stamp> stamps;
    if (P.enable_stamps && (P.crater_count > 0 || P.volcano_count > 0)) {
        pg::stamps::StampParams S;
        S.enable = true;
        S.seed   = P.seed ^ 0x31415926u;
        S.min_spacing = P.stamp_min_spacing;

        S.crater_count      = P.crater_count;
        S.crater_radius_min = P.crater_radius_min;
        S.crater_radius_max = P.crater_radius_max;
        S.crater_depth      = P.crater_depth;
        S.crater_rim_height = P.crater_rim_height;

        S.volcano_count        = P.volcano_count;
        S.volcano_radius_min   = P.volcano_radius_min;
        S.volcano_radius_max   = P.volcano_radius_max;
        S.volcano_height       = P.volcano_height;
        S.volcano_crater_ratio = P.volcano_crater_ratio;

        stamps = pg::stamps::generate(P.width, P.height, S);
        pg::stamps::apply(height, stamps, S);
    }

    // 2) Erosion
    thermal_erosion(height, P.thermal_iters, P.talus, P.thermal_strength);

    // Keep a copy before carving rivers (used for water classification).
    const Map2D height_before_rivers = height;

    // 3) Hydrology / rivers
    const float ocean_level = 0.0f; // in this pipeline, sea floor is clamped to 0 during height mapping

    Map2D flow_input = height_before_rivers;
    U8Map lake_mask(P.width, P.height, 0);

    if (P.enable_depression_fill) {
        auto filled = pg::hydro::priority_flood_fill(flow_input, ocean_level, P.fill_epsilon, /*seed water*/true);
        flow_input = std::move(filled.filled);
        lake_mask  = compute_lake_mask(height_before_rivers, flow_input, filled.filled_mask,
                                       ocean_level, P.lake_min_depth, P.lake_min_area);
    }

    Map2D flow = flow_accumulation_D8(flow_input);

    // WaterKind map (ocean / lakes / rivers)
    U8Map water = build_water_map(height_before_rivers, flow, lake_mask, ocean_level, P.river_threshold);

    // Carve rivers only on land.
    carve_rivers(height, flow, P.river_threshold, P.river_depth, &water);

    // 4) Climate
    Map2D moisture = make_moisture(P);
    if (P.moisture_from_water) {
        apply_moisture_from_water(moisture, water, P.moisture_water_strength, P.moisture_water_radius, P.moisture_include_ocean);
    }
    Map2D temp     = make_temperature(P, height);

    // 5) Biomes
    U8Map  biomes  = classify_biomes(temp, moisture, height, /*sea (world units)*/ ocean_level);

    // 6) Scatter
    Outputs out;
    out.height      = std::move(height);
    out.flow        = std::move(flow);
    out.moisture    = std::move(moisture);
    out.temperature = std::move(temp);
    out.biomes      = std::move(biomes);
    out.water       = std::move(water);
    out.stamps      = std::move(stamps);

    out.trees       = poisson_disk(out.biomes, &out.water, P.scatter_radius, P.seed ^ 0xBADCAFEu);
    return out;
}

} // namespace pg
