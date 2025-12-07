// pathfinding/JpsTimersWin.hpp
//
// Windows-only QPC timers for JPS internals (opt-in).
// When COLONY_PF_TIMERS is defined, this header exposes:
//   - detail::JpsTimers           : per-thread accumulators (ns + counters)
//   - detail::ScopedQpc           : RAII scope to accumulate ns into a field
//   - detail::jps_timers()        : access to the thread_local JpsTimers
//   - detail::reset_jps_timers()  : zero the accumulators
//
// When COLONY_PF_TIMERS is NOT defined, all the above are no-ops and compile away.
//
// Usage inside Jps.cpp (examples):
//
//   #include "JpsTimersWin.hpp"
//
//   using namespace colony::path::detail;
//
//   // 1) Time a scoped region (e.g., jump()):
//   {
//       ScopedQpc _jt(jps_timers().jump_ns);
//       ++jps_timers().jumps;
//       // ... jump logic ...
//   }
//
//   // 2) Accumulate pop timing manually (example):
//   LARGE_INTEGER t0{}, t1{};
//   QueryPerformanceCounter(&t0);
//   // pop from open list...
//   QueryPerformanceCounter(&t1);
//   jps_timers().pop_ns += Qpc::instance().to_ns(t1.QuadPart - t0.QuadPart);
//   ++jps_timers().pops;
//
//   // 3) Reset at the start of a frame / path query:
//   reset_jps_timers();
//
// Notes:
// - QueryPerformanceCounter (QPC) and QueryPerformanceFrequency (QPF) are the
//   recommended high-resolution timers for Windows user-mode profiling. The frequency
//   is fixed at boot and is consistent across processors, so cache it once and reuse.
// - This header keeps all Windows includes gated behind COLONY_PF_TIMERS to avoid
//   leaking <windows.h> into translation units that don't need it.
//
// References:
// - Microsoft: "Acquiring high-resolution time stamps" (QPC/QPF overview).
// - Microsoft: QueryPerformanceCounter / QueryPerformanceFrequency reference.
//
// (c) Colony-Game contributors. MIT/Unlicense as applicable to the project.

#pragma once
#include <cstdint>

namespace colony::path {
namespace detail {

struct JpsTimers {
    // Accumulated nanoseconds spent in each category (per-thread)
    long long pop_ns    = 0;  // open-list pop time
    long long jump_ns   = 0;  // jump() expansion time
    long long smooth_ns = 0;  // LOS/smoothing time

    // Event counters (per-thread)
    int pops   = 0;
    int jumps  = 0;

    void reset() noexcept {
        pop_ns = jump_ns = smooth_ns = 0;
        pops = jumps = 0;
    }
};

#if defined(COLONY_PF_TIMERS) && defined(_WIN32)

// Keep Windows includes contained and lean.
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// Thread-local accumulators so multiple threads can profile independently.
inline thread_local JpsTimers g_jps_timers;

// High-resolution clock wrapper (QueryPerformanceCounter/Frequency).
// Frequency is fixed at boot and can be cached once.
class Qpc {
public:
    static const Qpc& instance() {
        static Qpc q; // thread-safe since C++11
        return q;
    }

    // Convert a tick delta to nanoseconds using cached frequency.
    long long to_ns(LONGLONG ticks) const noexcept {
        // Note: (ticks * 1'000'000'000) / freq is safe for typical short intervals.
        // For very long spans, prefer chunked accumulation; here we time short scopes.
        return (ticks * 1000000000LL) / freq_.QuadPart;
    }

private:
    LARGE_INTEGER freq_{};

    Qpc() noexcept {
        // QPF is guaranteed to succeed on supported systems; guard just in case.
        if (!::QueryPerformanceFrequency(&freq_) || freq_.QuadPart <= 0) {
            // Fallback to 1 (ns == ticks), which keeps math defined; timings become arbitrary.
            freq_.QuadPart = 1;
        }
    }
};

// RAII scope that measures a code section and adds elapsed ns to an accumulator.
class ScopedQpc {
public:
    explicit ScopedQpc(long long& accumulator) noexcept
        : acc_(accumulator) {
        ::QueryPerformanceCounter(&start_);
    }

    ~ScopedQpc() {
        LARGE_INTEGER end{};
        ::QueryPerformanceCounter(&end);
        const LONGLONG delta = end.QuadPart - start_.QuadPart;
        acc_ += Qpc::instance().to_ns(delta);
    }

    ScopedQpc(const ScopedQpc&)            = delete;
    ScopedQpc& operator=(const ScopedQpc&) = delete;

private:
    long long&     acc_;
    LARGE_INTEGER  start_{};
};

// Access/reset helpers
inline JpsTimers& jps_timers() noexcept { return g_jps_timers; }
inline void reset_jps_timers() noexcept { g_jps_timers.reset(); }

#else  // !COLONY_PF_TIMERS || !_WIN32

// Stubs: compile away in non-profiling or non-Windows builds.
class Qpc {
public:
    static const Qpc& instance() { static Qpc q; return q; }
    long long to_ns(long long) const noexcept { return 0; }
};

class ScopedQpc {
public:
    explicit ScopedQpc(long long&) noexcept {}
};

inline JpsTimers& jps_timers() noexcept {
    static thread_local JpsTimers dummy;
    return dummy;
}
inline void reset_jps_timers() noexcept {
    jps_timers().reset();
}

#endif // COLONY_PF_TIMERS && _WIN32

} // namespace detail
} // namespace colony::path
