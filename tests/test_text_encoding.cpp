// tests/test_text_encoding.cpp
//
// Regression tests for colony::util::NormalizeTextToUtf8() (src/util/TextEncoding.h).
//
// The project loads several user-editable text files (settings.json, input_bindings.json, saves).
// On Windows, common editors can write UTF-8 with BOM, UTF-16 with BOM, or (rarely) UTF-32 with BOM.
// NormalizeTextToUtf8 makes those files safe to parse as UTF-8.

#include <doctest/doctest.h>

#include "util/TextEncoding.h"

#include <cstdint>
#include <string>

namespace {

std::string MakeUtf16WithBomLE(const std::u16string& s)
{
    std::string bytes;
    bytes.reserve(2 + (s.size() * 2));
    bytes.push_back(static_cast<char>(0xFF));
    bytes.push_back(static_cast<char>(0xFE));
    for (const char16_t cu : s)
    {
        bytes.push_back(static_cast<char>(static_cast<std::uint16_t>(cu) & 0xFFu));
        bytes.push_back(static_cast<char>((static_cast<std::uint16_t>(cu) >> 8) & 0xFFu));
    }
    return bytes;
}

std::string MakeUtf16WithBomBE(const std::u16string& s)
{
    std::string bytes;
    bytes.reserve(2 + (s.size() * 2));
    bytes.push_back(static_cast<char>(0xFE));
    bytes.push_back(static_cast<char>(0xFF));
    for (const char16_t cu : s)
    {
        bytes.push_back(static_cast<char>((static_cast<std::uint16_t>(cu) >> 8) & 0xFFu));
        bytes.push_back(static_cast<char>(static_cast<std::uint16_t>(cu) & 0xFFu));
    }
    return bytes;
}

std::string MakeUtf32WithBomLE(const std::u32string& s)
{
    std::string bytes;
    bytes.reserve(4 + (s.size() * 4));

    // UTF-32LE BOM: FF FE 00 00
    bytes.push_back(static_cast<char>(0xFF));
    bytes.push_back(static_cast<char>(0xFE));
    bytes.push_back(static_cast<char>(0x00));
    bytes.push_back(static_cast<char>(0x00));

    for (const char32_t cu : s)
    {
        const std::uint32_t v = static_cast<std::uint32_t>(cu);
        bytes.push_back(static_cast<char>((v      ) & 0xFFu));
        bytes.push_back(static_cast<char>((v >>  8) & 0xFFu));
        bytes.push_back(static_cast<char>((v >> 16) & 0xFFu));
        bytes.push_back(static_cast<char>((v >> 24) & 0xFFu));
    }
    return bytes;
}

std::string MakeUtf32WithBomBE(const std::u32string& s)
{
    std::string bytes;
    bytes.reserve(4 + (s.size() * 4));

    // UTF-32BE BOM: 00 00 FE FF
    bytes.push_back(static_cast<char>(0x00));
    bytes.push_back(static_cast<char>(0x00));
    bytes.push_back(static_cast<char>(0xFE));
    bytes.push_back(static_cast<char>(0xFF));

    for (const char32_t cu : s)
    {
        const std::uint32_t v = static_cast<std::uint32_t>(cu);
        bytes.push_back(static_cast<char>((v >> 24) & 0xFFu));
        bytes.push_back(static_cast<char>((v >> 16) & 0xFFu));
        bytes.push_back(static_cast<char>((v >>  8) & 0xFFu));
        bytes.push_back(static_cast<char>((v      ) & 0xFFu));
    }
    return bytes;
}

} // namespace

TEST_CASE("NormalizeTextToUtf8 strips UTF-8 BOM")
{
    std::string s;
    s.append("\xEF\xBB\xBF");
    s.append("{\"a\":1}\n");

    CHECK(colony::util::NormalizeTextToUtf8(s));
    CHECK(s == "{\"a\":1}\n");
}

TEST_CASE("NormalizeTextToUtf8 converts UTF-16LE BOM to UTF-8")
{
    std::string bytes = MakeUtf16WithBomLE(u"{\"hello\":\"world\"}\n");

    CHECK(colony::util::NormalizeTextToUtf8(bytes));
    CHECK(bytes == "{\"hello\":\"world\"}\n");
}

TEST_CASE("NormalizeTextToUtf8 converts UTF-16BE BOM to UTF-8")
{
    std::string bytes = MakeUtf16WithBomBE(u"{\"hello\":\"world\"}\n");

    CHECK(colony::util::NormalizeTextToUtf8(bytes));
    CHECK(bytes == "{\"hello\":\"world\"}\n");
}

