// src/platform/win/HiResClock.h
#pragma once
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>

struct HiResClock
{
    // Query timer frequency once, lazily, and cache it.
    static int64_t freq() noexcept
    {
        static int64_t f = []{
            LARGE_INTEGER li{};
            ::QueryPerformanceFrequency(&li);
            return li.QuadPart;
        }();
        return f;
    }

    static int64_t ticks() noexcept
    {
        LARGE_INTEGER t{};
        ::QueryPerformanceCounter(&t);
        return t.QuadPart;
    }

    static double seconds() noexcept
    {
        // Convert to double only at the edge.
        return static_cast<double>(ticks()) / static_cast<double>(freq());
    }

    static double millis() noexcept { return seconds() * 1000.0; }
    static double micros() noexcept { return seconds() * 1'000'000.0; }
};
