// tests/test_main.cpp
//
// IMPORTANT:
//   This must be the ONLY translation unit in the test executable that defines
//   DOCTEST_CONFIG_IMPLEMENT (or DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN).
//   All other test .cpp files must ONLY `#include <doctest/doctest.h>`
//   and must NOT define any DOCTEST_CONFIG_* implementation macros.
//
// We intentionally use DOCTEST_CONFIG_IMPLEMENT (NOT ...WITH_MAIN) because we
// provide our own main() so we can set sane defaults programmatically while
// still honoring doctest command-line flags.
// (Doctest requires the implementation block to exist in exactly one TU.)

#if defined(DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN)
#   error "DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN generates its own main(). This file provides a custom main(), so DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN must NOT be defined. Remove it from any target-wide compile definitions and any other .cpp files."
#endif

#ifdef _WIN32
    // Prevent Windows headers from defining min/max macros and reduce header bloat.
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
#endif

// Implement the doctest test runner in this (and only this) translation unit.
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <cstdlib> // std::getenv
#include <cstring> // std::strcmp

#ifdef _WIN32
    #include <windows.h> // IsDebuggerPresent
#endif

namespace {

bool env_truthy(const char* v) {
    // Treat any non-empty value except "0" as true (sufficient for typical CI env vars).
    return v != nullptr && v[0] != '\0' && std::strcmp(v, "0") != 0;
}

bool running_in_ci() {
    // Common CI environment variables across popular CI providers.
    // Add more here if you use a different CI.
    return env_truthy(std::getenv("CI")) ||
           env_truthy(std::getenv("GITHUB_ACTIONS")) ||
           env_truthy(std::getenv("TF_BUILD")) ||
           env_truthy(std::getenv("APPVEYOR")) ||
           env_truthy(std::getenv("GITLAB_CI")) ||
           env_truthy(std::getenv("CIRCLECI")) ||
           env_truthy(std::getenv("TEAMCITY_VERSION")) ||
           env_truthy(std::getenv("JENKINS_URL"));
}

bool debugger_attached() {
#ifdef _WIN32
    return ::IsDebuggerPresent() != FALSE;
#else
    return false;
#endif
}

} // namespace

int main(int argc, char** argv) {
    doctest::Context context;

    // ----- sane defaults (can be overridden by CLI flags) -----
    context.setOption("order-by", "name");        // deterministic ordering
    context.setOption("duration", true);          // show timings (helps spot slow tests)
    context.setOption("no-path-filenames", true); // cleaner Windows output

    // CI defaults: avoid debug breaks & ANSI color noise in logs.
    // (Still overridable via command line if you really want to.)
    if (running_in_ci()) {
        context.setOption("no-breaks", true);
        context.setOption("no-colors", true);
    }

    // Local debugging: keep breaks enabled when a debugger is attached.
    // (Still overridable via command line if you really want to.)
    if (debugger_attached()) {
        context.setOption("no-breaks", false);
    }

    // Let command-line arguments override the defaults above.
    context.applyCommandLine(argc, argv);

    const int res = context.run();

    // IMPORTANT: query flags (--help/--version/--list-test-cases/--no-run/--exit)
    // rely on the user doing this.
    if (context.shouldExit())
        return res;

    return res;
}
