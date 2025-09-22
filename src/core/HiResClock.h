// HiResClock.h
#pragma once
#include <cstdint>

struct HiResClock {
    static void init();                  // call once on startup
    static double seconds();             // monotonically increasing seconds
    static uint64_t ticks();             // raw ticks
    static uint64_t freq();              // ticks per second
    static void shutdown();              // pairs timeBeginPeriod/timeEndPeriod
};
