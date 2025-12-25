#pragma once

#include "input/InputEvent.h"

#include <array>
#include <cstddef>
#include <span>

namespace colony::input {

// Fixed-capacity per-frame input event queue.
//
// For now this is single-threaded and owned by the AppWindow.
// If you later move simulation to a worker thread, this can be replaced
// with an SPSC ring buffer without changing the InputEvent type.
class InputQueue {
public:
    // High polling-rate mice can generate very large WM_INPUT bursts; keep this
    // comfortably above 1k to avoid dropped input in worst-case stutters.
    static constexpr std::size_t kMaxEvents = 4096;

    void Clear() noexcept { m_count = 0; }

    void Push(const InputEvent& ev) noexcept
    {
        if (m_count < kMaxEvents) {
            m_events[m_count++] = ev;
        } else {
            ++m_dropped;
        }
    }

    [[nodiscard]] std::span<const InputEvent> Events() const noexcept
    {
        return std::span<const InputEvent>(m_events.data(), m_count);
    }

    [[nodiscard]] std::size_t Count() const noexcept { return m_count; }
    [[nodiscard]] std::size_t Dropped() const noexcept { return m_dropped; }

private:
    std::array<InputEvent, kMaxEvents> m_events{};
    std::size_t m_count = 0;
    std::size_t m_dropped = 0;
};

} // namespace colony::input
