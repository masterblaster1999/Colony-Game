// tests/worldgen/test_math_and_noise.cpp
//
// IMPORTANT:
//   Do NOT define DOCTEST_CONFIG_IMPLEMENT or DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN in this file.
//   The doctest implementation + test runner main() are provided by tests/test_main.cpp.
//
#if defined(DOCTEST_CONFIG_IMPLEMENT) || defined(DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN)
    #error "Do not define DOCTEST_CONFIG_IMPLEMENT* in individual test translation units. Define it only in tests/test_main.cpp."
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

// Pull in the production implementation so we can reach the TU-internal static fbm2D.
// This compiles the worldgen code into the *test* executable only.
//
// NOTE:
//   Only ONE test translation unit should include WorldGen.cpp directly.
//   If multiple tests include it, you'll get duplicate symbol/linker errors for the worldgen code.
#include "../../src/worldgen/WorldGen.cpp"

#include <doctest/doctest.h> // provided by vcpkg as <doctest/doctest.h> (header-only)

namespace wg = colony::worldgen;

static inline float hermite_smoothstep(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// ---------------------- lerp ----------------------
TEST_CASE("lerp: identities, midpoint, monotonicity") {
    CHECK(wg::lerp(1.0f, 5.0f, 0.0f) == doctest::Approx(1.0f));
    CHECK(wg::lerp(1.0f, 5.0f, 1.0f) == doctest::Approx(5.0f));

    CHECK(wg::lerp(-2.0f, 2.0f, 0.5f) == doctest::Approx(0.0f));
    CHECK(wg::lerp(10.0f, -2.0f, 0.5f) == doctest::Approx(4.0f));

    // Monotonic in t when b > a
    CHECK(wg::lerp(2.0f, 8.0f, 0.25f) < wg::lerp(2.0f, 8.0f, 0.75f));
    // And reversed when b < a
    CHECK(wg::lerp(8.0f, 2.0f, 0.25f) > wg::lerp(8.0f, 2.0f, 0.75f));
}

// ---------------------- smoothstep ----------------------
TEST_CASE("smoothstep: clamps outside [a,b] and matches Hermite polynomial") {
    const float a = -3.0f, b = 5.0f;

    // Clamps
    CHECK(wg::smoothstep(a, b, a - 1000.0f) == doctest::Approx(0.0f));
    CHECK(wg::smoothstep(a, b, b + 1000.0f) == doctest::Approx(1.0f));

    // Sampled agreement with Hermite form
    for (float x : {a - 1.0f,
                    a,
                    (a + b) * 0.25f,
                    (a + b) * 0.5f,
                    (a + b) * 0.75f,
                    b,
                    b + 1.0f}) {
        CHECK(wg::smoothstep(a, b, x) == doctest::Approx(hermite_smoothstep(a, b, x)).epsilon(1e-6f));
    }
}

TEST_CASE("smoothstep: symmetry around 0.5 in [0,1]") {
    // For a=0,b=1, f(0.5+d) == 1 - f(0.5-d)
    const float a = 0.0f, b = 1.0f;
    for (float d : {0.0f, 0.1f, 0.25f, 0.4f}) {
        const float left = wg::smoothstep(a, b, 0.5f - d);
        const float right = wg::smoothstep(a, b, 0.5f + d);
        CHECK(right == doctest::Approx(1.0f - left).epsilon(1e-6f));
    }
}

// ---------------------- fbm2D ----------------------
// Note: fbm2D is TU-internal (static) in WorldGen.cpp; since we included that TU above,
// we can call it here as colony::worldgen::fbm2D.
TEST_CASE("fbm2D: deterministic, finite, and within [0,1]") {
    const std::uint32_t seed = 123456u;
    constexpr int octaves = 5;
    constexpr float lacunarity = 2.03f;
    constexpr float gain = 0.5f;

    const std::array<std::pair<float, float>, 8> samples{{
        {12.345f, 67.89f},
        {-3.2f, 9.1f},
        {0.0f, 0.0f},
        {1.0f, 1.0f},
        {100.25f, -42.75f},
        {-999.0f, 0.125f},
        {0.5f, -0.5f},
        {37.0f, 13.0f},
    }};

    bool any_seed_difference = false;

    for (const auto& p : samples) {
        const float x = p.first;
        const float y = p.second;

        const float f1 = wg::fbm2D(x, y, seed, octaves, lacunarity, gain);
        const float f2 = wg::fbm2D(x, y, seed, octaves, lacunarity, gain);

        // Same inputs -> same output (should be deterministic)
        CHECK(f1 == f2);

        CHECK(std::isfinite(f1));
        CHECK(f1 >= -1e-6f);
        CHECK(f1 <= 1.0f + 1e-6f);

        // Different seed -> value should differ for at least one sample (guards against "seed ignored").
        const float f3 = wg::fbm2D(x, y, seed + 1u, octaves, lacunarity, gain);
        CHECK(std::isfinite(f3));
        CHECK(f3 >= -1e-6f);
        CHECK(f3 <= 1.0f + 1e-6f);

        if (std::abs(f1 - f3) > 1e-6f) {
            any_seed_difference = true;
        }
    }

    CHECK(any_seed_difference);
}

TEST_CASE("fbm2D: invariants for gain and lacunarity") {
    const float x = -3.2f, y = 9.1f;
    const std::uint32_t seed = 0xDEADBEEFu;

    // If gain == 0, only the first octave contributes -> exact match to octaves=1
    const float f_oct1 = wg::fbm2D(x, y, seed, 1, 3.0f, 0.0f);
    const float f_gain0_many = wg::fbm2D(x, y, seed, 6, 2.0f, 0.0f);
    CHECK(f_oct1 == doctest::Approx(f_gain0_many).epsilon(0.0));

    // With a single octave, lacunarity has no effect (freq is applied after the first octave).
    const float f_lac2 = wg::fbm2D(x, y, seed, 1, 2.0f, 0.7f);
    const float f_lac3 = wg::fbm2D(x, y, seed, 1, 3.0f, 0.7f);
    CHECK(f_lac2 == doctest::Approx(f_lac3).epsilon(0.0));

    // With a single octave, gain has no effect (amp is applied after the first octave).
    const float f_gain2 = wg::fbm2D(x, y, seed, 1, 2.0f, 0.2f);
    CHECK(f_lac2 == doctest::Approx(f_gain2).epsilon(0.0));
}
