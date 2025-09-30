#include <cstdio>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>

#include "pcg/SeededRng.hpp"
#include "pcg/TerrainGen.hpp"
#include "pcg/Poisson.hpp"
#include "pcg/WfcLayout.hpp"
#include "pcg/Weather.hpp"

using namespace pcg;

static void write_ppm(const std::string& path, const std::vector<float>& f, int W, int H, float lo, float hi) {
    std::ofstream o(path, std::ios::binary);
    o << "P6\n" << W << " " << H << "\n255\n";
    for (int i=0;i<W*H;++i) {
        float v = (f[i]-lo)/(hi-lo); v = std::clamp(v, 0.0f, 1.0f);
        unsigned char c = (unsigned char)(v*255);
        o.put(c).put(c).put(c);
    }
}

int main() {
    uint64_t worldSeed = 123456789ull;
    ChunkCoord cc{0,0};
    TerrainParams tp;
    tp.size = 256;
    tp.elevationAmp = 120.0f;

    auto chunk = generate_terrain(worldSeed, cc, /*cellSize*/ 2.0f, tp);

    // Export quicklooks
    write_ppm("height.ppm",   chunk.height,   chunk.size, chunk.size, -30.0f, 150.0f);
    write_ppm("flow.ppm",     chunk.flow,     chunk.size, chunk.size, 0.0f,   200.0f);

    // Poisson samples for resources (e.g., ruins)
    auto rng = make_rng(worldSeed, cc.cx, cc.cy, "poi");
    auto pts = poisson_disk(float(chunk.size), float(chunk.size), /*minDist*/ 12.0f, /*k*/ 30, rng);
    printf("Generated %zu POIs via Poisson-disk\n", pts.size());

    // WFC layout 16x10 with 3 simple tiles
    WfcRules rules;
    rules.tiles = {
        {"Wall",   /*N*/1, /*E*/1, /*S*/1, /*W*/1, 1.0f},
        {"Corr",   /*N*/2, /*E*/2, /*S*/2, /*W*/2, 3.5f}, // 2 = door/pass
        {"Room",   /*N*/2, /*E*/2, /*S*/2, /*W*/2, 1.0f}
    };
    auto grid = wfc_generate(rules, 16, 10, rng);
    printf("WFC collapsed cells: %d / %d\n", (int)std::count_if(grid.collapsed.begin(),grid.collapsed.end(),[](int v){return v>=0;}), 16*10);

    // Weather
    WeatherSystem weather(worldSeed);
    for (int d=0; d<5; ++d) { weather.step(); printf("Weather day %d: %d\n", d, (int)weather.state); }

    printf("Wrote: height.ppm, flow.ppm\n");
    return 0;
}
