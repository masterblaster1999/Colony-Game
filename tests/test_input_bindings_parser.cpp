// tests/test_input_bindings_parser.cpp

#include <doctest/doctest.h>

#include "input/InputBindingParse.h"

using colony::input::bindings::ParseChordString;
using colony::input::bindings::ParseInputCodeToken;

TEST_SUITE("InputBindingParse") {

TEST_CASE("ParseInputCodeToken: single characters") {
    CHECK(ParseInputCodeToken("w").value() == static_cast<std::uint32_t>('W'));
    CHECK(ParseInputCodeToken("W").value() == static_cast<std::uint32_t>('W'));
    CHECK(ParseInputCodeToken("1").value() == static_cast<std::uint32_t>('1'));
}

TEST_CASE("ParseInputCodeToken: common named keys") {
    using namespace colony::input::bindings;

    CHECK(ParseInputCodeToken("Space").value() == kVK_SPACE);
    CHECK(ParseInputCodeToken("Esc").value() == kVK_ESCAPE);
    CHECK(ParseInputCodeToken("Enter").value() == kVK_RETURN);
    CHECK(ParseInputCodeToken("Tab").value() == kVK_TAB);
    CHECK(ParseInputCodeToken("Backspace").value() == kVK_BACK);

    CHECK(ParseInputCodeToken("Up").value() == kVK_UP);
    CHECK(ParseInputCodeToken("ArrowDown").value() == kVK_DOWN);
    CHECK(ParseInputCodeToken("Left").value() == kVK_LEFT);
    CHECK(ParseInputCodeToken("Right").value() == kVK_RIGHT);

    CHECK(ParseInputCodeToken("PageUp").value() == kVK_PRIOR);
    CHECK(ParseInputCodeToken("PageDown").value() == kVK_NEXT);
    CHECK(ParseInputCodeToken("Home").value() == kVK_HOME);
    CHECK(ParseInputCodeToken("End").value() == kVK_END);
    CHECK(ParseInputCodeToken("Insert").value() == kVK_INSERT);
    CHECK(ParseInputCodeToken("Delete").value() == kVK_DELETE);
}

TEST_CASE("ParseInputCodeToken: modifiers") {
    using namespace colony::input::bindings;

    CHECK(ParseInputCodeToken("Shift").value() == kVK_SHIFT);
    CHECK(ParseInputCodeToken("LShift").value() == kVK_LSHIFT);
    CHECK(ParseInputCodeToken("RShift").value() == kVK_RSHIFT);

    CHECK(ParseInputCodeToken("Ctrl").value() == kVK_CONTROL);
    CHECK(ParseInputCodeToken("LeftCtrl").value() == kVK_LCONTROL);
    CHECK(ParseInputCodeToken("RightCtrl").value() == kVK_RCONTROL);

    CHECK(ParseInputCodeToken("Alt").value() == kVK_MENU);
    CHECK(ParseInputCodeToken("LeftAlt").value() == kVK_LMENU);
    CHECK(ParseInputCodeToken("RightAlt").value() == kVK_RMENU);
}

TEST_CASE("ParseInputCodeToken: function keys") {
    using namespace colony::input::bindings;

    CHECK(ParseInputCodeToken("F1").value() == kVK_F1);
    CHECK(ParseInputCodeToken("F2").value() == kVK_F2);
    CHECK(ParseInputCodeToken("f5").value() == (kVK_F1 + 4));
    CHECK(ParseInputCodeToken("F24").value() == kVK_F24);
    CHECK_FALSE(ParseInputCodeToken("F0").has_value());
    CHECK_FALSE(ParseInputCodeToken("F25").has_value());
}

TEST_CASE("ParseInputCodeToken: mouse buttons") {
    CHECK(ParseInputCodeToken("MouseLeft").value() == colony::input::kMouseButtonLeft);
    CHECK(ParseInputCodeToken("RMB").value() == colony::input::kMouseButtonRight);
    CHECK(ParseInputCodeToken("MouseMiddle").value() == colony::input::kMouseButtonMiddle);
    CHECK(ParseInputCodeToken("MouseX1").value() == colony::input::kMouseButtonX1);
    CHECK(ParseInputCodeToken("MouseX2").value() == colony::input::kMouseButtonX2);
}

TEST_CASE("ParseInputCodeToken: mouse wheel") {
    CHECK(ParseInputCodeToken("WheelUp").value() == colony::input::kMouseWheelUp);
    CHECK(ParseInputCodeToken("WheelDown").value() == colony::input::kMouseWheelDown);
    // Common aliases
    CHECK(ParseInputCodeToken("MouseWheelUp").value() == colony::input::kMouseWheelUp);
    CHECK(ParseInputCodeToken("ScrollDown").value() == colony::input::kMouseWheelDown);
}

TEST_CASE("ParseInputCodeToken: invalid") {
    CHECK_FALSE(ParseInputCodeToken("").has_value());
    CHECK_FALSE(ParseInputCodeToken(" ").has_value());
    CHECK_FALSE(ParseInputCodeToken("NotAKey").has_value());
}

TEST_CASE("ParseChordString: basic + dedupe") {
    std::vector<std::uint32_t> codes;
    CHECK(ParseChordString("Shift+W", codes));
    REQUIRE(codes.size() == 2);
    CHECK(codes[0] == colony::input::bindings::kVK_SHIFT);
    CHECK(codes[1] == static_cast<std::uint32_t>('W'));

    // Duplicate tokens should be removed.
    CHECK(ParseChordString(" W + Shift + W ", codes));
    REQUIRE(codes.size() == 2);
    CHECK(codes[0] == colony::input::bindings::kVK_SHIFT);
    CHECK(codes[1] == static_cast<std::uint32_t>('W'));
}

TEST_CASE("ParseChordString: mixed keyboard + mouse") {
    std::vector<std::uint32_t> codes;
    CHECK(ParseChordString("Alt+MouseLeft", codes));
    REQUIRE(codes.size() == 2);
    CHECK(codes[0] == colony::input::bindings::kVK_MENU);
    CHECK(codes[1] == colony::input::kMouseButtonLeft);
}

TEST_CASE("ParseChordString: mixed keyboard + mouse wheel") {
    std::vector<std::uint32_t> codes;
    CHECK(ParseChordString("Ctrl+WheelUp", codes));
    REQUIRE(codes.size() == 2);
    CHECK(codes[0] == colony::input::bindings::kVK_CONTROL);
    CHECK(codes[1] == colony::input::kMouseWheelUp);
}

TEST_CASE("ParseChordString: invalid token fails") {
    std::vector<std::uint32_t> codes;
    CHECK_FALSE(ParseChordString("Shift+NotAKey", codes));
    CHECK(codes.empty());
}

} // TEST_SUITE
