#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include "SeededRng.hpp"

namespace pcg {

struct Tile {
    std::string name;
    uint8_t north, east, south, west; // bitmask connectors, e.g., 1=wall,2=door,4=window...
    float weight = 1.0f;
};

struct WfcRules {
    std::vector<Tile> tiles;
};

struct WfcGrid {
    int W, H;
    std::vector<std::vector<int>> possibilities; // indices into tiles
    std::vector<int> collapsed; // -1 or tile index
};

WfcGrid wfc_generate(const WfcRules& rules, int W, int H, Rng& rng, int maxSteps = 100000);

} // namespace pcg
