// tests/worldgen/test_math_and_noise.cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>   // provided by vcpkg as <doctest/doctest.h>  (header-only)

// Pull in the production implementation so we can reach the TU-internal static fbm2D.
// This compiles the worldgen code into the *test* executable only.
#include "../../src/worldgen/WorldGen.cpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

using namespace colony::worldgen;

static inline float hermite_smoothstep(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// ---------------------- lerp ----------------------
TEST_CASE("lerp: identities, midpoint, monotonicity") {
    CHECK(lerp(1.0f, 5.0f, 0.0f) == doctest::Approx(1.0f));
    CHECK(lerp(1.0f, 5.0f, 1.0f) == doctest::Approx(5.0f));

    CHECK(lerp(-2.0f, 2.0f, 0.5f) == doctest::Approx(0.0f));
    CHECK(lerp(10.0f, -2.0f, 0.5f) == doctest::Approx(4.0f));

    // Monotonic in t when b > a
    CHECK(lerp(2.0f, 8.0f, 0.25f) < lerp(2.0f, 8.0f, 0.75f));
    // And reversed when b < a
    CHECK(lerp(8.0f, 2.0f, 0.25f) > lerp(8.0f, 2.0f, 0.75f));
}

// ---------------------- smoothstep ----------------------
TEST_CASE("smoothstep: clamps outside [a,b] and matches Hermite polynomial") {
    const float a = -3.0f, b = 5.0f;

    // Clamps
    CHECK(smoothstep(a, b, a - 1000.0f) == doctest::Approx(0.0f));
    CHECK(smoothstep(a, b, b + 1000.0f) == doctest::Approx(1.0f));

    // Sampled agreement with Hermite form
    for (float x : {a - 1.0f, a, (a + b) * 0.25f, (a + b) * 0.5f, (a + b) * 0.75f, b, b + 1.0f}) {
        CHECK(smoothstep(a, b, x) == doctest::Approx(hermite_smoothstep(a, b, x)).epsilon(1e-6));
    }
}

TEST_CASE("smoothstep: symmetry around 0.5 in [0,1]") {
    // For a=0,b=1, f(0.5+d) == 1 - f(0.5-d)
    const float a = 0.0f, b = 1.0f;
    for (float d : {0.0f, 0.1f, 0.25f, 0.4f}) {
        const float left  = smoothstep(a, b, 0.5f - d);
        const float right = smoothstep(a, b, 0.5f + d);
        CHECK(right == doctest::Approx(1.0f - left).epsilon(1e-6));
    }
}

// ---------------------- fbm2D ----------------------
// Note: fbm2D is TU-internal (static) in WorldGen.cpp; since we included that TU above,
// we can call it here as colony::worldgen::fbm2D.
TEST_CASE("fbm2D: result is within [0,1] and deterministic for same inputs") {
    const float x = 12.345f, y = 67.89f;
    const std::uint32_t seed = 123456u;

    const float f1 = fbm2D(x, y, seed, /*octaves*/5, /*lacunarity*/2.03f, /*gain*/0.5f);
    const float f2 = fbm2D(x, y, seed, /*octaves*/5, /*lacunarity*/2.03f, /*gain*/0.5f);

    CHECK(f1 == doctest::Approx(f2).epsilon(0.0)); // bit-for-bit in practice
    CHECK(f1 >= -1e-6f);
    CHECK(f1 <= 1.0f + 1e-6f);

    // Different seed -> typically different value (not guaranteed mathematically,
    // but overwhelmingly likely for value-noise-based FBM).
    const float f3 = fbm2D(x, y, seed + 1u, 5, 2.03f, 0.5f);
    CHECK_FALSE(std::abs(f1 - f3) < 1e-9f);
}

TEST_CASE("fbm2D: invariants for gain and lacunarity") {
    const float x = -3.2f, y = 9.1f;
    const std::uint32_t seed = 0xDEADBEEFu;

    // If gain == 0, only the first octave contributes -> exact match to octaves=1
    const float f_oct1 = fbm2D(x, y, seed, 1, 3.0f, 0.0f);
    const float f_gain0_many = fbm2D(x, y, seed, 6, 2.0f, 0.0f);
    CHECK(f_oct1 == doctest::Approx(f_gain0_many).epsilon(0.0));

    // With a single octave, lacunarity has no effect (freq is applied after the first octave).
    const float f_lac2 = fbm2D(x, y, seed, 1, 2.0f, 0.7f);
    const float f_lac3 = fbm2D(x, y, seed, 1, 3.0f, 0.9f);
    CHECK(f_lac2 == doctest::Approx(f_lac3).epsilon(0.0));
}
