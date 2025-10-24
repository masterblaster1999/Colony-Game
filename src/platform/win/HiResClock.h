#pragma once
#include <windows.h>
#include <cstdint>

class HiResClock {
public:
    HiResClock() {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        freq_ = static_cast<double>(f.QuadPart);
        Reset();
    }
    void Reset() {
        QueryPerformanceCounter(&last_);
    }
    // Seconds since last call to Tick()
    double Tick() {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const double dt = (static_cast<double>(now.QuadPart - last_.QuadPart)) / freq_;
        last_ = now;
        return dt;
    }
private:
    LARGE_INTEGER last_{};
    double freq_{};
};
