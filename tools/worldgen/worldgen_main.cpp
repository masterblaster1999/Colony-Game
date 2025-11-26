#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "u16_raster.hpp"

using std::string;
namespace fs = std::filesystem;

// --- utilities ---------------------------------------------------------------

static bool ensure_dir(const fs::path& p) {
    std::error_code ec;
    if (fs::exists(p, ec)) return true;
    return fs::create_directories(p, ec);
}

static bool write_text(const fs::path& p, const std::string& s) {
    if (!ensure_dir(p.parent_path())) return false;
    std::ofstream os(p, std::ios::binary);
    if (!os) return false;
    os << s;
    return os.good();
}

static std::map<string, string> parse_kv(int argc, char** argv) {
    std::map<string,string> kv;
    for (int i=1;i<argc;++i) {
        string a = argv[i];
        auto eq = a.find('=');
        if (eq != string::npos) {
            kv[a.substr(0,eq)] = a.substr(eq+1);
        } else if (a.rfind("--",0)==0 && i+1<argc) {
            kv[a] = argv[++i];
        } else {
            kv[a] = "";
        }
    }
    return kv;
}

static inline float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

// --- generators --------------------------------------------------------------

static bool gen_meta(const std::map<string,string>& kv) {
    const string outDir = kv.count("--out") ? kv.at("--out") : "data/worlds/12345";
    const int seed = kv.count("--seed") ? std::stoi(kv.at("--seed")) : 12345;
    const int w = kv.count("--width") ? std::stoi(kv.at("--width")) : 4096;
    const int h = kv.count("--height") ? std::stoi(kv.at("--height")) : 4096;
    const float mpt = kv.count("--meters-per-texel") ? std::stof(kv.at("--meters-per-texel")) : 1.0f;

    fs::path p = fs::path(outDir) / "world.meta.json";
    std::string j = "{\n"
        "  \"version\": 1,\n"
        "  \"seed\": " + std::to_string(seed) + ",\n"
        "  \"size\": { \"width\": " + std::to_string(w) + ", \"height\": " + std::to_string(h) + " },\n"
        "  \"tileSizeMeters\": " + std::to_string(mpt) + ",\n"
        "  \"scales\": { \"height\": { \"min\": -50.0, \"max\": 350.0 }, \"temperature\": { \"min\": -10.0, \"max\": 40.0 } },\n"
        "  \"subseeds\": { \"terrain\": 1111, \"structures\": 2222, \"factions\": 3333, \"events\": 4444 }\n"
        "}\n";
    return write_text(p, j);
}

static bool gen_rivers(const std::map<string,string>& kv) {
    const string outDir = kv.count("--out") ? kv.at("--out") : "data/worlds/12345/networks";
    fs::path p = fs::path(outDir) / "rivers.graph.json";
    std::string j =
        "{\n"
        "  \"version\": 1,\n"
        "  \"crs\": \"world-pixels\",\n"
        "  \"nodes\": [\n"
        "    { \"id\": 0, \"x\": 256.0, \"y\": 3800.0, \"flow\": 1.2 },\n"
        "    { \"id\": 1, \"x\": 800.0, \"y\": 3000.0, \"flow\": 1.8 },\n"
        "    { \"id\": 2, \"x\": 1600.0, \"y\": 2400.0, \"flow\": 2.5 }\n"
        "  ],\n"
        "  \"edges\": [\n"
        "    { \"a\": 0, \"b\": 1, \"width\": 1.3, \"type\": \"river\" },\n"
        "    { \"a\": 1, \"b\": 2, \"width\": 1.9, \"type\": \"river\" }\n"
        "  ]\n"
        "}\n";
    return write_text(p, j);
}

