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

using pg::Map2D; using pg::U8Map; using pg::Params; using pg::Biome; using pg::Vec2; using pg::SettlementSite; using pg::RoadSegment; using pg::Outputs;

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


// ---------------- settlement + roads layer ------------------------------------
//
// This is a lightweight "content multiplier" layer:
//   - score start sites (freshwater, slope, biome, resources, flood risk)
//   - pick 3..8 settlements with a Poisson-ish min-distance constraint
//   - connect them with an MST, then rasterize each edge via A* on a terrain cost-field
//   - stamp farmland near freshwater + forest near moisture
//
// Everything stays within this TU to keep the public header clean.

struct SiteCandidate {
    int x = 0, y = 0;
    float score = 0.0f;      // 0..1
    float water_dist = 0.0f; // cells
    float slope_n = 0.0f;    // 0..1
    float fertility = 0.0f;  // 0..1
    std::uint8_t biome = 0;
};

static inline float biome_desirability(Biome b) {
    switch (b) {
        case Biome::Grassland:       return 1.00f;
        case Biome::TemperateForest: return 0.90f;
        case Biome::Savanna:         return 0.78f;
        case Biome::BorealForest:    return 0.70f;
        case Biome::TropicalForest:  return 0.70f;
        case Biome::Shrubland:       return 0.55f;
        case Biome::Beach:           return 0.40f;
        case Biome::Bare:            return 0.25f;
        case Biome::Tundra:          return 0.20f;
        case Biome::Desert:          return 0.10f;
        case Biome::Ocean:           return 0.00f;
        default:                     return 0.40f;
    }
}

static inline float biome_road_penalty01(Biome b) {
    switch (b) {
        case Biome::Desert: return 1.0f;
        case Biome::Tundra: return 0.9f;
        case Biome::Bare:   return 0.7f;
        case Biome::Beach:  return 0.6f;
        default:            return 0.2f;
    }
}

static Map2D slope_normalized(const Map2D& H) {
    Map2D S(H.w, H.h, 0.0f);
    float maxv = 0.0f;
    for (int y=0; y<H.h; ++y) for (int x=0; x<H.w; ++x) {
        const float hL = H.at(std::max(0, x-1), y);
        const float hR = H.at(std::min(H.w-1, x+1), y);
        const float hD = H.at(x, std::max(0, y-1));
        const float hU = H.at(x, std::min(H.h-1, y+1));
        const float dx = (hR - hL) * 0.5f;
        const float dy = (hU - hD) * 0.5f;
        const float s  = std::sqrt(dx*dx + dy*dy);
        S.at(x,y) = s;
        if (s > maxv) maxv = s;
    }
    if (maxv < 1e-6f) {
        std::fill(S.v.begin(), S.v.end(), 0.0f);
        return S;
    }
    const float inv = 1.0f / maxv;
    for (float& v : S.v) v = clampf(v * inv, 0.0f, 1.0f);
    return S;
}

static U8Map river_mask(const Map2D& flow, float threshold) {
    U8Map R(flow.w, flow.h, 0);
    for (int y=0; y<flow.h; ++y) for (int x=0; x<flow.w; ++x) {
        if (flow.at(x,y) >= threshold) R.at(x,y) = 255;
    }
    return R;
}

static Map2D distance_to_mask4(const U8Map& mask) {
    const int W = mask.w, H = mask.h;
    Map2D D(W, H, 1.0e9f);
    std::queue<int> q;
    for (int y=0; y<H; ++y) for (int x=0; x<W; ++x) {
        if (mask.at(x,y) > 0) {
            const int id = y*W + x;
            D.v[static_cast<size_t>(id)] = 0.0f;
            q.push(id);
        }
    }
    if (q.empty()) return D;

    const int dx4[4] = { -1, 1, 0, 0 };
    const int dy4[4] = {  0, 0,-1, 1 };
    while (!q.empty()) {
        const int id = q.front(); q.pop();
        const int x = id % W, y = id / W;
        const float base = D.v[static_cast<size_t>(id)];
        for (int k=0; k<4; ++k) {
            const int nx = x + dx4[k], ny = y + dy4[k];
            if (nx<0 || ny<0 || nx>=W || ny>=H) continue;
            const int nid = ny*W + nx;
            const float nd = base + 1.0f;
            float& cur = D.v[static_cast<size_t>(nid)];
            if (nd < cur) { cur = nd; q.push(nid); }
        }
    }
    return D;
}

