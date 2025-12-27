#pragma once

#include "platform/win/WinCommon.h"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
    // Available on Windows 10, version 1803+. Older SDKs may not define it.
    #define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

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

    ~FramePacer() noexcept;

    FramePacer(const FramePacer&) = delete;
    FramePacer& operator=(const FramePacer&) = delete;
    FramePacer(FramePacer&&) = delete;
    FramePacer& operator=(FramePacer&&) = delete;

    // Update the safety cap used when vsync is OFF.
    //
    //  - 0 means uncapped (not recommended; can peg a CPU core)
    //  - negative values are treated as 0
    //  - very large values are clamped to a reasonable upper bound
    void SetMaxFpsWhenVsyncOff(int maxFpsWhenVsyncOff) noexcept;
    [[nodiscard]] int MaxFpsWhenVsyncOff() const noexcept { return m_maxFpsWhenVsyncOff; }

    // Optional background FPS cap used when the window is *unfocused* but still
    // running (i.e., not paused). This cap can apply even when vsync is ON,
    // which reduces unnecessary GPU work when the app is in the background.
    //
    //  - 0 means uncapped
    //  - negative values are treated as 0
    //  - very large values are clamped
    void SetMaxFpsWhenUnfocused(int maxFpsWhenUnfocused) noexcept;
    [[nodiscard]] int MaxFpsWhenUnfocused() const noexcept { return m_maxFpsWhenUnfocused; }

    void ResetSchedule() noexcept;
    void ResetFps() noexcept;

    // Call before pumping messages. If a cap is active (vsync OFF cap, or the
    // optional unfocused cap), this waits until the next scheduled frame time
    // OR until messages arrive.
    void ThrottleBeforeMessagePump(bool vsync, bool unfocused) noexcept;

    // Call after pumping messages. Returns false when a cap is active and
    // we're still too early to render.
    bool IsTimeToRender(bool vsync, bool unfocused) noexcept;

    // Call after rendering/presenting. Returns true when FPS was updated.
    bool OnFramePresented(bool vsync, bool unfocused) noexcept;

    [[nodiscard]] double Fps() const noexcept { return m_fps; }

private:
    void RecomputeTicksPerFrame() noexcept;
    [[nodiscard]] LONGLONG ActiveTicksPerFrame(bool vsync, bool unfocused) const noexcept;

    void EnsureWaitableTimer() noexcept;


    LARGE_INTEGER m_freq{};
    LONGLONG m_ticksPerFrameVsyncOff = 0;
    LONGLONG m_ticksPerFrameUnfocused = 0;
    LONGLONG m_nextFrameQpc = 0;

    // Optional high-resolution waitable timer for more accurate sleeping than a millisecond-granularity
    // MsgWaitForMultipleObjectsEx timeout. Created lazily.
    HANDLE m_waitableTimer = nullptr;

    LARGE_INTEGER m_fpsStart{};
    int m_fpsFrames = 0;
    double m_fps = 0.0;

    int m_maxFpsWhenVsyncOff = 240;
    int m_maxFpsWhenUnfocused = 30;
};

} // namespace colony::appwin
