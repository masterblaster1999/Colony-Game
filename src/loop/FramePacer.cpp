#include "loop/FramePacer.h"

namespace colony::appwin {

FramePacer::FramePacer(int maxFpsWhenVsyncOff) noexcept
    : m_maxFpsWhenVsyncOff(maxFpsWhenVsyncOff)
{
    QueryPerformanceFrequency(&m_freq);
    RecomputeTicksPerFrame();

    ResetSchedule();
    ResetFps();
}

void FramePacer::SetMaxFpsWhenVsyncOff(int maxFpsWhenVsyncOff) noexcept
{
    // Clamp to a sane range. (0 == uncapped.)
    if (maxFpsWhenVsyncOff < 0)
        maxFpsWhenVsyncOff = 0;
    if (maxFpsWhenVsyncOff > 1000)
        maxFpsWhenVsyncOff = 1000;

    if (m_maxFpsWhenVsyncOff == maxFpsWhenVsyncOff)
        return;

    m_maxFpsWhenVsyncOff = maxFpsWhenVsyncOff;
    RecomputeTicksPerFrame();
    ResetSchedule();
}

void FramePacer::RecomputeTicksPerFrame() noexcept
{
    if (m_maxFpsWhenVsyncOff > 0) {
        m_ticksPerFrame = m_freq.QuadPart / static_cast<LONGLONG>(m_maxFpsWhenVsyncOff);
    } else {
        m_ticksPerFrame = 0;
    }
}

void FramePacer::ResetSchedule() noexcept
{
    m_nextFrameQpc = 0;
}

void FramePacer::ResetFps() noexcept
{
    QueryPerformanceCounter(&m_fpsStart);
    m_fpsFrames = 0;
    // Keep m_fps as-is; it will update after ~1 second.
}

void FramePacer::ThrottleBeforeMessagePump(bool vsync) noexcept
{
    if (vsync || m_ticksPerFrame <= 0)
        return;

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);

    if (m_nextFrameQpc == 0) {
        // First frame: render immediately.
        m_nextFrameQpc = now.QuadPart;
    }

    const LONGLONG remaining = m_nextFrameQpc - now.QuadPart;
    if (remaining <= 0)
        return;

    const DWORD waitMs = static_cast<DWORD>((remaining * 1000) / m_freq.QuadPart);
    if (waitMs > 0) {
        MsgWaitForMultipleObjectsEx(
            0,
            nullptr,
            waitMs,
            QS_ALLINPUT,
            MWMO_INPUTAVAILABLE
        );
    } else {
        // Very small remainder; yield to avoid hot spinning.
        Sleep(0);
    }
}

bool FramePacer::IsTimeToRender(bool vsync) noexcept
{
    if (vsync || m_ticksPerFrame <= 0)
        return true;

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);

    // If we woke due to messages, don't render early; wait until scheduled time.
    if (m_nextFrameQpc != 0 && now.QuadPart < m_nextFrameQpc) {
        return false;
    }

    return true;
}

bool FramePacer::OnFramePresented(bool vsync) noexcept
{
    ++m_fpsFrames;

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);

    bool fpsUpdated = false;
    const double elapsed = double(now.QuadPart - m_fpsStart.QuadPart) / double(m_freq.QuadPart);
    if (elapsed >= 1.0) {
        m_fps = (elapsed > 0.0) ? (double(m_fpsFrames) / elapsed) : 0.0;
        m_fpsFrames = 0;
        m_fpsStart = now;
        fpsUpdated = true;
    }

    if (!vsync && m_ticksPerFrame > 0)
    {
        if (m_nextFrameQpc == 0) {
            m_nextFrameQpc = now.QuadPart;
        }

        m_nextFrameQpc += m_ticksPerFrame;

        // If we're far behind (breakpoints / long hitch), resync to avoid a spiral.
        if (now.QuadPart > m_nextFrameQpc + (m_ticksPerFrame * 8)) {
            m_nextFrameQpc = now.QuadPart + m_ticksPerFrame;
        }
    }

    return fpsUpdated;
}

} // namespace colony::appwin
