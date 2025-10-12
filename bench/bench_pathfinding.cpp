#include <benchmark/benchmark.h>
#include "colony/pathfinding/AStar.hpp"
#include "colony/pathfinding/JPS.hpp"
#include <random>
#include <filesystem>
#include <fstream>
#include <string>

using namespace colony::pf;

// Simple loader for Moving AI .map (ASCII)
// https://www.movingai.com/benchmarks/  (give a folder via --map=...)
static bool load_movingai_map(const std::filesystem::path& file, GridMap& out) {
    std::ifstream f(file);
    if (!f) return false;
    std::string line;
    int w=0,h=0;
    while (std::getline(f,line)) {
        if (line.rfind("type",0)==0) continue;
        if (line.rfind("height",0)==0) { h = std::stoi(line.substr(7)); continue; }
        if (line.rfind("width",0)==0)  { w = std::stoi(line.substr(6)); continue; }
        if (line=="map") break;
    }
    if (w<=0 || h<=0) return false;
    out = GridMap(w,h);
    for (int y=0;y<h;++y) {
        std::getline(f,line);
        for (int x=0;x<w;++x) {
            char c = line[x];
            // '.' and 'G' are ground; '@','T' etc are blocked
            const bool free = (c=='.' || c=='G' || c=='S' || c=='W');
            out.set_walkable(x,y, free ? 1 : 0);
        }
    }
    return true;
}

static GridMap make_random(int w,int h, double blocked, uint32_t seed=1337) {
    GridMap m(w,h);
    std::mt19937 rng(seed);
    std::bernoulli_distribution free_prob(1.0 - blocked);
    for (int y=0;y<h;++y) for (int x=0;x<w;++x) m.set_walkable(x,y, free_prob(rng) ? 1 : 0);
    return m;
}

static void bench_astar_random(benchmark::State& st) {
    const int w = static_cast<int>(st.range(0));
    GridMap m = make_random(w, w, 0.20);
    m.set_walkable(1,1,1); m.set_walkable(w-2,w-2,1);
    AStar solver(m);
    for (auto _ : st) {
        auto path = solver.find_path({1,1}, {w-2,w-2});
        benchmark::DoNotOptimize(path.length());
    }
}
BENCHMARK(bench_astar_random)->Arg(128)->Arg(256)->Arg(512);

static void bench_jps_random(benchmark::State& st) {
    const int w = static_cast<int>(st.range(0));
    GridMap m = make_random(w, w, 0.20);
    m.set_walkable(1,1,1); m.set_walkable(w-2,w-2,1);
    JPS solver(m);
    for (auto _ : st) {
        auto path = solver.find_path({1,1}, {w-2,w-2});
        benchmark::DoNotOptimize(path.length());
    }
}
BENCHMARK(bench_jps_random)->Arg(128)->Arg(256)->Arg(512);

BENCHMARK_MAIN();
