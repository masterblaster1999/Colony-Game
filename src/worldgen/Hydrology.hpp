#pragma once

// Hydrology stage API (Windows/MSVC-safe).
// Keep this header stable: other modules (StagesConfig, worldgen stages, tests) include it.

#include <cstdint>

// Important: ClimateParams is expected to be visible to any TU that includes Hydrology.hpp
// (StagesConfig.hpp stores it by value).
#include "Climate.hpp"

// HeightField lives in/behind DomainWarp.hpp in this project layout.
#include "DomainWarp.hpp"

namespace cg
{
    // Parameters for hydrology. Some of these are configured by StagesConfig.cpp.
    // Even if the current hydrology implementation only uses `iterations`,
    // keeping the fields here prevents "missing member" compile failures and
    // lets you evolve the algorithm later without churn.
    struct HydroParams
    {
        // Primary knob used by existing code paths.
        int iterations = 50;

        // Used by worldgen StagesConfig (stream-power / incision tuning).
        float incisionExpM = 0.5f;
        float incisionExpN = 1.0f;

        // Optional smoothing passes on derived fields (if/when implemented).
        int smoothIterations = 0;

        // Safe extension knobs (defaults keep behavior identical).
        float seaLevel = 0.0f;
        float rainScale = 1.0f;
        float evaporation = 0.0f;
    };

    struct HydroOutputs
    {
        HeightField precip;
        HeightField temperature;

        // Required by Hydrology.cpp (your build error says this member is missing).
        HeightField flowAccum;

        HeightField filled;
        HeightField carved;
        HeightField waterLevel;
    };

    // Original/legacy signature (widely referenced).
    [[nodiscard]] HydroOutputs simulateHydrology(const HeightField& elev,
                                                 const HeightField& precip,
                                                 int iterations = 50);

    // Convenience overload: allows StagesConfig to pass HydroParams without forcing
    // a large refactor. This keeps linking safe even if only iterations is used.
    [[nodiscard]] inline HydroOutputs simulateHydrology(const HeightField& elev,
                                                        const HeightField& precip,
                                                        const HydroParams& params)
    {
        return simulateHydrology(elev, precip, params.iterations);
    }

    [[nodiscard]] HeightField distanceToCoast(const HeightField& landmask);
} // namespace cg
