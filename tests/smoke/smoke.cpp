// Minimal-but-useful smoke test for Colony-Game on Windows.
// Uses doctest (header-only) and EnTT from vcpkg.

// NOTE:
// This file is not currently part of the default colony_tests target (the
// main tests target only globs tests/*.cpp). If you later add recursive test
// collection, keep this file free of DOCTEST_CONFIG_IMPLEMENT* macros.
// The doctest implementation + main() must live in a single TU (tests/test_main.cpp).

// Use the repo's compatibility wrapper so this smoke test builds even when
// doctest is vendored (./external/doctest) instead of installed system-wide.
#include "../doctest.h"

// EnTT is normally provided by vcpkg for the full project, but this standalone
// Visual Studio project (tests/colony_smoke.vcxproj) can be built without vcpkg.
// Make EnTT-dependent tests compile only when the header is available.
#if defined(__has_include)
  #if __has_include(<entt/entt.hpp>)
    #define COLONY_SMOKE_HAS_ENTT 1
    #include <entt/entt.hpp>
  #endif
#endif

TEST_CASE("smoke::doctest_boots") {
    CHECK(true);
    CHECK_EQ(1, 1);
}

#if defined(COLONY_SMOKE_HAS_ENTT)
TEST_CASE("entt::basic_registry create/valid/destroy") {
    entt::registry reg;

    auto e = reg.create();
    CHECK(reg.valid(e));               // <-- EnTT 3.15+: use valid(), not alive()

    reg.emplace<int>(e, 42);
    CHECK(reg.all_of<int>(e));
    CHECK_EQ(reg.get<int>(e), 42);

    reg.destroy(e);
    CHECK_FALSE(reg.valid(e));
}

TEST_CASE("entt::view iteration") {
    struct pos { float x, y; };
    struct vel { float x, y; };

    entt::registry reg;

    auto a = reg.create();
    auto b = reg.create();
    reg.emplace<pos>(a, 1.0f, 2.0f);
    reg.emplace<vel>(a, 0.5f, 0.5f);
    reg.emplace<pos>(b, -1.0f, 0.0f);

    int counted = 0;
    for (auto entity : reg.view<pos>()) {
        (void)entity;
        ++counted;
    }
    CHECK_EQ(counted, 2);

    // update all with both pos+vel
    for (auto [entity, p, v] : reg.view<pos, vel>().each()) {
        (void)entity;
        p.x += v.x; p.y += v.y;
    }
    auto [x,y] = reg.get<pos>(a);
    CHECK_GT(x, 1.0f);
    CHECK_GT(y, 2.0f);
}

#else

TEST_CASE("entt::header optional") {
    // If you want EnTT coverage here, install dependencies via vcpkg (manifest)
    // and/or build via the repo's CMake presets.
    CHECK(true);
}

#endif
