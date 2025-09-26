#pragma once
#include <vector>
#include <cstdint>

namespace cg {

struct FlowMap {
    int w=0, h=0;
    // RGBA8; R,G store dir in [0..255] mapped from [-1..1]; B stores normalized speed [0..255]; A=255
    std::vector<uint8_t> rgba; // size = w*h*4
};

// Build a simple flow map from D8 directions and accumulation.
// accum is any positive scalar proxy for speed/discharge; it will be normalized.
FlowMap buildFlowMapFromD8(const std::vector<uint8_t>& flowDir,
                           const std::vector<float>&   flowAccum,
                           int W,int H);

} // namespace cg
