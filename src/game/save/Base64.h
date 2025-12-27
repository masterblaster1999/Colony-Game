#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace colony::game::save {

// Small base64 helpers used for prototype save metadata.
//
// Notes:
//  - This is intentionally minimal (no streaming API).
//  - Decode ignores ASCII whitespace.
//  - Encode returns an empty string for empty input.

[[nodiscard]] std::string Base64Encode(const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::string Base64Encode(const std::vector<std::uint8_t>& bytes);

[[nodiscard]] bool Base64Decode(std::string_view b64, std::vector<std::uint8_t>& out);

} // namespace colony::game::save
