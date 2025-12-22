// src/worldgen/Hydrology.hpp
#pragma once

// Hydrology stage public API (implementation lives in Hydrology.cpp).
//
// IMPORTANT DESIGN NOTE (unity-build safe):
// - HeightField is defined in HeightField.hpp (a small shared data header).
// - This header intentionally does NOT include other worldgen algorithm headers
//   (e.g., DomainWarp.hpp) to avoid transitive include bloat and unity-build ODR
//   collision risks.

#include "HeightField.hpp"

#include <cstdint>

namespace cg
{
    // Outputs produced by the hydrology simulation.
    struct HydroOutputs
    {
        HeightField precip;       // precipitation field (input or derived)
        HeightField temperature;  // optional (may be empty if not computed)
        HeightField filled;       // filled terrain (lakes)
        HeightField carved;       // carved terrain (rivers)
        HeightField waterLevel;   // water surface level

        [[nodiscard]] bool valid() const noexcept
        {
            return (filled.w > 0) && (filled.h > 0);
        }

        void clear() noexcept
        {
            precip      = {};
            temperature = {};
            filled      = {};
            carved      = {};
            waterLevel  = {};
        }
    };

    // Parameters for the hydrology simulation.
    struct HydroParams
    {
        float seaLevel            = 0.0f;   // absolute sea level in same units as height
        float precipitationScale  = 1.0f;   // scales precip input to "water amount"
        float evaporation         = 0.02f;  // fraction removed per iteration
        float erosionRate         = 0.25f;  // carving strength per unit flow
        float depositionRate      = 0.05f;  // deposition where flow slows
        int   iterations          = 32;     // number of relaxation/transport passes
        bool  carveBelowSea       = false;  // allow carving below sea level
    };

    // Run a simple heightfield hydrology pass.
    // Requirements:
    // - heightIn and precipIn must be same resolution.
    HydroOutputs simulateHydrology(const HeightField& heightIn,
                                  const HeightField& precipIn,
                                  const HydroParams& params);

    // Compute a distance-to-coast field for a given sea level.
    // Output units are in "cells" (you can scale externally).
    HeightField distanceToCoast(const HeightField& heightIn, float seaLevel);

} // namespace cg
