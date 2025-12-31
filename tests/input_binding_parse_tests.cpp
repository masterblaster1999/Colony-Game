#include <doctest/doctest.h>

#include "input/InputBindingParse.h"

#include <algorithm>
#include <span>
#include <string>
#include <vector>

namespace bp = colony::input::bindings;

static std::string joinChord(std::span<const std::uint32_t> codes)
{
    std::string out;
    for (std::size_t i = 0; i < codes.size(); ++i)
    {
        if (i != 0)
            out.push_back('+');
        out += bp::InputCodeToToken(codes[i]);
    }
    return out;
}

TEST_CASE("ParseChordString parses modifiers + key")
{
    std::vector<std::uint32_t> codes;
    REQUIRE(bp::ParseChordString("Ctrl+Shift+Z", codes));

    // ParseChordString canonicalizes (sort + unique).
    const std::vector<std::uint32_t> expected = {bp::kVK_SHIFT, bp::kVK_CONTROL, static_cast<std::uint32_t>('Z')};
    CHECK(codes == expected);
}

TEST_CASE("InputCodeToToken produces readable tokens")
{
    CHECK(bp::InputCodeToToken(bp::kVK_CONTROL) == "Ctrl");
    CHECK(bp::InputCodeToToken(bp::kVK_SHIFT) == "Shift");
    CHECK(bp::InputCodeToToken(static_cast<std::uint32_t>('W')) == "W");
    CHECK(bp::InputCodeToToken(bp::kVK_ESCAPE) == "Esc");
}

TEST_CASE("Wheel tokens are parseable")
{
    std::vector<std::uint32_t> codes;
    REQUIRE(bp::ParseChordString("Ctrl+WheelUp", codes));

    // NOTE: ordering is numeric (VK_CONTROL is < mouse code base), so Ctrl comes first.
    const std::vector<std::uint32_t> expected = {bp::kVK_CONTROL, colony::input::kMouseWheelUp};
    CHECK(codes == expected);
}

TEST_CASE("Chord tokenization roundtrip")
{
    const std::vector<std::uint32_t> original = {
        bp::kVK_CONTROL,
        bp::kVK_SHIFT,
        static_cast<std::uint32_t>('S'),
    };

    const std::string text = joinChord(original);

    std::vector<std::uint32_t> parsed;
    REQUIRE(bp::ParseChordString(text, parsed));

    std::vector<std::uint32_t> expected = original;
    std::sort(expected.begin(), expected.end());
    expected.erase(std::unique(expected.begin(), expected.end()), expected.end());

    CHECK(parsed == expected);
}
