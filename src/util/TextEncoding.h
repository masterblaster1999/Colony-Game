#pragma once

// util/TextEncoding.h
// -------------------
// Small, dependency-free helpers for normalizing text files to UTF-8.
//
// Why this exists:
//   - On Windows, common editors (including Notepad) may save JSON/INI files with a BOM,
//     or as UTF-16 with a BOM.
//   - Many parsers (including strict JSON parsers) expect UTF-8 without a BOM.
//
// What we do:
//   - Strip UTF-8 BOM (EF BB BF) if present.
//   - If the file starts with a UTF-16 BOM (FF FE or FE FF), decode UTF-16 and re-encode as UTF-8.
//
// Design goals:
//   - Windows-friendly, but dependency-free (no <windows.h>, no deprecated <codecvt>).
//   - Never throws (returns false on malformed UTF-16 or allocation failure).

#include <cstddef>
#include <cstdint>
#include <string>

namespace colony::util {

namespace detail {

inline void AppendUtf8(std::string& out, std::uint32_t cp)
{
    if (cp <= 0x7Fu) {
        out.push_back(static_cast<char>(cp));
        return;
    }
    if (cp <= 0x7FFu) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        return;
    }
    if (cp <= 0xFFFFu) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        return;
    }

    out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
}

inline std::uint16_t ReadU16(const unsigned char* p, bool little) noexcept
{
    return little ? static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8))
                  : static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
}

inline bool ConvertUtf16BomToUtf8(std::string& bytes, bool little) noexcept
{
    const std::size_t n = bytes.size();
    if (n < 2)
        return false;

    const std::size_t payload = n - 2;
    if ((payload & 1u) != 0u)
        return false;

    const auto* p = reinterpret_cast<const unsigned char*>(bytes.data()) + 2;
    const std::size_t units = payload / 2;

    std::string out;
    out.reserve(units * 2); // heuristic (ASCII-heavy files)

    std::size_t i = 0;
    while (i < units)
    {
        const std::uint16_t w1 = ReadU16(p + (i * 2), little);
        ++i;

        std::uint32_t cp = w1;

        // UTF-16 surrogate pairs
        if (w1 >= 0xD800u && w1 <= 0xDBFFu)
        {
            if (i >= units)
                return false;

            const std::uint16_t w2 = ReadU16(p + (i * 2), little);
            if (w2 < 0xDC00u || w2 > 0xDFFFu)
                return false;

            cp = 0x10000u + ((static_cast<std::uint32_t>(w1 - 0xD800u) << 10)
                             | static_cast<std::uint32_t>(w2 - 0xDC00u));
            ++i;
        }
        else if (w1 >= 0xDC00u && w1 <= 0xDFFFu)
        {
            // Unpaired low surrogate
            return false;
        }

        if (cp > 0x10FFFFu)
            return false;

        AppendUtf8(out, cp);
    }

    bytes.swap(out);
    return true;
}

} // namespace detail

// Normalizes `bytes` in place so parsers can safely treat it as UTF-8 text.
//  - Strips UTF-8 BOM (EF BB BF) if present.
//  - If UTF-16LE/UTF-16BE BOM is present, converts to UTF-8.
// Returns false only for malformed UTF-16 or allocation failure.
inline bool NormalizeTextToUtf8(std::string& bytes) noexcept
{
    try
    {
        if (bytes.size() >= 3)
        {
            const unsigned char b0 = static_cast<unsigned char>(bytes[0]);
            const unsigned char b1 = static_cast<unsigned char>(bytes[1]);
            const unsigned char b2 = static_cast<unsigned char>(bytes[2]);

            if (b0 == 0xEFu && b1 == 0xBBu && b2 == 0xBFu)
            {
                bytes.erase(0, 3);
                return true;
            }
        }

        if (bytes.size() >= 2)
        {
            const unsigned char b0 = static_cast<unsigned char>(bytes[0]);
            const unsigned char b1 = static_cast<unsigned char>(bytes[1]);

            // UTF-16 BOMs
            if (b0 == 0xFFu && b1 == 0xFEu)
                return detail::ConvertUtf16BomToUtf8(bytes, /*little=*/true);
            if (b0 == 0xFEu && b1 == 0xFFu)
                return detail::ConvertUtf16BomToUtf8(bytes, /*little=*/false);
        }

        return true;
    }
    catch (...)
    {
        return false;
    }
}

} // namespace colony::util
