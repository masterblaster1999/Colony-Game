// tests/worldgen/test_determinism.cpp
//
// Windows-only determinism tests for worldgen/noise.
// - Fixes doctest::Approx C2512 by constructing it WITH a value.
// - Verifies same-seed determinism at point samples and chunk level.
// - Uses a stable FNV-1a hash over a float grid (no locale/format dependence).
//
// Integration notes:
//  * The test tries to bind automatically to one of:
//      - <FastNoiseLite.h>           (FastNoiseLite)
//      - <PerlinNoise.hpp>           (siv::PerlinNoise)
//      - "worldgen/Noise.h"          (your own API providing noise2D(...))
//  * If none are found, the test case is compiled but skipped. To enable it,
//    either add one of the headers above, or define your own `height_at()`
//    and remove CG_NO_NOISE_API.
//
// No main() here – assume your test runner defines it elsewhere.

#if defined(_WIN32) || defined(_WIN64)
  // ok, this suite is Windows-first
#else
  #define DOCTEST_CONFIG_DISABLE
#endif

#if !defined(__has_include)
  #define __has_include(x) 0
#endif

// doctest include – try the common vcpkg-style path first.
#if __has_include(<doctest/doctest.h>)
  #include <doctest/doctest.h>
#elif __has_include(<doctest.h>)
  #include <doctest.h>
#else
  #error "doctest header not found – ensure doctest is available to tests."
#endif

#include <bit>        // C++20: std::bit_cast, std::endian  [required for bit_cast]
#include <cstdint>
#include <vector>
#include <array>
#include <random>
#include <algorithm>
#include <numeric>    // std::iota
#include <cstring>
#include <type_traits>
#include <limits>

// Environment assumptions for bitwise reproducibility on Windows builds.
static_assert(sizeof(float) == 4, "This test assumes 32-bit IEEE-754 floats.");
static_assert(std::numeric_limits<float>::is_iec559,
              "This test assumes IEEE-754 (iec559) semantics for float.");
#if defined(__cpp_lib_endian) && __cpp_lib_endian >= 201907L
  static_assert(std::endian::native == std::endian::little,
                "This test suite assumes little-endian byte order.");
#endif

// ---------- Bind to one of several noise/worldgen APIs ----------

namespace cg_test
{
    // Unified adapter: return a deterministic height/noise value for (x,y,seed)
    // in double precision (we’ll hash floats for chunk tests).
    static inline double height_at(int x, int y, std::uint32_t seed) noexcept;

    // Try FastNoiseLite (very common in game projects)
    #if __has_include(<FastNoiseLite.h>)
        #include <FastNoiseLite.h>
        static inline double height_at(int x, int y, std::uint32_t seed) noexcept {
            FastNoiseLite fn(seed);
            fn.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
            // Use integer lattice directly – adjust if your world uses scaling.
            return static_cast<double>(fn.GetNoise((float)x, (float)y));
        }
    // Try siv::PerlinNoise.hpp
    #elif __has_include(<PerlinNoise.hpp>)
        #include <PerlinNoise.hpp>
        static inline double height_at(int x, int y, std::uint32_t seed) noexcept {
            siv::PerlinNoise pn(seed);
            // _01 variant is in [0,1]; map to [-1,1] to match typical Perlin range
            return static_cast<double>(pn.noise2D_01(double(x), double(y)) * 2.0 - 1.0);
        }
    // Try a project-local noise header (adjust the namespace/function if needed)
    #elif __has_include("worldgen/Noise.h")
        #include "worldgen/Noise.h"
        using ::worldgen::noise2D; // expecting: double noise2D(float x, float y, uint32_t seed)
        static inline double height_at(int x, int y, std::uint32_t seed) noexcept {
            return static_cast<double>(noise2D(static_cast<float>(x), static_cast<float>(y), seed));
        }
    #else
        #define CG_NO_NOISE_API 1
        static inline double height_at(int, int, std::uint32_t) noexcept {
            return 0.0; // never called when skipping, but defined to satisfy the linker if needed
        }
    #endif
} // namespace cg_test

// ---------- Utilities: bit_cast polyfill & FNV-1a hashing ----------

namespace cg_test
{
#if defined(__cpp_lib_bit_cast) && __cpp_lib_bit_cast >= 201806L
    using std::bit_cast; // C++20 <bit>
#else
    template <class To, class From>
    static inline std::enable_if_t<
        sizeof(To) == sizeof(From) &&
        std::is_trivially_copyable<From>::value &&
        std::is_trivially_copyable<To>::value, To>
    bit_cast(const From& src) noexcept {
        To dst;
        std::memcpy(&dst, &src, sizeof(To));
        return dst;
    }
#endif

    // Spec-correct 64-bit FNV-1a (offset-basis + prime).
    // See: FNV hash specification (offset_basis 14695981039346656037, prime 1099511628211).
    // https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
    static inline std::uint64_t fnv1a64(const void* data, std::size_t nbytes) noexcept {
        const auto* p = static_cast<const unsigned char*>(data);
        std::uint64_t h = 14695981039346656037ull;      // offset basis (0xCBF29CE484222325)
        constexpr std::uint64_t prime = 1099511628211ull; // 0x100000001B3
        for (std::size_t i = 0; i < nbytes; ++i) {
            h ^= static_cast<std::uint64_t>(p[i]);
            h *= prime;
        }
        return h;
    }

