// src/worldgen/FlowMapGen.hpp
#pragma once

#include <cstdint>
#include <vector>

namespace cg
{
    struct FlowMap
    {
        int w = 0;
        int h = 0;

        // RGBA8:
        // - R,G store direction encoded from [-1..1] into [0..255]
        // - B stores normalized speed/strength [0..255]
        // - A = 255
        std::vector<std::uint8_t> rgba; // size = w*h*4
    };

    // Build a simple flow map from D8 directions and accumulation.
    // flowDir: per-cell D8 direction index [0..7], or 255 for "no direction".
    // flowAccum: positive scalar proxy for speed/discharge; will be normalized.
    FlowMap buildFlowMapFromD8(const std::vector<std::uint8_t>& flowDir,
                               const std::vector<float>& flowAccum,
                               int W,
                               int H);
} // namespace cg