static Map2D flood_risk01(const Params& P, const Map2D& H, const Map2D& flow,
                          const Map2D& riverDist, const U8Map& biomes) {
    Map2D F(H.w, H.h, 0.0f);
    const float hs = std::max(1.0f, P.height_scale);
    for (int y=0; y<H.h; ++y) for (int x=0; x<H.w; ++x) {
        const Biome b = static_cast<Biome>(biomes.at(x,y));
        if (b == Biome::Ocean) { F.at(x,y) = 1.0f; continue; }

        const float d = riverDist.at(x,y);
        const float infl = std::exp(-d / 18.0f);
        const float f    = flow.at(x,y);
        const float fn   = clampf(std::log1p(std::max(0.0f, f / std::max(1.0f, P.river_threshold))) / 4.0f, 0.0f, 1.0f);
        const float hn   = clampf(H.at(x,y) / hs, 0.0f, 1.0f);
        const float low  = clampf((0.28f - hn) / 0.28f, 0.0f, 1.0f);
        F.at(x,y) = clampf(infl * fn * (0.65f + 0.35f*low), 0.0f, 1.0f);
    }
    return F;
}

static inline float sample_resources01(const U8Map& wood, const U8Map& stone, const U8Map& ore, int x, int y) {
    const int W = wood.w, H = wood.h;
    const int ox[9] = {0, 12,-12, 0, 0,  8, -8,  8, -8};
    const int oy[9] = {0,  0,  0,12,-12, 8,  8, -8, -8};
    float acc = 0.0f;
    for (int i=0;i<9;++i){
        const int sx = std::max(0, std::min(W-1, x + ox[i]));
        const int sy = std::max(0, std::min(H-1, y + oy[i]));
        const float w = wood.at(sx,sy)  / 255.0f;
        const float s = stone.at(sx,sy) / 255.0f;
        const float o = ore.at(sx,sy)   / 255.0f;
        acc += (w + s + o) / 3.0f;
    }
    return acc / 9.0f;
}

static void build_fertility_resources(const Params& P,
                                      const Map2D& height,
                                      const Map2D& moisture,
                                      const Map2D& temp,
                                      const U8Map& biomes,
                                      const Map2D& slopeN,
                                      const Map2D& riverDist,
                                      U8Map& fertility,
                                      U8Map& farmland,
                                      U8Map& forest,
                                      U8Map& res_wood,
                                      U8Map& res_stone,
                                      U8Map& res_ore)
{
    const int W = height.w, H = height.h;
    fertility = U8Map(W, H, 0);
    farmland  = U8Map(W, H, 0);
    forest    = U8Map(W, H, 0);
    res_wood  = U8Map(W, H, 0);
    res_stone = U8Map(W, H, 0);
    res_ore   = U8Map(W, H, 0);

    const float hs = std::max(1.0f, P.height_scale);

    for (int y=0; y<H; ++y) for (int x=0; x<W; ++x) {
        const Biome b = static_cast<Biome>(biomes.at(x,y));
        if (b == Biome::Ocean) continue;

        const float m = clampf(moisture.at(x,y), 0.0f, 1.0f);
        const float t = clampf(temp.at(x,y),     0.0f, 1.0f);
        const float s = clampf(slopeN.at(x,y),   0.0f, 1.0f);

        const float climate = clampf(1.0f - std::fabs(t - 0.55f) / 0.55f, 0.0f, 1.0f);
        const float water_bonus = std::exp(-riverDist.at(x,y) / 55.0f);
        float fert = std::pow(m, 0.9f) * climate * (1.0f - 0.85f*s) * (0.70f + 0.30f*water_bonus);

        // bias by biome (small so climate still dominates)
        fert *= clampf(0.60f + 0.40f * biome_desirability(b), 0.0f, 1.0f);

        fert = clampf(fert, 0.0f, 1.0f);
        const std::uint8_t fu8 = static_cast<std::uint8_t>(clampf(fert * 255.0f, 0.0f, 255.0f));
        fertility.at(x,y) = fu8;

        // forest stamp (mostly moisture-driven)
        if (P.stamp_forest) {
            const std::uint8_t mu8 = static_cast<std::uint8_t>(clampf(m * 255.0f, 0.0f, 255.0f));
            if (mu8 >= P.forest_min_moisture && s < 0.85f) forest.at(x,y) = 255;
        }

        // farmland stamp (requires fertility + freshwater proximity)
        if (P.stamp_farmland) {
            if (fu8 >= P.farmland_min_fertility &&
                riverDist.at(x,y) <= P.farmland_radius &&
                s <= 0.35f)
            {
                farmland.at(x,y) = 255;
            }
        }

        // simple resource masks
        if (forest.at(x,y) > 0 || b==Biome::TemperateForest || b==Biome::BorealForest || b==Biome::TropicalForest) {
            res_wood.at(x,y) = 255;
        }
        if (s > 0.45f || (height.at(x,y) / hs) > 0.55f) {
            res_stone.at(x,y) = 255;
        }

        // ore: mountain-ish + noisy
        const float hn = clampf(height.at(x,y) / hs, 0.0f, 1.0f);
        const float ore_base = clampf((hn - 0.55f) / 0.45f, 0.0f, 1.0f) * clampf((s - 0.35f) / 0.65f, 0.0f, 1.0f);
        const float n = fbm2(x * 0.02f, y * 0.02f, 3, 2.0f, 0.5f, P.seed ^ 0xC0FFEEu) * 0.5f + 0.5f;
        const float ore = clampf(ore_base * n, 0.0f, 1.0f);
        res_ore.at(x,y) = static_cast<std::uint8_t>(clampf(ore * 255.0f, 0.0f, 255.0f));
    }

    // Keep forests out of explicit farmland areas
    for (int y=0; y<H; ++y) for (int x=0; x<W; ++x) {
        if (farmland.at(x,y) > 0) forest.at(x,y) = 0;
    }
}

