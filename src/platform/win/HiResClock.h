// src/platform/win/HiResClock.h
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <cstdint>

struct HiResClock
{
    HiResClock() noexcept { Reset(); }

    void Reset() noexcept
    {
        ::QueryPerformanceCounter(&m_last);
    }

    // Returns delta time in seconds since last Tick/Reset.
    double Tick() noexcept
    {
        LARGE_INTEGER now{};
        ::QueryPerformanceCounter(&now);

        const auto freq = Frequency();
        const double dt = (freq.QuadPart > 0)
            ? double(now.QuadPart - m_last.QuadPart) / double(freq.QuadPart)
            : 0.0;

        m_last = now;
        return dt;
    }

    static LARGE_INTEGER Frequency() noexcept
    {
        static LARGE_INTEGER s_freq = []() {
            LARGE_INTEGER f{};
            ::QueryPerformanceFrequency(&f);
            return f;
        }();
        return s_freq;
    }

private:
    LARGE_INTEGER m_last{};
};
