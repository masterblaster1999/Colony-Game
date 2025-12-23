#pragma once

#include "input/InputEvent.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <span>

namespace colony::input {

// High-level actions produced by the input mapper.
//
// For now we only need movement actions for the debug camera (WASD + QE),
// but this enum is the seam where future gameplay actions should be added.
enum class Action : std::uint8_t {
    MoveForward = 0,
    MoveBackward,
    MoveLeft,
    MoveRight,
    MoveDown,
    MoveUp,

    Count
};

struct MovementAxes {
    // x: strafe (right - left)
    // y: forward/back (forward - backward)
    // z: vertical (up - down)
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

// Maps raw key events to high-level actions.
//
// Notes:
//  - This intentionally stores Win32 virtual-key codes (0..255) as integers.
//    The rest of the game layer never sees Win32 types.
//  - Current implementation is designed for the prototype executable:
//    single-threaded, fed once per frame.
class InputMapper {
public:
    InputMapper() noexcept;

    void SetDefaultBinds() noexcept;

    // Clears all tracked key state (useful on focus loss).
    void ClearState() noexcept;

    void BindKey(Action action, std::uint32_t vk) noexcept;
    [[nodiscard]] std::uint32_t BoundKey(Action action) const noexcept;

    // Consumes key events and updates key state.
    // Returns true if any key state actually changed.
    bool Consume(std::span<const InputEvent> events) noexcept;

    [[nodiscard]] bool IsDown(Action action) const noexcept;

    [[nodiscard]] MovementAxes GetMovementAxes() const noexcept;

private:
    static constexpr std::size_t kVkCount = 256;

    static constexpr std::size_t ToIndex(Action a) noexcept
    {
        return static_cast<std::size_t>(a);
    }

    std::bitset<kVkCount> m_keysDown{};
    std::array<std::uint32_t, ToIndex(Action::Count)> m_binds{};
};

} // namespace colony::input
