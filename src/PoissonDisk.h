#pragma once
#include <vector>
#include <functional>
#include <random>
#include <cstdint>

struct Float2 { float x, y; };

struct PoissonParams
{
    float minDist = 8.0f;   // minimum spacing (in "pixels" / world units)
    int   k = 30;           // attempts per active sample
    float width = 512.0f;   // domain width
    float height = 512.0f;  // domain height
};

// Bridson Poisson-disk sampler on [0,width) x [0,height).
// Optional 'accept' predicate can reject points (e.g., steep slope / water).
std::vector<Float2> PoissonSample(
    const PoissonParams& params,
    std::mt19937& rng,
    const std::function<bool(float x, float y)>& accept);