TEST_CASE("NormalizeTextToUtf8 converts UTF-32LE BOM to UTF-8")
{
    std::string bytes = MakeUtf32WithBomLE(U"{\"hello\":\"world\"}\n");

    CHECK(colony::util::NormalizeTextToUtf8(bytes));
    CHECK(bytes == "{\"hello\":\"world\"}\n");
}

TEST_CASE("NormalizeTextToUtf8 converts UTF-32BE BOM to UTF-8")
{
    std::string bytes = MakeUtf32WithBomBE(U"{\"hello\":\"world\"}\n");

    CHECK(colony::util::NormalizeTextToUtf8(bytes));
    CHECK(bytes == "{\"hello\":\"world\"}\n");
}

TEST_CASE("NormalizeTextToUtf8 converts surrogate pairs correctly (U+1F600)")
{
    // U+1F600 GRINNING FACE 😀
    // UTF-16 surrogate pair: D83D DE00
    std::u16string u;
    u.push_back(static_cast<char16_t>(0xD83D));
    u.push_back(static_cast<char16_t>(0xDE00));

    std::string bytes = MakeUtf16WithBomLE(u);

    CHECK(colony::util::NormalizeTextToUtf8(bytes));

    // UTF-8: F0 9F 98 80
    std::string expected;
    expected.append("\xF0\x9F\x98\x80");
    CHECK(bytes == expected);
}

TEST_CASE("NormalizeTextToUtf8 converts UTF-32 code points correctly (U+1F600)")
{
    // U+1F600 GRINNING FACE 😀
    std::u32string u;
    u.push_back(static_cast<char32_t>(0x1F600));

    std::string bytes = MakeUtf32WithBomLE(u);

    CHECK(colony::util::NormalizeTextToUtf8(bytes));

    // UTF-8: F0 9F 98 80
    std::string expected;
    expected.append("\xF0\x9F\x98\x80");
    CHECK(bytes == expected);
}

TEST_CASE("NormalizeTextToUtf8 rejects malformed UTF-16")
{
    SUBCASE("odd payload length")
    {
        std::string bytes;
        bytes.push_back(static_cast<char>(0xFF));
        bytes.push_back(static_cast<char>(0xFE));
        bytes.push_back(static_cast<char>(0x41)); // dangling byte
        CHECK_FALSE(colony::util::NormalizeTextToUtf8(bytes));
    }

    SUBCASE("unpaired high surrogate")
    {
        std::u16string u;
        u.push_back(static_cast<char16_t>(0xD83D)); // high surrogate
        // Missing low surrogate
        std::string bytes = MakeUtf16WithBomLE(u);
        CHECK_FALSE(colony::util::NormalizeTextToUtf8(bytes));
    }

    SUBCASE("unpaired low surrogate")
    {
        std::u16string u;
        u.push_back(static_cast<char16_t>(0xDE00)); // low surrogate without high
        std::string bytes = MakeUtf16WithBomLE(u);
        CHECK_FALSE(colony::util::NormalizeTextToUtf8(bytes));
    }

    SUBCASE("high surrogate followed by non-low surrogate")
    {
        std::u16string u;
        u.push_back(static_cast<char16_t>(0xD83D)); // high surrogate
        u.push_back(static_cast<char16_t>('A'));    // not a low surrogate
        std::string bytes = MakeUtf16WithBomLE(u);
        CHECK_FALSE(colony::util::NormalizeTextToUtf8(bytes));
    }
}

TEST_CASE("NormalizeTextToUtf8 rejects malformed UTF-32")
{
    SUBCASE("payload length not divisible by 4")
    {
        std::string bytes;
        bytes.push_back(static_cast<char>(0xFF));
        bytes.push_back(static_cast<char>(0xFE));
        bytes.push_back(static_cast<char>(0x00));
        bytes.push_back(static_cast<char>(0x00));
        bytes.push_back(static_cast<char>(0x41));
        bytes.push_back(static_cast<char>(0x00));
        bytes.push_back(static_cast<char>(0x00)); // 3-byte payload (invalid)
        CHECK_FALSE(colony::util::NormalizeTextToUtf8(bytes));
    }

    SUBCASE("surrogate code point is rejected")
    {
        std::u32string u;
        u.push_back(static_cast<char32_t>(0xD800));
        std::string bytes = MakeUtf32WithBomLE(u);
        CHECK_FALSE(colony::util::NormalizeTextToUtf8(bytes));
    }

    SUBCASE("out-of-range code point is rejected")
    {
        std::u32string u;
        u.push_back(static_cast<char32_t>(0x110000));
        std::string bytes = MakeUtf32WithBomBE(u);
        CHECK_FALSE(colony::util::NormalizeTextToUtf8(bytes));
    }
}
