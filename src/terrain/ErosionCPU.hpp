#pragma once
#include "Heightfield.hpp"
#include "ErosionCommon.hpp"
#include "DeterministicRNG.hpp"
#include <vector>

namespace colony::terrain {

// Hydraulic erosion (height + water + sediment fields, "virtual pipes" 4-neighbour).
// Based on heightfield shallow-water style used by Mei–Decaudin–Hu and follow-ups. :contentReference[oaicite:3]{index=3}
void HydraulicErodeCPU(Heightfield& h, const HydraulicParams& p);

// Thermal erosion (talus angle relaxation). Classic heightmap scheme per Olsen. :contentReference[oaicite:4]{index=4}
void ThermalErodeCPU(Heightfield& h, const ThermalParams& p);

} // namespace colony::terrain
