#include "game/WorldSeed.h"
#include <cstdlib>             // std::getenv
#include <filesystem>
#include <fstream>
#include <string>
#include <chrono>
#include <random>

using namespace std;

namespace fs = std::filesystem;

namespace {
    optional<uint64_t> parseSeedIni(const fs::path& p) {
        if (!fs::exists(p)) return nullopt;
        ifstream f(p);
        string line;
        while (std::getline(f, line)) {
            // Trim minimal
            auto pos = line.find("seed=");
            if (pos == string::npos) continue;
            const char* s = line.c_str() + pos + 5;
            char* end = nullptr;
            unsigned long long v = strtoull(s, &end, 10);
            if (end != s && v > 0ull) return static_cast<uint64_t>(v);
        }
        return nullopt;
    }

    optional<uint64_t> seedFromEnv() {
        if (const char* e = std::getenv("COLONY_SEED")) {
            char* end = nullptr;
            unsigned long long v = strtoull(e, &end, 10);
            if (end != e && v > 0ull) return static_cast<uint64_t>(v);
        }
        return nullopt;
    }

    fs::path localConfigPath() {
        // Avoid extra Win libs: use LOCALAPPDATA env
        fs::path base;
        if (const char* e = std::getenv("LOCALAPPDATA")) base = e;
        else base = fs::temp_directory_path(); // fallback
        fs::path dir = base / "ColonyGame";
        std::error_code ec;
        fs::create_directories(dir, ec);
        return dir / "config.ini";
    }
}

namespace worldseed {

uint64_t loadOrDefault() {
    if (auto e = seedFromEnv()) return *e;

    // Project default (relative to working dir)
    if (auto d = parseSeedIni(fs::path("res") / "config" / "default.ini")) return *d;

    // User last-used
    if (auto u = parseSeedIni(localConfigPath())) return *u;

    return kDefaultSeed;
}

void saveLastUsed(uint64_t seed) {
    auto p = localConfigPath();
    ofstream f(p, ios::trunc);
    f << "seed=" << seed << "\n";
}

uint64_t randomSeed() {
    // Combine rd + time for high entropy, but deterministic across same inputs is not required here.
    std::random_device rd;
    uint64_t s = (static_cast<uint64_t>(rd()) << 32) ^ static_cast<uint64_t>(rd());
    uint64_t t = static_cast<uint64_t>(
        chrono::high_resolution_clock::now().time_since_epoch().count()
    );
    // Mix to avalanche
    return rnd::mix(s, t);
}

Streams derive(uint64_t root) {
    Streams out{};
    // Namespace constants to keep streams stable if you insert new ones later.
    constexpr uint64_t N_TERRAIN = 0x01D1CEAA1ull;
    constexpr uint64_t N_BIOME   = 0x01B10ME001ull;
    constexpr uint64_t N_SCATTER = 0x05CA773Rull;
    constexpr uint64_t N_PATHING = 0x0PA7H1N61ull;
    constexpr uint64_t N_LOOT    = 0x10A0AD11ull;
    constexpr uint64_t N_AUDIO   = 0x0AUD10A1ull;

    out.terrain = rnd::mix(root, N_TERRAIN);
    out.biome   = rnd::mix(root, N_BIOME);
    out.scatter = rnd::mix(root, N_SCATTER);
    out.pathing = rnd::mix(root, N_PATHING);
    out.loot    = rnd::mix(root, N_LOOT);
    out.audio   = rnd::mix(root, N_AUDIO);
    return out;
}

} // namespace worldseed
