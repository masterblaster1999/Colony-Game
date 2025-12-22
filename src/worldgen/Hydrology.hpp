// src/worldgen/Hydrology.hpp
#pragma once

#include "Climate.hpp"
#include "DomainWarp.hpp" // provides cg::HeightField

namespace cg
{
    // NOTE:
    // HeightField is defined in DomainWarp.hpp.
    // Do NOT define it again here (it caused cg::HeightField redefinition errors).

    struct HydroOutputs
    {
        HeightField precip;
        HeightField temperature;
        HeightField flowAccum;
        HeightField filled;
        HeightField carved;
        HeightField waterLevel;
    };

    // Fixed-signature API (kept simple and stable):
    HydroOutputs simulateHydrology(const HeightField& elev,
                                  const HeightField& precip,
                                  int iterations = 50);

    HeightField distanceToCoast(const HeightField& landmask);
} // namespace cg