static bool gen_recipes(const std::map<string,string>& kv) {
    const string outDir = kv.count("--out") ? kv.at("--out") : "data/worlds/12345/rules";
    fs::path p = fs::path(outDir) / "recipes.json";
    std::string j =
        "{\n"
        "  \"version\": 1,\n"
        "  \"recipes\": [\n"
        "    {\"id\":\"plank\",\"inputs\":{\"log\":2},\"outputs\":{\"plank\":4},\"timeSec\":6.0,\"station\":\"sawhorse\"},\n"
        "    {\"id\":\"iron_ingot\",\"inputs\":{\"iron_ore\":2,\"charcoal\":1},\"outputs\":{\"iron_ingot\":1},\"timeSec\":12.0,\"station\":\"smelter\",\"unlockedBy\":[\"smelting\"]}\n"
        "  ]\n"
        "}\n";
    return write_text(p, j);
}

// cost-slope: from heightmap.r16 -> nav/cost_slope.u16
static bool gen_cost_slope(const std::map<string,string>& kv) {
    using namespace wg;
    const string inPath  = kv.count("--in") ? kv.at("--in") : "data/worlds/12345/heightmap.r16";
    const string outPath = kv.count("--out") ? kv.at("--out") : "data/worlds/12345/nav/cost_slope.u16";
    const uint32_t W = kv.count("--width") ? static_cast<uint32_t>(std::stoul(kv.at("--width"))) : 4096;
    const uint32_t H = kv.count("--height") ? static_cast<uint32_t>(std::stoul(kv.at("--height"))) : 4096;
    const float mpt = kv.count("--meters-per-texel") ? std::stof(kv.at("--meters-per-texel")) : 1.0f;
    const float maxAngleDeg = kv.count("--max-angle") ? std::stof(kv.at("--max-angle")) : 45.0f;

    U16Raster height{};
    if (!read_u16_raw(inPath, W, H, height)) {
        std::cerr << "Failed to read heightmap: " << inPath << "\n";
        return false;
    }

    U16Raster cost{W, H, std::vector<uint16_t>(size_t(W)*H, 0)};
    const float toDeg = 180.0f / 3.1415926535f;

    auto H_at = [&](int x, int y)->float {
        x = std::clamp(x, 0, int(W)-1);
        y = std::clamp(y, 0, int(H)-1);
        return float(height.pixels[size_t(y)*W + x]);
    };

    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            // central differences
            float dzdx = (H_at(int(x)+1,int(y)) - H_at(int(x)-1,int(y))) / (2.0f * mpt);
            float dzdy = (H_at(int(x),int(y)+1) - H_at(int(x),int(y)-1)) / (2.0f * mpt);
            float slopeTan = std::sqrt(dzdx*dzdx + dzdy*dzdy); // |grad|
            float angleDeg = std::atan(slopeTan) * toDeg;

            // Map angle -> cost [0..65535]; clamp; ensure nonzero traversable
            float t = clampf(angleDeg / maxAngleDeg, 0.0f, 1.0f);
            uint16_t c = static_cast<uint16_t>(std::round(t * 65535.0f));
            if (c == 0) c = 1; // avoid zero-cost paths
            cost.pixels[size_t(y)*W + x] = c;
        }
    }

    if (!ensure_dir(fs::path(outPath).parent_path())) return false;
    return write_u16_raw(outPath, cost);
}

// --- entry -------------------------------------------------------------------

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr <<
          "Usage:\n"
          "  worldgen meta --out <dir> --seed <n> --width <w> --height <h> --meters-per-texel <m>\n"
          "  worldgen rivers --out <dir>\n"
          "  worldgen recipes --out <dir>\n"
          "  worldgen cost-slope --in <heightmap.r16> --out <cost_slope.u16> --width <w> --height <h> --meters-per-texel <m> --max-angle <deg>\n";
        return 1;
    }

    auto kv = parse_kv(argc, argv);
    string cmd = argv[1];

    bool ok = false;
    if (cmd == "meta") ok = gen_meta(kv);
    else if (cmd == "rivers") ok = gen_rivers(kv);
    else if (cmd == "recipes") ok = gen_recipes(kv);
    else if (cmd == "cost-slope") ok = gen_cost_slope(kv);

    if (!ok) { std::cerr << "Failed.\n"; return 2; }
    return 0;
}
