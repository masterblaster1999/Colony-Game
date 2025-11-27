#pragma once
#include <vector>
#include <cstdint>
#include "Types.h"

namespace procgen {

// Bridson's fast Poisson-disk sampling (2D) over a width x height grid coordinates.
// radius is in "cells". Returns integer pixel coords.
std::vector<Vec2> poissonDisk(int width, int height, float radius, uint32_t seed, int k = 30);

} // namespace procgen