static std::vector<SiteCandidate> score_site_candidates(const Params& P,
                                                        const Map2D& height,
                                                        const Map2D& slopeN,
                                                        const Map2D& riverDist,
                                                        const Map2D& floodRisk,
                                                        const U8Map& biomes,
                                                        const U8Map& fertility,
                                                        const U8Map& res_wood,
                                                        const U8Map& res_stone,
                                                        const U8Map& res_ore)
{
    std::vector<SiteCandidate> C;
    const int step = std::max(1, P.site_sample_step);
    C.reserve(static_cast<size_t>(height.w/step) * (height.h/step));

    for (int y=0; y<height.h; y+=step) for (int x=0; x<height.w; x+=step) {
        const Biome b = static_cast<Biome>(biomes.at(x,y));
        if (b == Biome::Ocean) continue;

        const float d = riverDist.at(x,y);
        if (d < 2.0f) continue; // don't place directly inside the carved channel

        const float s = slopeN.at(x,y);
        const float fert01 = fertility.at(x,y) / 255.0f;

        // water score: good when near, bad when *too* near
        const float near = 1.0f - clampf(d / std::max(1.0f, P.water_max_dist), 0.0f, 1.0f);
        const float tooClose = clampf((P.water_preferred_dist - d) / std::max(1.0f, P.water_preferred_dist), 0.0f, 1.0f);
        const float waterScore = clampf(near - 0.70f*tooClose, 0.0f, 1.0f);

        // slope score: prefer flatter; hard penalize above threshold
        float slopeScore = clampf(1.0f - s / std::max(0.001f, P.max_slope_for_sites), 0.0f, 1.0f);
        slopeScore *= slopeScore;

        const float biomeScore = biome_desirability(b);
        const float resScore   = sample_resources01(res_wood, res_stone, res_ore, x, y);
        const float floodScore = 1.0f - clampf(floodRisk.at(x,y), 0.0f, 1.0f);

        const float wsum = std::max(0.001f, P.w_water + P.w_slope + P.w_biome + P.w_resource + P.w_flood);
        float score = 0.0f;
        score += P.w_water    * waterScore;
        score += P.w_slope    * slopeScore;
        score += P.w_biome    * biomeScore;
        score += P.w_resource * resScore;
        score += P.w_flood    * floodScore;
        score /= wsum;

        // fertility is important in colony games: treat as multiplicative gate
        score *= (0.55f + 0.45f * fert01);

        // light altitude penalty (avoid extreme mountains)
        const float hn = height.at(x,y) / std::max(1.0f, P.height_scale);
        score *= (1.0f - 0.30f * clampf((hn - 0.75f) / 0.25f, 0.0f, 1.0f));

        score = clampf(score, 0.0f, 1.0f);
        if (score <= 0.0f) continue;

        SiteCandidate sc;
        sc.x = x; sc.y = y;
        sc.score = score;
        sc.water_dist = d;
        sc.slope_n = s;
        sc.fertility = fert01;
        sc.biome = static_cast<std::uint8_t>(b);
        C.push_back(sc);
    }

    // Keep only top N
    const size_t keep = static_cast<size_t>(std::max(64, P.top_site_candidates));
    if (C.size() > keep) {
        std::nth_element(C.begin(), C.begin() + static_cast<std::ptrdiff_t>(keep), C.end(),
                         [](const SiteCandidate& a, const SiteCandidate& b){ return a.score > b.score; });
        C.resize(keep);
    }
    std::sort(C.begin(), C.end(), [](const SiteCandidate& a, const SiteCandidate& b){ return a.score > b.score; });
    return C;
}

