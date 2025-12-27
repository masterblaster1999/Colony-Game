// tests/test_input_binding_parse_vk_hex.cpp
//
// Regression tests for parsing InputCodeToToken()'s fallback "VK_0x.." format.
//
// Motivation:
// InputCodeToToken() emits unknown keyboard codes as "VK_0xNN".
// Historically, ParseInputCodeToken() did not accept that format, so config/UI
// output could not be round-tripped back into binds.

#include <doctest/doctest.h>

#include "input/InputBindingParse.h"

#include <vector>

using namespace colony::input::bindings;

TEST_CASE("ParseInputCodeToken accepts VK_0x.. tokens")
{
    // Pick a reasonably common OEM key code that the pretty-printer does not
    // special-case (so it uses the fallback VK_0xNN format).
    constexpr std::uint32_t kUnknownVk = 0xBA; // VK_OEM_1 on Win32

    const std::string token = InputCodeToToken(kUnknownVk);
    CHECK(token == "VK_0xBA");

    const auto parsed = ParseInputCodeToken(token);
    REQUIRE(parsed.has_value());
    CHECK(*parsed == kUnknownVk);

    // Case-insensitivity
    CHECK(ParseInputCodeToken("vk_0xba").value_or(0u) == kUnknownVk);

    // Also accept bare 0xNN.
    CHECK(ParseInputCodeToken("0xBA").value_or(0u) == kUnknownVk);
}

TEST_CASE("ParseInputCodeToken rejects invalid VK_0x.. tokens")
{
    CHECK_FALSE(ParseInputCodeToken("VK_0x").has_value());
    CHECK_FALSE(ParseInputCodeToken("VK_0xGG").has_value());
    CHECK_FALSE(ParseInputCodeToken("VK_0x1FF").has_value()); // out of keyboard range
}

TEST_CASE("ParseChordString supports VK_0x.. tokens")
{
    std::vector<std::uint32_t> codes;

    CHECK(ParseChordString("Ctrl+VK_0xBA", codes));
    REQUIRE(codes.size() == 2);

    // Sorted order: Ctrl (0x11) then the OEM key (0xBA).
    CHECK(codes[0] == kVK_CONTROL);
    CHECK(codes[1] == 0xBA);
}
