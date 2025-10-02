#pragma once
#include <vector>

namespace pcg {

// Simple 8-neighbour D8 flow accumulation and channel carving
void compute_flow_accumulation(const std::vector<float>& height, int W, int H, std::vector<float>& outFlow);

// Carve channels where flow exceeds threshold; mark river cells
void carve_rivers(std::vector<float>& height,
                  const std::vector<float>& flow,
                  int W, int H, [[maybe_unused]] float cellSize,
                  float flowThreshold,
                  std::vector<uint8_t>& outRiverMask);

} // namespace pcg