    // Make a size x size grid sampling height_at on the world lattice of chunk (cx, cy).
    static inline std::vector<float> make_grid(std::uint32_t seed, int cx, int cy, int size) {
        std::vector<float> g(static_cast<std::size_t>(size) * static_cast<std::size_t>(size));
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const int wx = cx * size + x;
                const int wy = cy * size + y;
                const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(size)
                                      + static_cast<std::size_t>(x);
                g[idx] = static_cast<float>(height_at(wx, wy, seed));
            }
        }
        return g;
    }

    static inline std::uint64_t hash_grid(const std::vector<float>& g) noexcept {
        // Hash by value bits – independent of locale/formatting.
        std::uint64_t h = 14695981039346656037ull;
        constexpr std::uint64_t prime = 1099511628211ull;
        for (float v : g) {
            const std::uint32_t bits = bit_cast<std::uint32_t>(v);
            h ^= static_cast<std::uint64_t>(bits & 0xFFu);
            h *= prime;
            h ^= static_cast<std::uint64_t>((bits >> 8) & 0xFFu);
            h *= prime;
            h ^= static_cast<std::uint64_t>((bits >> 16) & 0xFFu);
            h *= prime;
            h ^= static_cast<std::uint64_t>((bits >> 24) & 0xFFu);
            h *= prime;
        }
        return h;
    }
} // namespace cg_test

// ---------- Tests ----------

using cg_test::height_at;
using cg_test::make_grid;
using cg_test::hash_grid;

#ifndef CG_NO_NOISE_API

TEST_SUITE("worldgen_determinism");

TEST_CASE("Point samples: same seed + coords -> same value (within epsilon)")
{
    constexpr double eps = 1e-6;

    const std::array<std::uint32_t, 4> seeds{{
        0u, 1u, 123456789u, 0xDEADBEEFu
    }};
    const std::array<std::pair<int,int>, 8> coords{{
        {0,0}, {1,0}, {0,1}, {13,37},
        {-1,-1}, {-17, 4}, {256, -512}, {-999, 999}
    }};

    for (auto seed : seeds) {
        for (auto [x,y] : coords) {
            const double a = height_at(x, y, seed);
            const double b = height_at(x, y, seed); // recompute
            CHECK(b == doctest::Approx(a).epsilon(eps));
        }
    }
}

TEST_CASE("Chunk hash: same seed + chunk -> identical bitwise hash")
{
    const std::array<std::uint32_t, 4> seeds{{
        0u, 42u, 123456u, 0xCAFEBABEu
    }};
    const std::array<std::pair<int,int>, 6> chunks{{
        {0,0}, {1,0}, {0,1}, {-1,-1}, {7,-3}, {15,15}
    }};
    constexpr int SIZE = 64;

    for (auto seed : seeds) {
        for (auto [cx, cy] : chunks) {
            const auto g1 = make_grid(seed, cx, cy, SIZE);
            const auto g2 = make_grid(seed, cx, cy, SIZE);

            const auto h1 = hash_grid(g1);
            const auto h2 = hash_grid(g2);

            CHECK(h1 == h2);
        }
    }
}

TEST_CASE("Different seeds rarely collide: chunk hash should usually differ")
{
    constexpr int SIZE = 64;
    const int cx = 3, cy = -2;

    const auto gA = make_grid(1111u, cx, cy, SIZE);
    const auto gB = make_grid(2222u, cx, cy, SIZE);

    const auto hA = hash_grid(gA);
    const auto hB = hash_grid(gB);

    CHECK(hA != hB);
}

TEST_CASE("Traversal order invariance: randomized sampling vs sequential")
{
    constexpr int SIZE = 48;
    constexpr std::uint32_t seed = 0xA1B2C3D4u;
    const int cx = -5, cy = 9;

    // Sequential
    const auto seq = make_grid(seed, cx, cy, SIZE);

    // Randomized order fill
    std::vector<float> rnd(seq.size());
    std::vector<std::size_t> indices(seq.size());
    std::iota(indices.begin(), indices.end(), 0);

    std::mt19937 rng(123456u); // fixed RNG to make the test self-deterministic
    std::shuffle(indices.begin(), indices.end(), rng);

    for (std::size_t k = 0; k < indices.size(); ++k) {
        const std::size_t i = indices[k];
        const int x = int(i % SIZE);
        const int y = int(i / SIZE);
        const int wx = cx * SIZE + x;
        const int wy = cy * SIZE + y;
        rnd[i] = static_cast<float>(height_at(wx, wy, seed));
    }

    CHECK(hash_grid(seq) == hash_grid(rnd));
}

TEST_SUITE_END();

#else // CG_NO_NOISE_API

TEST_CASE("Worldgen determinism (skipped): no known noise API headers found" * doctest::skip(true))
{
    // To enable this suite, add FastNoiseLite, siv::PerlinNoise, or your own
    // worldgen/Noise.h (exposing noise2D(x,y,seed)), then re-run CI.
}

#endif // CG_NO_NOISE_API
