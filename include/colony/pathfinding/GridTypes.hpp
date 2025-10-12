#pragma once
#include <cstdint>
#include <vector>
#include <limits>
#include <cassert>

namespace colony::pf {

using u8  = std::uint8_t;
using u32 = std::uint32_t;

struct IVec2 {
    int x{}, y{};
    constexpr bool operator==(const IVec2&) const = default;
};

struct Bounds {
    int w{}, h{};
    [[nodiscard]] constexpr bool contains(int x, int y) const noexcept {
        return (x >= 0 && y >= 0 && x < w && y < h);
    }
};

using NodeId = u32; // enough for maps up to ~65k x 65k

struct StepCost {
    float g{};      // path cost-so-far
    float f{};      // g + h
    NodeId parent{std::numeric_limits<NodeId>::max()};
};

constexpr NodeId kInvalid = std::numeric_limits<NodeId>::max();

// Encode/decode (x,y) <-> NodeId (row-major)
inline NodeId to_id(int x, int y, int width) { return static_cast<NodeId>(y * width + x); }
inline IVec2  from_id(NodeId id, int width)  { return { int(id % width), int(id / width) }; }

} // namespace colony::pf
