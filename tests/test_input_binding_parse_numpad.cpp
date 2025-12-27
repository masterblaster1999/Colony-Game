// tests/test_input_binding_parse_numpad.cpp
//
// Regression + usability tests for numpad / keypad tokens in input binding parsing.
//
// Motivation:
//  - Numpad keys have distinct Win32 virtual-key codes (VK_NUMPAD0..VK_NUMPAD9, VK_ADD, ...).
//  - It's common for players to want to bind actions to the keypad.
//  - InputCodeToToken() should emit stable, human-friendly names, and the parser should
//    accept common aliases so binds can be hand-edited.

#include <doctest/doctest.h>

#include "input/InputBindingParse.h"

#include <vector>

using namespace colony::input::bindings;

TEST_CASE("ParseInputCodeToken supports numpad digits")
{
    CHECK(ParseInputCodeToken("Numpad0").value_or(0u) == kVK_NUMPAD0);
    CHECK(ParseInputCodeToken("numpad9").value_or(0u) == kVK_NUMPAD9);

    // Common short aliases.
    CHECK(ParseInputCodeToken("Num5").value_or(0u) == kVK_NUMPAD5);
    CHECK(ParseInputCodeToken("KP3").value_or(0u) == kVK_NUMPAD3);

    // Reject unsupported multi-digit suffix.
    CHECK_FALSE(ParseInputCodeToken("Numpad10").has_value());
}

TEST_CASE("ParseInputCodeToken supports numpad operations")
{
    CHECK(ParseInputCodeToken("NumpadAdd").value_or(0u) == kVK_ADD);
    CHECK(ParseInputCodeToken("NumpadPlus").value_or(0u) == kVK_ADD);
    CHECK(ParseInputCodeToken("KPPlus").value_or(0u) == kVK_ADD);

    CHECK(ParseInputCodeToken("NumpadSubtract").value_or(0u) == kVK_SUBTRACT);
    CHECK(ParseInputCodeToken("NumpadMinus").value_or(0u) == kVK_SUBTRACT);

    CHECK(ParseInputCodeToken("NumpadMultiply").value_or(0u) == kVK_MULTIPLY);
    CHECK(ParseInputCodeToken("KPMul").value_or(0u) == kVK_MULTIPLY);

    CHECK(ParseInputCodeToken("NumpadDivide").value_or(0u) == kVK_DIVIDE);
    CHECK(ParseInputCodeToken("KPDiv").value_or(0u) == kVK_DIVIDE);

    CHECK(ParseInputCodeToken("NumpadDecimal").value_or(0u) == kVK_DECIMAL);
    CHECK(ParseInputCodeToken("NumpadDot").value_or(0u) == kVK_DECIMAL);

    CHECK(ParseInputCodeToken("NumLock").value_or(0u) == kVK_NUMLOCK);
}

TEST_CASE("InputCodeToToken emits stable numpad names")
{
    CHECK(InputCodeToToken(kVK_NUMLOCK) == "NumLock");
    CHECK(InputCodeToToken(kVK_NUMPAD0) == "Numpad0");
    CHECK(InputCodeToToken(kVK_NUMPAD9) == "Numpad9");

    CHECK(InputCodeToToken(kVK_ADD) == "NumpadAdd");
    CHECK(InputCodeToToken(kVK_SUBTRACT) == "NumpadSubtract");
    CHECK(InputCodeToToken(kVK_MULTIPLY) == "NumpadMultiply");
    CHECK(InputCodeToToken(kVK_DIVIDE) == "NumpadDivide");
    CHECK(InputCodeToToken(kVK_DECIMAL) == "NumpadDecimal");
}

TEST_CASE("Numpad tokens round-trip")
{
    const std::uint32_t codes[] = {
        kVK_NUMPAD0,
        kVK_NUMPAD1,
        kVK_NUMPAD9,
        kVK_ADD,
        kVK_SUBTRACT,
        kVK_MULTIPLY,
        kVK_DIVIDE,
        kVK_DECIMAL,
        kVK_NUMLOCK,
    };

    for (auto c : codes)
    {
        const std::string token = InputCodeToToken(c);
        const auto parsed = ParseInputCodeToken(token);
        REQUIRE_MESSAGE(parsed.has_value(), token);
        CHECK_MESSAGE(*parsed == c, token);
    }
}

TEST_CASE("ParseChordString supports numpad tokens")
{
    std::vector<std::uint32_t> codes;
    CHECK(ParseChordString("Ctrl+Numpad0", codes));
    REQUIRE(codes.size() == 2);

    // Sorted order: Ctrl (0x11) then Numpad0 (0x60).
    CHECK(codes[0] == kVK_CONTROL);
    CHECK(codes[1] == kVK_NUMPAD0);
}
