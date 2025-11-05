#pragma once
#include "Heightmap.h"

namespace procgen {

struct ThermalParams {
    float talus = 0.012f; // slope threshold
    float amount = 0.5f;  // proportion moved when above talus
    int iterations = 60;
};

void thermal_erosion(Heightmap& h, const ThermalParams& p);

} // namespace procgen
