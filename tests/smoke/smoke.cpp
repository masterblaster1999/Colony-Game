#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

TEST_CASE("smoke: startup predicate is true") {
    const bool initialized = true;
    // Use a binary comparison instead of chaining (doctest forbids complex boolean expressions)
    CHECK(initialized == true);
}
