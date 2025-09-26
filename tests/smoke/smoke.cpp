#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

TEST_CASE("smoke: startup predicate is true") {
    const bool initialized = true;
    CHECK(initialized == true);
}
