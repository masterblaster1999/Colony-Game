// tests/test_main.cpp
//
// IMPORTANT:
//   This must be the ONLY translation unit in the test executable that defines
//   DOCTEST_CONFIG_IMPLEMENT (or DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN).
//   All other test .cpp files should just include doctest WITHOUT those macros.
//
// We use DOCTEST_CONFIG_IMPLEMENT (instead of ...WITH_MAIN) so we can customize
// defaults and still support doctest command-line flags.
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

// Defensive: avoid leaking implementation macros into other files when tests are
// built in unity/jumbo mode. CMake also excludes this file from unity builds,
// but keeping this here makes the TU safe even if that property is removed.
#undef DOCTEST_CONFIG_IMPLEMENT

#include <cstdlib> // std::getenv
#include <cstring> // std::strcmp

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h> // IsDebuggerPresent
#endif

namespace {

bool env_truthy(const char* v) {
    // Treat any non-empty value except "0" as true (sufficient for typical CI env vars).
    return v != nullptr && v[0] != '\0' && std::strcmp(v, "0") != 0;
}

bool running_in_ci() {
    // Common CI environment variables across popular CI providers.
    return env_truthy(std::getenv("CI")) ||
           env_truthy(std::getenv("GITHUB_ACTIONS")) ||
           env_truthy(std::getenv("TF_BUILD")) ||
           env_truthy(std::getenv("APPVEYOR"));
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
    if (running_in_ci()) {
        context.setOption("no-breaks", true);
        context.setOption("no-colors", true);
    }

    // Local debugging: keep breaks enabled when a debugger is attached.
    if (debugger_attached()) {
        context.setOption("no-breaks", false);
    }

    // Let command-line arguments override the defaults above.
    context.applyCommandLine(argc, argv);

    const int res = context.run();

    // --help / --version etc.
    if (context.shouldExit())
        return res;

    return res;
}
