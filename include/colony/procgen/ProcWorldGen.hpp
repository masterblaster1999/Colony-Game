#pragma once
// Single-file procedural generator for Colony-Game (Windows-only build).
// Public domain / CC0-style intent by author of this contribution.

#include <vector>
#include <array>
#include <cstdint>
#include <random>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <fstream>

namespace colony::procgen {

struct Vec2i { int x=0, y=0; };
static inline float clamp01(float v){ return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
static inline float lerp(float a, float b, float t){ return a + (b - a) * t; }

enum class Tile : uint8_t {
    DeepWater, ShallowWater, Beach,
    Grassland, Forest, Jungle, Savanna, Desert,
    Hills, Mountain, Snow,
    River  // overlay marker; base tile remains
};

struct Resource {
    enum class Kind : uint8_t { Wood, Stone, Metal, Food, Crystal };
    int x=0, y=0; Kind kind=Kind::Wood;
};

struct Map {
    int width=0, height=0;
    std::vector<Tile> tiles;            // size = w*h
    std::vector<uint8_t> riverMask;     // 0/1 per cell
    std::vector<float> height;          // 0..1
    std::vector<float> moisture;        // 0..1
    std::vector<float> temperature;     // 0..1
    std::vector<Resource> resources;

    inline int idx(int x, int y) const { return y*width + x; }
    inline bool inBounds(int x,int y) const { return x>=0 && y>=0 && x<width && y<height; }
    Tile getTile(int x,int y) const { return tiles[idx(x,y)]; }
    bool isLand(int x,int y) const {
        Tile t = tiles[idx(x,y)];
        return t!=Tile::DeepWater && t!=Tile::ShallowWater && t!=Tile::Beach;
    }
};

struct Options {
    int width  = 256;
    int height = 256;
    uint32_t seed = 1337u;

    // Terrain controls
    int    octaves      = 5;
    float  lacunarity   = 2.0f;
    float  persistence  = 0.5f;
    float  frequency    = 1.0f / 128.0f;

    // Sea / coast
    float  seaLevel     = 0.47f;
    float  beachWidth   = 0.02f;

    // Rivers
    int    numRivers    = 12;
    int    maxRiverLen  = 4000;
    float  riverMinSlope= 0.0005f;

    // Resources
    int    resourceAttempts = 3000;
    float  resourceMinDist  = 6.5f;

    // Continent falloff shaping
    float  islandFalloff   = 0.35f;   // 0..1; higher => more coast
};

// --- lightweight Perlin-style gradient noise -------------------------------
class Perlin2D {
public:
    explicit Perlin2D(uint32_t seed = 1337) { reseed(seed); }

    void reseed(uint32_t seed) {
        std::mt19937 rng(seed);
        std::vector<int> p(256);
        for (int i = 0; i < 256; ++i) p[i] = i;
        std::shuffle(p.begin(), p.end(), rng);
        for (int i = 0; i < 512; ++i) perm[i] = p[i & 255];
    }

    // returns ~[-1,1]
    float noise(float x, float y) const {
        int X = fastFloor(x) & 255;
        int Y = fastFloor(y) & 255;
        x -= std::floor(x);
        y -= std::floor(y);
        float u = fade(x);
        float v = fade(y);

        int aa = perm[perm[X] + Y];
        int ab = perm[perm[X] + Y + 1];
        int ba = perm[perm[X + 1] + Y];
        int bb = perm[perm[X + 1] + Y + 1];

        float grad_aa = grad(aa, x,     y    );
        float grad_ba = grad(ba, x-1.f, y    );
        float grad_ab = grad(ab, x,     y-1.f);
        float grad_bb = grad(bb, x-1.f, y-1.f);

        float lerp_x1 = lerp(grad_aa, grad_ba, u);
        float lerp_x2 = lerp(grad_ab, grad_bb, u);
        return lerp(lerp_x1, lerp_x2, v);
    }

    // fractal brownian motion
    float fbm(float x, float y, int octaves, float lac, float pers) const {
        float amp = 1.f, freq = 1.f, sum = 0.f, norm = 0.f;
        for(int i=0;i<octaves;i++){
            sum  += amp * noise(x*freq, y*freq);
            norm += amp;
            amp  *= pers;
            freq *= lac;
        }
        return sum / (norm + 1e-6f); // ~[-1,1]
    }

private:
    int perm[512]{};

    static inline float fade(float t){ return t*t*t*(t*(t*6.f - 15.f) + 10.f); }
    static inline int fastFloor(float v){ return (v>=0)?(int)v:((int)v-1); }

    static inline float grad(int h, float x, float y){
        // 8 gradient directions
        switch(h & 7){
            case 0: return  x + y;
            case 1: return  x - y;
            case 2: return -x + y;
            case 3: return -x - y;
            case 4: return  x;
            case 5: return -x;
            case 6: return  y;
            default:return -y;
        }
    }
};

// --- main generator ---------------------------------------------------------
class Generator {
public:
    explicit Generator(Options opt = {}) : o(opt), perlin(opt.seed) {}

    Map generate() {
        Map m;
        m.width  = o.width;
        m.height = o.height;
        const int N = m.width * m.height;
        m.tiles.resize(N);
        m.riverMask.assign(N, 0);
        m.height.resize(N);
        m.moisture.resize(N);
        m.temperature.resize(N);

        // Precompute fields
        const float fx = o.frequency;
        for (int y=0; y<m.height; ++y) {
            for (int x=0; x<m.width; ++x) {
                const float nx = (float)x / (float)m.width;
                const float ny = (float)y / (float)m.height;

                float h = perlin.fbm((x+13)*fx, (y+7)*fx, o.octaves, o.lacunarity, o.persistence);
                float ridge = 1.f - std::fabs(perlin.fbm((x-100)*fx*0.8f, (y+25)*fx*0.8f, o.octaves, o.lacunarity, 0.5f));
                h = 0.70f*h + 0.30f*ridge;                // blend classic and ridged
                h = (h*0.5f + 0.5f);                      // normalize to 0..1

                // radial falloff to make continents
                const float cx = nx*2.f-1.f, cy = ny*2.f-1.f;
                float r = std::sqrt(cx*cx + cy*cy);
                float fall = 1.f - clamp01((r - o.islandFalloff) / (1.f - o.islandFalloff));
                h = clamp01(h * 0.75f + 0.25f*fall);

                float moist = perlin.fbm((x-321)*fx*0.9f, (y+222)*fx*0.9f, o.octaves, o.lacunarity, 0.55f);
                moist = clamp01(moist*0.5f + 0.5f);

                float lat = 1.f - std::fabs(ny*2.f - 1.f); // equator=1, poles=0
                float temp = clamp01(0.65f*lat + 0.35f*(perlin.fbm((x+555)*fx*0.7f,(y-987)*fx*0.7f,o.octaves,1.9f,0.5f)*0.5f + 0.5f));

                const int i = m.idx(x,y);
                m.height[i] = h;
                m.moisture[i] = moist;
                m.temperature[i] = temp;
            }
        }

        // Initial tile classification (no rivers yet)
        for (int y=0; y<m.height; ++y) for (int x=0; x<m.width; ++x) {
            const int i = m.idx(x,y);
            const float h = m.height[i];
            const float t = m.temperature[i];
            const float w = m.moisture[i];

            if (h < o.seaLevel - o.beachWidth*2.f)           m.tiles[i] = Tile::DeepWater;
            else if (h < o.seaLevel)                         m.tiles[i] = Tile::ShallowWater;
            else if (h < o.seaLevel + o.beachWidth)          m.tiles[i] = Tile::Beach;
            else {
                // biome by temp/moisture/height
                if      (h > 0.80f)                          m.tiles[i] = (t < 0.35f ? Tile::Snow : Tile::Mountain);
                else if (h > 0.70f)                          m.tiles[i] = Tile::Hills;
                else {
                    if      (w < 0.25f && t > 0.65f)         m.tiles[i] = Tile::Desert;
                    else if (w > 0.70f && t > 0.60f)         m.tiles[i] = Tile::Jungle;
                    else if (w > 0.55f)                      m.tiles[i] = Tile::Forest;
                    else if (t > 0.60f && w > 0.35f)         m.tiles[i] = Tile::Savanna;
                    else                                     m.tiles[i] = Tile::Grassland;
                }
            }
        }

        // Rivers
        carveRivers(m);

        // Resources
        scatterResources(m);

        return m;
    }

private:
    Options o;
    Perlin2D perlin;

    struct RiverNode { int x,y; };

    void carveRivers(Map& m) {
        // pick N high points as sources
        std::vector<Vec2i> candidates;
        candidates.reserve(m.width*m.height);
        for (int y=2; y<m.height-2; ++y)
        for (int x=2; x<m.width-2;  ++x) {
            const float h = m.height[m.idx(x,y)];
            if (h > 0.72f) candidates.push_back({x,y});
        }
        std::mt19937 rng(o.seed ^ 0x9E3779B9u);
        std::shuffle(candidates.begin(), candidates.end(), rng);

        int spawned = 0;
        for (size_t c=0; c<candidates.size() && spawned<o.numRivers; ++c) {
            auto [sx, sy] = candidates[c];
            if (!m.isLand(sx,sy)) continue;
            // simple steepest-descent path
            int x = sx, y = sy;
            int steps = 0;
            std::array<int, 8> dx{ -1,0,1,-1,1,-1,0,1 };
            std::array<int, 8> dy{ -1,-1,-1,0,0,1,1,1 };
            float prevH = m.height[m.idx(x,y)];

            while (steps++ < o.maxRiverLen) {
                // Stop if we reached coast
                Tile t = m.tiles[m.idx(x,y)];
                if (t==Tile::ShallowWater || t==Tile::Beach || t==Tile::DeepWater) break;

                m.riverMask[m.idx(x,y)] = 1; // mark
                // Next step: choose neighbor with lowest height (bias toward downhill)
                float bestH = prevH;
                int bestDir = -1;
                for (int k=0;k<8;k++){
                    int nx = x+dx[k], ny = y+dy[k];
                    if (!m.inBounds(nx,ny)) continue;
                    float nh = m.height[m.idx(nx,ny)];
                    if (nh < bestH - o.riverMinSlope) { bestH = nh; bestDir = k; }
                }
                if (bestDir < 0) break; // local minima; river ends in lake
                x += dx[bestDir]; y += dy[bestDir];
                prevH = bestH;
            }
            // Lay river overlay on base land tiles
            for (int yy = std::max(0, sy-1); yy<std::min(m.height, sy+1); ++yy) (void)yy;
            spawned++;
        }

        // Convert river mask into overlay tiles (non-destructive to base water)
        for (int i=0;i<(int)m.tiles.size();++i) {
            if (m.riverMask[i] && m.tiles[i] != Tile::DeepWater && m.tiles[i] != Tile::ShallowWater)
                m.tiles[i] = Tile::River;
        }
    }

    bool farFromExisting(const std::vector<Resource>& out, float minDist, int x, int y) {
        const float r2 = minDist*minDist;
        for (const auto& r: out){
            const float dx = float(r.x - x), dy = float(r.y - y);
            if (dx*dx + dy*dy < r2) return false;
        }
        return true;
    }

    void scatterResources(Map& m) {
        std::mt19937 rng(o.seed ^ 0xA5A5A5A5u);
        std::uniform_int_distribution<int> distX(0, m.width-1);
        std::uniform_int_distribution<int> distY(0, m.height-1);
        std::uniform_real_distribution<float> coin(0.f, 1.f);

        m.resources.clear(); m.resources.reserve(o.resourceAttempts/4);

        auto isGood = [&](int x,int y)->bool {
            Tile t = m.tiles[m.idx(x,y)];
            if (!m.isLand(x,y)) return false;
            if (t==Tile::Desert && coin(rng) < 0.80f) return false;
            return true;
        };

        for (int n=0;n<o.resourceAttempts;n++){
            int x = distX(rng), y = distY(rng);
            if (!isGood(x,y)) continue;
            if (!farFromExisting(m.resources, o.resourceMinDist, x, y)) continue;

            // choose kind by biome
            Tile t = m.tiles[m.idx(x,y)];
            Resource::Kind k = Resource::Kind::Stone;
            if (t==Tile::Forest || t==Tile::Jungle)      k = Resource::Kind::Wood;
            else if (t==Tile::Hills || t==Tile::Mountain)k = (coin(rng)<0.7f?Resource::Kind::Metal:Resource::Kind::Stone);
            else if (t==Tile::Grassland || t==Tile::Savanna) k = Resource::Kind::Food;
            else if (t==Tile::Snow) k = Resource::Kind::Stone;
            if (m.riverMask[m.idx(x,y)] && coin(rng)<0.15f)  k = Resource::Kind::Crystal;

            m.resources.push_back({x,y,k});
        }
    }

public:
    // Optional debug: write a simple PPM preview (no external deps)
    static bool writeDebugPPM(const Map& m, const std::string& file) {
        std::ofstream f(file, std::ios::binary);
        if (!f) return false;
        f << "P6\n" << m.width << " " << m.height << "\n255\n";
        auto put = [&](uint8_t r,uint8_t g,uint8_t b){ f.put((char)r); f.put((char)g); f.put((char)b); };
        for (int y=0;y<m.height;++y){
            for (int x=0;x<m.width;++x){
                const int i = m.idx(x,y);
                auto t = m.tiles[i];
                uint8_t r=0,g=0,b=0;
                switch(t){
                    case Tile::DeepWater:    r=5; g=15; b=60; break;
                    case Tile::ShallowWater: r=20; g=60; b=120; break;
                    case Tile::Beach:        r=210; g=190; b=120; break;
                    case Tile::Grassland:    r=60; g=140; b=60; break;
                    case Tile::Forest:       r=25; g=100; b=30; break;
                    case Tile::Jungle:       r=20; g=120; b=20; break;
                    case Tile::Savanna:      r=150; g=170; b=60; break;
                    case Tile::Desert:       r=210; g=180; b=90; break;
                    case Tile::Hills:        r=120; g=120; b=120; break;
                    case Tile::Mountain:     r=100; g=100; b=100; break;
                    case Tile::Snow:         r=230; g=230; b=230; break;
                    case Tile::River:        r=30; g=110; b=200; break;
                }
                put(r,g,b);
            }
        }
        return true;
    }
};

} // namespace colony::procgen