static SettlementSite to_site(const SiteCandidate& c) {
    SettlementSite s;
    s.pos = Vec2{ static_cast<float>(c.x) + 0.5f, static_cast<float>(c.y) + 0.5f };
    s.score = c.score;
    s.water_dist = c.water_dist;
    s.slope_n = c.slope_n;
    s.fertility = c.fertility;
    s.biome = c.biome;
    return s;
}

static std::vector<SettlementSite> place_settlements(const Params& P,
                                                     const std::vector<SiteCandidate>& candidates,
                                                     const SettlementSite& start,
                                                     std::uint32_t seed)
{
    std::vector<SettlementSite> S;
    if (candidates.empty()) return S;

    RNG rng(seed);

    const int minN = std::max(0, P.settlements_min);
    const int maxN = std::max(minN, P.settlements_max);
    const int target = rng.randint(minN, maxN);

    const float minDist2 = P.settlement_min_dist * P.settlement_min_dist;

    auto far_enough = [&](float x, float y) {
        const float dx0 = start.pos.x - x, dy0 = start.pos.y - y;
        if (dx0*dx0 + dy0*dy0 < minDist2) return false;
        for (const auto& s : S) {
            const float dx = s.pos.x - x, dy = s.pos.y - y;
            if (dx*dx + dy*dy < minDist2) return false;
        }
        return true;
    };

    // pick pool: bias towards top candidates but allow variety
    const int pool = std::min<int>(static_cast<int>(candidates.size()),
                                   std::max(64, std::min(1024, P.top_site_candidates)));

    for (int tries=0; tries<P.settlement_try_budget && static_cast<int>(S.size()) < target; ++tries) {
        const int idx = rng.randint(0, pool-1);
        const auto& c = candidates[static_cast<size_t>(idx)];
        if (c.score < P.settlement_score_cut) continue;
        const float px = static_cast<float>(c.x) + 0.5f;
        const float py = static_cast<float>(c.y) + 0.5f;
        if (!far_enough(px, py)) continue;
        S.push_back(to_site(c));
    }

    return S;
}

static std::vector<std::pair<int,int>> mst_edges_prim(const std::vector<SettlementSite>& nodes) {
    const int N = static_cast<int>(nodes.size());
    std::vector<std::pair<int,int>> edges;
    if (N <= 1) return edges;

    std::vector<float> best(static_cast<size_t>(N), std::numeric_limits<float>::infinity());
    std::vector<int>   parent(static_cast<size_t>(N), -1);
    std::vector<char>  used(static_cast<size_t>(N), 0);
    best[0] = 0.0f;

    for (int it=0; it<N; ++it) {
        int u = -1;
        float bu = std::numeric_limits<float>::infinity();
        for (int i=0;i<N;++i) if (!used[static_cast<size_t>(i)] && best[static_cast<size_t>(i)] < bu) {
            bu = best[static_cast<size_t>(i)];
            u = i;
        }
        if (u < 0) break;
        used[static_cast<size_t>(u)] = 1;
        if (parent[static_cast<size_t>(u)] >= 0) edges.emplace_back(parent[static_cast<size_t>(u)], u);

        for (int v=0; v<N; ++v) if (!used[static_cast<size_t>(v)]) {
            const float dx = nodes[static_cast<size_t>(u)].pos.x - nodes[static_cast<size_t>(v)].pos.x;
            const float dy = nodes[static_cast<size_t>(u)].pos.y - nodes[static_cast<size_t>(v)].pos.y;
            const float d  = std::sqrt(dx*dx + dy*dy);
            if (d < best[static_cast<size_t>(v)]) {
                best[static_cast<size_t>(v)] = d;
                parent[static_cast<size_t>(v)] = u;
            }
        }
    }
    return edges;
}

