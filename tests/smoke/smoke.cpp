#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include <filesystem>

TEST_CASE("engine boots") {
    CHECK(true);
}

TEST_CASE("assets-and-shaders-exist") {
    namespace fs = std::filesystem;
    INFO("cwd = " << fs::current_path().string());
    CHECK(fs::exists("res"));
    CHECK(fs::exists("shaders") || fs::exists("res/shaders"));
}
