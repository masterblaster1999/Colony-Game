// pathfinding/procgen/Cellular.h
#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>
#include <random>

namespace colony::pathfinding::procgen {

inline size_t idx(int x, int y, int w) { return static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x); }

// Fill a binary obstacle mask by density in [0..1]
inline void randomMask(std::vector<uint8_t>& mask, int w, int h, float density, uint32_t seed) {
    std::mt19937 rng(seed);
    std::bernoulli_distribution bern(std::clamp(density, 0.0f, 1.0f));
    mask.assign(static_cast<size_t>(w) * h, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            mask[idx(x, y, w)] = bern(rng) ? 1u : 0u;
}

inline int countWallNeighbors(const std::vector<uint8_t>& m, int w, int h, int x, int y) {
    int c = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx, ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) { c++; continue; } // treat out-of-bounds as wall
            c += (m[idx(nx, ny, w)] != 0);
        }
    return c;
}

// One cellular automata step (common rule-of-thumb settings create nice caves).
inline void cellularStep(std::vector<uint8_t>& m, int w, int h, int birthLimit = 4, int deathLimit = 3) {
    std::vector<uint8_t> next(m.size());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int n = countWallNeighbors(m, w, h, x, y);
            if (m[idx(x, y, w)]) {
                next[idx(x, y, w)] = (n >= deathLimit) ? 1u : 0u;
            } else {
                next[idx(x, y, w)] = (n > birthLimit) ? 1u : 0u;
            }
        }
    }
    m.swap(next);
}

} // namespace colony::pathfinding::procgen