struct AStarNode { int id; float f; };
struct AStarCmp { bool operator()(const AStarNode& a, const AStarNode& b) const { return a.f > b.f; } };

static std::vector<int> astar_path_grid(const Params& P,
                                       int sx, int sy, int gx, int gy,
                                       const Map2D& slopeN,
                                       const Map2D& flow,
                                       const U8Map& biomes)
{
    const int W = biomes.w, H = biomes.h;
    auto inside = [&](int x,int y){ return x>=0 && y>=0 && x<W && y<H; };
    if (!inside(sx,sy) || !inside(gx,gy)) return {};

    auto cell_cost = [&](int x,int y){
        const Biome b = static_cast<Biome>(biomes.at(x,y));
        if (b == Biome::Ocean) return P.road_ocean_penalty;

        float c = P.road_base_cost;
        const float s = clampf(slopeN.at(x,y), 0.0f, 1.0f);
        c += P.road_slope_cost * (s*s);
        c += P.road_biome_penalty * biome_road_penalty01(b);

        if (flow.at(x,y) >= P.river_threshold) c += P.road_river_penalty;
        return c;
    };

    const int start = sy*W + sx;
    const int goal  = gy*W + gx;

    const float INF = std::numeric_limits<float>::infinity();
    std::vector<float> gScore(static_cast<size_t>(W)*H, INF);
    std::vector<int>   came  (static_cast<size_t>(W)*H, -1);
    std::vector<char>  closed(static_cast<size_t>(W)*H, 0);
    std::priority_queue<AStarNode, std::vector<AStarNode>, AStarCmp> open;

    auto heuristic = [&](int id){
        const int x = id % W, y = id / W;
        const float dx = static_cast<float>(gx - x);
        const float dy = static_cast<float>(gy - y);
        return std::sqrt(dx*dx + dy*dy);
    };

    gScore[static_cast<size_t>(start)] = 0.0f;
    open.push(AStarNode{start, heuristic(start)});

    const int dx8[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
    const int dy8[8] = { -1,-1,-1,  0, 0,  1, 1, 1 };
    const float stepCost[8] = { 1.4142136f,1.0f,1.4142136f, 1.0f,1.0f, 1.4142136f,1.0f,1.4142136f };

    int expansions = 0;

    while (!open.empty()) {
        const int cur = open.top().id;
        open.pop();
        if (closed[static_cast<size_t>(cur)]) continue;
        closed[static_cast<size_t>(cur)] = 1;

        if (cur == goal) break;

        if (++expansions > P.road_max_expansions) break;

        const int cx = cur % W, cy = cur / W;
        const float gc = gScore[static_cast<size_t>(cur)];
        for (int k=0;k<8;++k) {
            const int nx = cx + dx8[k], ny = cy + dy8[k];
            if (!inside(nx,ny)) continue;
            const int nid = ny*W + nx;
            if (closed[static_cast<size_t>(nid)]) continue;

            const float cc = cell_cost(nx,ny);
            if (cc >= P.road_ocean_penalty * 0.5f) continue; // blocked

            const float nc = cell_cost(cx,cy);
            const float step = stepCost[k] * 0.5f * (nc + cc);
            const float ng = gc + step;

            if (ng < gScore[static_cast<size_t>(nid)]) {
                gScore[static_cast<size_t>(nid)] = ng;
                came[static_cast<size_t>(nid)] = cur;
                open.push(AStarNode{ nid, ng + heuristic(nid) });
            }
        }
    }

    if (came[static_cast<size_t>(goal)] < 0 && goal != start) return {};

    // reconstruct
    std::vector<int> path;
    int cur = goal;
    path.push_back(cur);
    while (cur != start) {
        cur = came[static_cast<size_t>(cur)];
        if (cur < 0) break;
        path.push_back(cur);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

static void rasterize_path_to_roads(const std::vector<int>& path, int W,
                                   U8Map& road_mask, std::vector<RoadSegment>& segs)
{
    if (path.size() < 2) return;

    auto id_to_xy = [&](int id){ return std::pair<int,int>(id % W, id / W); };
    auto center = [&](int x,int y){ return Vec2{ static_cast<float>(x)+0.5f, static_cast<float>(y)+0.5f }; };

    // mark mask
    for (int id : path) {
        const int x = id % W, y = id / W;
        if (x>=0 && y>=0 && x<road_mask.w && y<road_mask.h) road_mask.at(x,y) = 255;
    }

    // compress into polyline segments by direction changes
    auto [sx,sy] = id_to_xy(path.front());
    auto [px,py] = id_to_xy(path.front());
    int dirx = 0, diry = 0;

    for (size_t i=1;i<path.size();++i) {
        auto [cx,cy] = id_to_xy(path[i]);
        const int dx = cx - px;
        const int dy = cy - py;

        if (i == 1) {
            dirx = dx; diry = dy;
        } else if (dx != dirx || dy != diry) {
            // emit segment from start to prev
            RoadSegment rs;
            rs.a = center(sx, sy);
            rs.b = center(px, py);
            rs.kind = 0;
            segs.push_back(rs);

            sx = px; sy = py;
            dirx = dx; diry = dy;
        }

        px = cx; py = cy;
    }

    RoadSegment rs;
    rs.a = center(sx, sy);
    rs.b = center(px, py);
    rs.kind = 0;
    segs.push_back(rs);
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

    // --- settlement/roads layer inputs ---
    Map2D slopeN    = slope_normalized(height);
    U8Map rivers    = river_mask(flow, P.river_threshold);
    Map2D riverDist = distance_to_mask4(rivers);
    Map2D flood     = flood_risk01(P, height, flow, riverDist, biomes);

    // --- derived masks (useful even when the settlement layer is disabled) ---
    U8Map fertility, farmland, forest, resWood, resStone, resOre;
    build_fertility_resources(P, height, moisture, temp, biomes, slopeN, riverDist,
                              fertility, farmland, forest, resWood, resStone, resOre);

    SettlementSite start{};
    std::vector<SettlementSite> settlements;
    std::vector<RoadSegment> roads;
    U8Map roadMask(P.width, P.height, 0);

    if (P.enable_settlement_layer) {
        const auto candidates = score_site_candidates(P, height, slopeN, riverDist, flood, biomes,
                                                      fertility, resWood, resStone, resOre);

        if (!candidates.empty()) {
            start = to_site(candidates.front());
        } else {
            start.pos = Vec2{ P.width * 0.5f, P.height * 0.5f };
            start.score = 0.0f;
        }

        settlements = place_settlements(P, candidates, start, P.seed ^ 0x51A7E11u);

        if (P.build_roads) {
            std::vector<SettlementSite> nodes;
            nodes.reserve(1 + settlements.size());
            nodes.push_back(start);
            nodes.insert(nodes.end(), settlements.begin(), settlements.end());

            const auto edges = mst_edges_prim(nodes);
            for (auto [a, b] : edges) {
                const int ax = static_cast<int>(std::floor(nodes[static_cast<size_t>(a)].pos.x));
                const int ay = static_cast<int>(std::floor(nodes[static_cast<size_t>(a)].pos.y));
                const int bx = static_cast<int>(std::floor(nodes[static_cast<size_t>(b)].pos.x));
                const int by = static_cast<int>(std::floor(nodes[static_cast<size_t>(b)].pos.y));

                const auto path = astar_path_grid(P, ax, ay, bx, by, slopeN, flow, biomes);
                rasterize_path_to_roads(path, P.width, roadMask, roads);
            }
        }
    }

    Outputs out;
    out.height      = std::move(height);
    out.flow        = std::move(flow);
    out.moisture    = std::move(moisture);
    out.temperature = std::move(temp);
    out.biomes      = std::move(biomes);
    out.trees       = poisson_disk(out.biomes, P.scatter_radius, P.seed ^ 0xBADCAFEu);

    // new outputs
    out.start       = start;
    out.settlements = std::move(settlements);
    out.roads       = std::move(roads);
    out.road_mask   = std::move(roadMask);

    out.fertility   = std::move(fertility);
    out.farmland    = std::move(farmland);
    out.forest      = std::move(forest);
    out.res_wood    = std::move(resWood);
    out.res_stone   = std::move(resStone);
    out.res_ore     = std::move(resOre);

    return out;
}


} // namespace pg
