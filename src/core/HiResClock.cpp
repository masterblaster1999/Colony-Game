// HiResClock.cpp
#include "core/HiResClock.h"
#include <windows.h>

static LARGE_INTEGER gFreq{};
static bool gPeriod1 = false;

void HiResClock::init() {
    QueryPerformanceFrequency(&gFreq);
    // Optional: improve Sleep granularity. Must be balanced by timeEndPeriod.
    if (timeBeginPeriod(1) == TIMERR_NOERROR) gPeriod1 = true; // must match with timeEndPeriod
}

uint64_t HiResClock::freq() { return (uint64_t)gFreq.QuadPart; }

uint64_t HiResClock::ticks() {
    LARGE_INTEGER t{}; QueryPerformanceCounter(&t); return (uint64_t)t.QuadPart;
}

double HiResClock::seconds() {
    return double(ticks()) / double(freq());
}

void HiResClock::shutdown() {
    if (gPeriod1) { timeEndPeriod(1); gPeriod1 = false; }
}
