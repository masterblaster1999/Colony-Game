#include "game/save/Base64.h"

#include <array>
#include <cctype>

namespace colony::game::save {

namespace {

constexpr char kEncodeTable[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

[[nodiscard]] constexpr bool IsWs(char c) noexcept
{
    return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

[[nodiscard]] const std::array<int, 256>& DecodeTable() noexcept
{
    static const std::array<int, 256> table = [] {
        std::array<int, 256> t{};
        t.fill(-1);
        for (int i = 0; i < 64; ++i)
            t[static_cast<unsigned char>(kEncodeTable[i])] = i;
        t[static_cast<unsigned char>('=')] = -2; // padding sentinel
        return t;
    }();
    return table;
}

} // namespace

std::string Base64Encode(const std::uint8_t* data, std::size_t size)
{
    if (data == nullptr || size == 0)
        return {};

    std::string out;
    out.reserve(((size + 2) / 3) * 4);

    for (std::size_t i = 0; i < size; i += 3)
    {
        const std::uint32_t b0 = data[i];
        const std::uint32_t b1 = (i + 1 < size) ? data[i + 1] : 0u;
        const std::uint32_t b2 = (i + 2 < size) ? data[i + 2] : 0u;

        const std::uint32_t n = (b0 << 16) | (b1 << 8) | b2;

        out.push_back(kEncodeTable[(n >> 18) & 63u]);
        out.push_back(kEncodeTable[(n >> 12) & 63u]);

        if (i + 1 < size)
            out.push_back(kEncodeTable[(n >> 6) & 63u]);
        else
            out.push_back('=');

        if (i + 2 < size)
            out.push_back(kEncodeTable[n & 63u]);
        else
            out.push_back('=');
    }

    return out;
}

std::string Base64Encode(const std::vector<std::uint8_t>& bytes)
{
    return Base64Encode(bytes.data(), bytes.size());
}

bool Base64Decode(std::string_view b64, std::vector<std::uint8_t>& out)
{
    out.clear();

    const auto& table = DecodeTable();

    int vals[4] = {0, 0, 0, 0};
    int count = 0;

    bool finished = false;

    for (std::size_t i = 0; i < b64.size(); ++i)
    {
        const char c = b64[i];
        if (IsWs(c))
            continue;

        if (finished)
        {
            // After padding, only whitespace is allowed.
            return false;
        }

        const int v = table[static_cast<unsigned char>(c)];
        if (v == -1)
            return false;

        vals[count++] = v;
        if (count != 4)
            continue;

        // Cannot start with padding.
        if (vals[0] < 0 || vals[1] < 0)
            return false;

        const std::uint32_t v2 = (vals[2] >= 0) ? static_cast<std::uint32_t>(vals[2]) : 0u;
        const std::uint32_t v3 = (vals[3] >= 0) ? static_cast<std::uint32_t>(vals[3]) : 0u;

        const std::uint32_t n = (static_cast<std::uint32_t>(vals[0]) << 18)
                              | (static_cast<std::uint32_t>(vals[1]) << 12)
                              | (v2 << 6)
                              | v3;

        out.push_back(static_cast<std::uint8_t>((n >> 16) & 0xFFu));

        if (vals[2] == -2)
        {
            // "xx==" -> 1 output byte. Must have two '='.
            if (vals[3] != -2)
                return false;
            finished = true;
        }
        else
        {
            out.push_back(static_cast<std::uint8_t>((n >> 8) & 0xFFu));

            if (vals[3] == -2)
            {
                // "xxx=" -> 2 output bytes.
                finished = true;
            }
            else
            {
                out.push_back(static_cast<std::uint8_t>(n & 0xFFu));
            }
        }

        count = 0;
    }

    // Trailing partial quartet is invalid.
    if (count != 0)
        return false;

    return true;
}

} // namespace colony::game::save
