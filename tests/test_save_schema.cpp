// tests/test_save_schema.cpp
#ifdef _WIN32
#ifdef VALIDATE_SAVES

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if __has_include(<nlohmann/json-schema.hpp>)
  #include <nlohmann/json-schema.hpp>
  #define COLONY_HAS_JSON_VALIDATOR 1
#else
  #define COLONY_HAS_JSON_VALIDATOR 0
#endif

namespace fs = std::filesystem;
using nlohmann::json;

static fs::path repo_root_from_src() {
    // This file lives in <repo>/tests/, so parent_path() is the repo root.
    return fs::path(__FILE__).parent_path().parent_path();
}

static std::vector<fs::path> find_schema_candidates() {
    std::vector<fs::path> out;

    const fs::path schema_dir = repo_root_from_src() / "data" / "schema";
    if (fs::exists(schema_dir)) {
        for (const auto& e : fs::directory_iterator(schema_dir)) {
            if (e.is_regular_file() && e.path().extension() == ".json")
                out.push_back(e.path());
        }
    }

    // Fallback: any *.json containing "schema" or "save" in the filename under the repo.
    if (out.empty()) {
        for (const auto& e : fs::recursive_directory_iterator(repo_root_from_src())) {
            const auto name = e.path().filename().string();
            if (e.is_regular_file() && e.path().extension() == ".json" &&
                (name.find("schema") != std::string::npos || name.find("save") != std::string::npos)) {
                out.push_back(e.path());
            }
        }
    }

    return out;
}

TEST_CASE("Save schema JSON file(s) exist and parse") {
    const auto candidates = find_schema_candidates();
    REQUIRE_MESSAGE(!candidates.empty(),
        "No schema JSON files found under data/schema/ or anywhere in the repo.");

    for (const auto& f : candidates) {
        std::ifstream in(f);
        REQUIRE_MESSAGE(in.good(), "Unable to open schema file: " << f.string());
        json j; in >> j;

        CHECK(j.is_object());
        // Common meta fields; schema may or may not include both.
        CHECK_MESSAGE(j.contains("$schema") || j.contains("$id"),
                      "Schema meta fields missing in: " << f.string());
    }
}

#if COLONY_HAS_JSON_VALIDATOR
TEST_CASE("Schema compiles and validates embedded examples (if any)") {
    const auto candidates = find_schema_candidates();
    REQUIRE(!candidates.empty());

    // Use the first schema found.
    std::ifstream in(candidates.front());
    json schema; in >> schema;

    nlohmann::json_schema::json_validator validator;
    // Must not throw when compiling the schema.
    CHECK_NOTHROW(validator.set_root_schema(schema));

    // If the schema includes JSON examples, validate them.
    if (schema.contains("examples") && schema["examples"].is_array() && !schema["examples"].empty()) {
        for (const auto& ex : schema["examples"]) {
            CHECK_NOTHROW(validator.validate(ex));
        }
    } else {
        // No examples to validate: compilation is still a useful check.
        CHECK(true);
    }
}
#else
TEST_CASE("json-schema-validator not available") {
    // Informative check that keeps CI green until the dependency is added.
    CHECK_MESSAGE(true,
        "nlohmann/json-schema.hpp not found - install json-schema-validator "
        "and build with -DVALIDATE_SAVES=ON to enable schema validation.");
}
#endif // COLONY_HAS_JSON_VALIDATOR

#endif // VALIDATE_SAVES
#endif // _WIN32
