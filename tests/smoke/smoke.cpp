// tests/smoke/smoke.cpp
//
// Colony-Game — smoke test suite
// - Ultra-fast checks that the build & environment are sane.
// - Purely header-only / stdlib usage unless headers are present; no extra linking.
//
// Safe to compile with the existing tests/CMakeLists.txt.
//
// Tags (doctest): [smoke], [fs], [core], [entt], [taskflow], [fmt]

#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

// C++20 STL
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// Conditionally bring in header-only third-party deps if visible in the compiler's include path.
// We only use header-only parts so the current tests/CMakeLists remains unchanged.
#if __has_include(<entt/entt.hpp>)
  #define CG_HAS_ENTT 1
  #include <entt/entt.hpp>
#else
  #define CG_HAS_ENTT 0
#endif

#if __has_include(<taskflow/taskflow.hpp>)
  #define CG_HAS_TASKFLOW 1
  #include <taskflow/taskflow.hpp>
#else
  #define CG_HAS_TASKFLOW 0
#endif

#if __has_include(<fmt/format.h>)
  #define CG_HAS_FMT 1
  #ifndef FMT_HEADER_ONLY
    #define FMT_HEADER_ONLY 1
  #endif
  #include <fmt/format.h>
#else
  #define CG_HAS_FMT 0
#endif

#if defined(_WIN32)
  #define CG_ON_WINDOWS 1
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h> // included but we avoid calling WinAPI to keep linking minimal
#else
  #define CG_ON_WINDOWS 0
#endif

namespace cg::smoke {

[[nodiscard]] inline bool file_exists(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::exists(p, ec);
}

[[nodiscard]] inline bool dir_exists(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::is_directory(p, ec);
}

[[nodiscard]] inline std::optional<std::string>
read_first_line(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if(!in) return std::nullopt;
    std::string line;
    std::getline(in, line);
    if(!line.empty()) return line;
    return std::nullopt;
}

struct deterministic_rng {
    std::mt19937_64 eng;
    explicit deterministic_rng(std::uint64_t seed = 0xC0110AB5ull) : eng{seed} {}

    // Returns identical sequence for identical seeds (deterministic).
    int uniform_int(int lo, int hi) {
        std::uniform_int_distribution<int> dist(lo, hi);
        return dist(eng);
    }
};

} // namespace cg::smoke

// ----------------------
// Core sanity & FS checks
// ----------------------

TEST_CASE("[smoke][core] doctest boots and C++20 basics hold") {
    CHECK(2 + 2 == 4);

#if CG_ON_WINDOWS
    // This project targets Win64 — assert that in Windows builds.
    static_assert(sizeof(void*) == 8, "Expected a 64-bit Windows build.");
#endif

    // span: view + write-through
    std::array<int, 4> a{1, 2, 3, 4};
    std::span<int> s{a};
    s[1] = 42;
    CHECK(a[1] == 42);

    // deterministic RNG equality for same seeds (no flakiness)
    cg::smoke::deterministic_rng r1{123}, r2{123};
    for (int i = 0; i < 8; ++i)
        CHECK(r1.uniform_int(0, 1000) == r2.uniform_int(0, 1000));
}

TEST_CASE("[smoke][fs] repository layout is visible from working directory") {
    using cg::smoke::dir_exists;
    using cg::smoke::file_exists;

    // tests/CMakeLists.txt sets the working dir to CMAKE_SOURCE_DIR.
    // Assert a couple of expected entries so we fail early if that ever regresses.
    INFO("cwd = " << std::filesystem::current_path().string());

    CHECK(file_exists("CMakeLists.txt"));
    CHECK(file_exists("vcpkg.json"));
    CHECK(dir_exists("src"));
    CHECK(dir_exists("tests"));

    // README should be present and non-empty.
    auto first = cg::smoke::read_first_line("README.md");
    REQUIRE(first.has_value());
    CHECK(first->size() > 0);
}

// ----------------------
// Optional: EnTT (header-only)
// ----------------------
TEST_CASE("[smoke][entt] registry create/update/destroy (if EnTT headers visible)") {
#if !CG_HAS_ENTT
    DOCTEST_SKIP("EnTT header not found in include path — skipping.");
#else
    struct Position { float x{}, y{}; };
    struct Velocity { float dx{}, dy{}; };

    entt::registry reg;
    constexpr int N = 256;
    std::array<entt::entity, N> ents{};

    for (int i = 0; i < N; ++i) {
        ents[i] = reg.create();
        reg.emplace<Position>(ents[i], float(i), float(i));
        if ((i % 2) == 0) reg.emplace<Velocity>(ents[i], 1.f, -1.f);
    }

    // Update only those with velocity.
    int moved = 0;
    reg.view<Position, const Velocity>().each([&](auto, Position& p, const Velocity& v) {
        p.x += v.dx; p.y += v.dy; ++moved;
    });
    CHECK(moved == (N + 1) / 2); // even indices

    // Destroy every 3rd entity and check view sizes match the expectation.
    for (int i = 0; i < N; i += 3) reg.destroy(ents[i]);
    const int destroyed = (N + 2) / 3;
    const int expected_remaining = N - destroyed;

    // Count remaining Position components.
    std::size_t pos_count = 0;
    reg.view<Position>().each([&](auto, Position&) { ++pos_count; });

    CHECK(static_cast<int>(pos_count) == expected_remaining);
    CHECK(static_cast<int>(reg.alive()) == expected_remaining);
#endif
}

// ----------------------
// Optional: Taskflow (header-only)
// ----------------------
TEST_CASE("[smoke][taskflow] simple DAG transformation + reduction (if Taskflow headers visible)") {
#if !CG_HAS_TASKFLOW
    DOCTEST_SKIP("Taskflow header not found in include path — skipping.");
#else
    std::vector<int> data(1000);
    std::iota(data.begin(), data.end(), 1); // 1..1000

    std::atomic<long long> sum{0};

    tf::Executor exec;
    tf::Taskflow flow;

    // Double all values, then sum them in parallel.
    auto t_double = flow.for_each(data.begin(), data.end(), [](int& v) { v *= 2; });
    auto t_sum    = flow.for_each(data.begin(), data.end(),
                                  [&](int v) { sum.fetch_add(v, std::memory_order_relaxed); });

    t_sum.succeed(t_double);
    exec.run(flow).wait();

    // Sum(1..1000) = 500,500; doubled => 1,001,000
    CHECK(sum.load(std::memory_order_relaxed) == 1001000LL);
#endif
}

// ----------------------
// Optional: {fmt} (header-only mode)
// ----------------------
TEST_CASE("[smoke][fmt] formatting works in header-only mode (if fmt headers visible)") {
#if !CG_HAS_FMT
    DOCTEST_SKIP("fmt header not found in include path — skipping.");
#else
    const std::string s = fmt::format("ColonyGame v{}.{}.{}", 0, 1, 0);
    CHECK(s.find("ColonyGame") != std::string::npos);
#endif
}

// ----------------------
// Custom main: friendlier defaults for CI/local runs
// ----------------------
int main(int argc, char** argv) {
    doctest::Context ctx;

    // Nicer defaults for a smoke suite:
    ctx.setOption("order-by", "name");   // stable ordering
    ctx.setOption("abort-after", 5);     // don't drown logs if something fundamental breaks
    ctx.setOption("no-breaks", true);    // don't break into debugger even if available

    ctx.applyCommandLine(argc, argv);
    const int res = ctx.run();
    // ctx.shouldExit() is useful if integrating with other code; not needed here.
    return res;
}
