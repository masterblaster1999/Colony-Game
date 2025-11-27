#pragma once
#include <vector>
#include <cstdint>

namespace procgen {

struct ErosionParams {
    int   dropletCount = 50000;
    int   maxSteps = 30;
    float inertia = 0.05f;
    float sedimentCapacityFactor = 4.0f;
    float minSlope = 0.01f;
    float depositSpeed = 0.1f;
    float erodeSpeed = 0.3f;
    float evaporateSpeed = 0.01f;
    float gravity = 4.0f;
    float initialWater = 1.0f;
    float initialSpeed = 1.0f;
};

void applyHydraulicErosion(std::vector<float>& height, int w, int h,
                           uint32_t seed, const ErosionParams& p);

} // namespace procgen
