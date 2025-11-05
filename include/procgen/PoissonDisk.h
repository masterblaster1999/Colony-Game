#pragma once
#include <vector>
#include <random>
#include "Types.h"

namespace procgen {

// Bridson's Poisson-disk sampling in 2D (unit space). Scale by your world size.
std::vector<FV2> poisson_disk_2d(float radius, int k, uint32_t seed);

} // namespace procgen
