#pragma once
#include <cstdint>

namespace colony::terrain {

struct HydraulicParams {
    int   iterations           = 250;
    float rainfall             = 0.01f;  // water added each step
    float evaporation          = 0.02f;  // water removed each step
    float gravity              = 9.81f;  // influence on flow
    float pipeK                = 0.5f;   // virtual pipes coefficient
    float sedimentCapacityK    = 4.0f;   // capacity proportionality
    float minSlope             = 0.01f;  // minimum slope to carry sediment
    float dissolveRate         = 0.30f;  // erosion rate when capacity not met
    float depositRate          = 0.30f;  // deposition rate when over capacity
    float friction             = 0.05f;  // damp flow velocity proxy
    float dt                   = 1.0f;   // simulation step
    uint64_t seed              = 0xC01OnyULL;
};

struct ThermalParams {
    int   iterations = 60;
    float talus      = 0.02f; // threshold slope
    float carry      = 0.5f;  // fraction of (slope - talus) to move
};

} // namespace colony::terrain
