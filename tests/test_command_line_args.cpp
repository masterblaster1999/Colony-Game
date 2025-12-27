// tests/test_command_line_args.cpp
//
// Regression coverage for src/app/CommandLineArgs.{h,cpp}.
//
// Goals:
//   - Options are case-insensitive
//   - Windows-style /switch normalization works (/w, /hgt, /safe-mode, /?)
//   - Both "--opt value" and "--opt=value" / "--opt:value" are supported
//   - Unknown options and bad values are reported in a predictable order

#include <doctest/doctest.h>

#include "app/CommandLineArgs.h"

#include <initializer_list>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] colony::appwin::CommandLineArgs Parse(std::initializer_list<std::wstring_view> argv)
{
    std::vector<std::wstring_view> v;
    v.reserve(argv.size());
    for (const auto& a : argv)
        v.push_back(a);
    return colony::appwin::ParseCommandLineArgsFromArgv(v);
}

} // namespace

TEST_CASE("CommandLineArgs parses basic flags (case-insensitive)")
{
    const auto args = Parse({
        L"ColonyGame.exe",
        L"--SAFE-MODE",
        L"--No-ImGui",
        L"--IGNORE-SETTINGS",
    });

    CHECK(args.safeMode);
    CHECK(args.disableImGui);
    CHECK(args.ignoreSettings);
    CHECK(args.unknown.empty());
}

TEST_CASE("CommandLineArgs supports Windows-style /switch normalization")
{
    const auto args = Parse({
        L"ColonyGame.exe",
        L"/safe-mode",
        L"/w",
        L"800",
        L"/hgt",
        L"600",
    });

    CHECK(args.safeMode);
    REQUIRE(args.width.has_value());
    REQUIRE(args.height.has_value());
    CHECK(args.width.value() == 800);
    CHECK(args.height.value() == 600);
    CHECK(args.unknown.empty());
}

TEST_CASE("CommandLineArgs accepts : and = separators for values")
{
    const auto args = Parse({
        L"ColonyGame.exe",
        L"--width=1280",
        L"--height:720",
        L"/bgfps:30",
        L"/mfl=4",
    });

    REQUIRE(args.width);
    REQUIRE(args.height);
    REQUIRE(args.maxFpsWhenUnfocused);
    REQUIRE(args.maxFrameLatency);
    CHECK(args.width.value() == 1280);
    CHECK(args.height.value() == 720);
    CHECK(args.maxFpsWhenUnfocused.value() == 30);
    CHECK(args.maxFrameLatency.value() == 4);
    CHECK(args.unknown.empty());
}

TEST_CASE("CommandLineArgs last boolean override wins")
{
    const auto args = Parse({
        L"ColonyGame.exe",
        L"/fullscreen",
        L"/windowed",
    });

    REQUIRE(args.fullscreen.has_value());
    CHECK(args.fullscreen.value() == false);
    CHECK(args.unknown.empty());
}

TEST_CASE("CommandLineArgs reports unknown options and bad values")
{
    const auto args = Parse({
        L"ColonyGame.exe",
        L"--width",
        L"abc",
        L"--does-not-exist",
        L"--height=notanint",
    });

    CHECK_FALSE(args.width.has_value());
    CHECK_FALSE(args.height.has_value());

    // Invalid "--width abc" produces two unknown entries because the parser
    // does not consume the bad value token.
    REQUIRE(args.unknown.size() == 4);
    CHECK(args.unknown[0] == L"--width");
    CHECK(args.unknown[1] == L"abc");
    CHECK(args.unknown[2] == L"--does-not-exist");
    CHECK(args.unknown[3] == L"--height=notanint");
}
