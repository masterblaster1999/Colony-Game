// tests/smoke.cpp
//
// Keep this as a lightweight sanity check that the unit test binary boots.
// The actual test runner main() lives in tests/test_main.cpp.

#include "doctest_include.h"

TEST_CASE("smoke: test runner boots") {
    CHECK(true);
    // Expand later: device/renderer bootstrap once the engine exposes a
    // deterministic headless init path.
}
