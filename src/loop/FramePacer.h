#pragma once

#include <windows.h>

namespace colony::appwin {

// High-resolution frame pacing helper for the prototype message loop.
//
// Mirrors the behavior that previously lived directly in AppWindow.cpp:
//  - If vsync is OFF, cap to a conservative max FPS to avoid pegging a CPU core
//  - Use MsgWaitForMultipleObjectsEx so we stay responsive to Windows messages
//  - Track a simple FPS estimate (updated about once per second)
class FramePacer {
public:
    explicit FramePacer(int maxFpsWhenVsyncOff = 240) noexcept;

    void ResetSchedule() noexcept;
    void ResetFps() noexcept;

    // Call before pumping messages. This waits (vsync OFF only) until the next
    // scheduled frame time OR until messages arrive.
    void ThrottleBeforeMessagePump(bool vsync) noexcept;

    // Call after pumping messages. Returns false when vsync is OFF and we're
    // still too early to render.
    bool IsTimeToRender(bool vsync) noexcept;

    // Call after rendering/presenting. Returns true when FPS was updated.
    bool OnFramePresented(bool vsync) noexcept;

    [[nodiscard]] double Fps() const noexcept { return m_fps; }

private:
    void RecomputeTicksPerFrame() noexcept;

    LARGE_INTEGER m_freq{};
    LONGLONG m_ticksPerFrame = 0;
    LONGLONG m_nextFrameQpc = 0;

    LARGE_INTEGER m_fpsStart{};
    int m_fpsFrames = 0;
    double m_fps = 0.0;

    int m_maxFpsWhenVsyncOff = 240;
};

} // namespace colony::appwin
