// HiResClock.cpp — Windows-only high-resolution timing utilities
//
// Public API preserved: HiResClock::init(), freq(), ticks(), seconds(), shutdown().
// Extras are provided under namespace cg::hires (frame pacing, precise sleep, conversions, profiling).
//
// Key Windows APIs used (see references below):
//  - QueryPerformanceCounter / QueryPerformanceFrequency (QPC) for high-res ticks.
//  - timeBeginPeriod/timeEndPeriod (winmm) to request 1ms scheduler granularity (paired).
//  - CreateWaitableTimerEx / SetWaitableTimerEx for high-resolution waitable timers (Win10 1803+).
//  - QueryUnbiasedInterruptTimePrecise: sleep/hibernate-agnostic monotonic time.
//
// This file intentionally avoids modifying the HiResClock class interface; you can expose
// selected cg::hires helpers later by adding declarations in HiResClock.h.

#include "core/HiResClock.h"

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <mmsystem.h>        // timeBeginPeriod, timeEndPeriod
#include <synchapi.h>        // Waitable timers
#include <realtimeapiset.h>  // QueryUnbiasedInterruptTime*, SetWaitableTimerEx
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <string>
#include <sstream>
#include <limits>
#include <vector>

#if defined(_MSC_VER)
#  include <intrin.h>        // _mm_pause / YieldProcessor
#endif

// Prefer linking winmm in CMake; keep pragma as a safety net for local builds.
#ifndef CG_SUPPRESS_PRAGMA_LIBS
#  pragma comment(lib, "winmm.lib")
#endif

// Some SDKs may not define these in older headers; provide constants to compile cleanly.
#ifndef CREATE_WAITABLE_TIMER_MANUAL_RESET
#  define CREATE_WAITABLE_TIMER_MANUAL_RESET 0x00000001
#endif
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#  define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

// -------------------------------------------------------------------------------------------------
// Global state for QPC and timer period
// -------------------------------------------------------------------------------------------------
static LARGE_INTEGER                gFreqLI{};          // QPC frequency (ticks per second)
static long double                  gInvFreq = 0.0L;    // 1.0 / frequency (for fast conversions)
static std::atomic<uint32_t>        gInitRefs{0};       // ref-count init/shutdown
static bool                         gPeriod1 = false;   // did we successfully request 1ms period?
static uint64_t                     gInitQpc = 0;       // QPC at init (epoch for 'since start')

// -------------------------------------------------------------------------------------------------
// Internal helpers (implementation detail)
// -------------------------------------------------------------------------------------------------
namespace cg_detail {

    // RAII guard for timeBeginPeriod/timeEndPeriod
    struct TimerPeriodGuard {
        UINT period = 1;
        bool active = false;
        TimerPeriodGuard() {
            active = (timeBeginPeriod(period) == TIMERR_NOERROR);
        }
        ~TimerPeriodGuard() {
            if (active) {
                timeEndPeriod(period);
                active = false;
            }
        }
    };

    // Dynamically resolve kernel32 exports (for graceful fallback).
    template <typename Fn>
    static Fn load_kernel32(const char* name) {
        HMODULE k32 = ::GetModuleHandleW(L"kernel32.dll");
        return reinterpret_cast<Fn>(::GetProcAddress(k32, name));
    }

    // QPC now (tick count)
    static inline uint64_t qpc_now() noexcept {
        LARGE_INTEGER t{};
        ::QueryPerformanceCounter(&t);
        return static_cast<uint64_t>(t.QuadPart);
    }

    // Conversions between QPC ticks and ns/us/s
    static inline uint64_t qpc_to_ns(uint64_t qpc) noexcept {
        // ns = (qpc / freq) * 1e9  →  qpc * (1e9 * invFreq)
        const long double ns = static_cast<long double>(qpc) * (1.0e9L * gInvFreq);
        return static_cast<uint64_t>(ns + 0.5L);
    }
    static inline uint64_t ns_to_qpc(uint64_t ns) noexcept {
        // qpc = ns * (freq / 1e9)
        const long double qpc = static_cast<long double>(ns) * (static_cast<long double>(gFreqLI.QuadPart) / 1.0e9L);
        return static_cast<uint64_t>(qpc + 0.5L);
    }

    // Sleep-agnostic, monotonic time in seconds from QueryUnbiasedInterruptTime(Precise) if available.
    static inline double unbiased_seconds() noexcept {
        using QUTPrecise = VOID (WINAPI*)(PULONGLONG);
        using QUT        = BOOL (WINAPI*)(PULONGLONG);

        static QUTPrecise pPrecise = load_kernel32<QUTPrecise>("QueryUnbiasedInterruptTimePrecise");
        static QUT        p        = load_kernel32<QUT>("QueryUnbiasedInterruptTime");

        ULONGLONG t100 = 0; // 100ns units
        if (pPrecise) { pPrecise(&t100); return static_cast<double>(t100) / 1e7; }
        if (p && p(&t100)) { return static_cast<double>(t100) / 1e7; }
        // Fallback: biased uptime in seconds (includes sleep); last resort for very old systems.
        return static_cast<double>(::GetTickCount64()) / 1000.0;
    }

    // CPU-friendly spin with backoff until target QPC is reached.
    static inline void spin_until_qpc(uint64_t qpc_target) noexcept {
        for (;;) {
            if (qpc_now() >= qpc_target) break;
        #if defined(_MSC_VER)
            _mm_pause();
        #endif
        }
    }

    // High-resolution waitable timer wrapper (Win10 1803+: HR timers).
    struct WaitableTimer {
        HANDLE h = nullptr;
        bool   highRes = false;

        WaitableTimer() {
            // Try high-resolution waitable timer first.
            h = ::CreateWaitableTimerExW(nullptr, nullptr,
                                         CREATE_WAITABLE_TIMER_MANUAL_RESET | CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                         TIMER_ALL_ACCESS);
            if (h) { highRes = true; return; }

            // Fallback to standard waitable timer.
            h = ::CreateWaitableTimerExW(nullptr, nullptr,
                                         CREATE_WAITABLE_TIMER_MANUAL_RESET,
                                         TIMER_ALL_ACCESS);
        }
        ~WaitableTimer() { if (h) ::CloseHandle(h); }

        // Set a relative wait in nanoseconds; uses SetWaitableTimerEx if available.
        void set_relative_ns(uint64_t ns) const {
            LARGE_INTEGER due{};
            // Negative => relative, in 100ns units; clamp to >= 100ns
            const uint64_t ticks100 = (ns >= 100) ? (ns / 100) : 1;
            due.QuadPart = -static_cast<LONGLONG>(ticks100);

            using SetEx = BOOL (WINAPI*)(HANDLE, const LARGE_INTEGER*, LONG, PTIMERAPCROUTINE, LPVOID, PREASON_CONTEXT, ULONG);
            static SetEx pSetEx = load_kernel32<SetEx>("SetWaitableTimerEx");

            if (pSetEx) {
                // Tolerable delay 0 => do not intentionally coalesce; we want precision.
                pSetEx(h, &due, /*period ms*/0, nullptr, nullptr, nullptr, /*TolerableDelay*/0);
            } else {
                ::SetWaitableTimer(h, &due, /*period ms*/0, nullptr, nullptr, FALSE);
            }
        }

        void wait() const {
            ::WaitForSingleObject(h, INFINITE);
        }
    };

    // Precise sleep: use waitable timer for coarse portion and spin for last ~200µs.
    static inline void precise_sleep_ns(uint64_t ns) noexcept {
        if (ns == 0) return;

        // Target absolute deadline in QPC ticks.
        const uint64_t start   = qpc_now();
        const uint64_t target  = start + ns_to_qpc(ns);

        // Below ~1.5-2 ms: a context switch is often worse than spinning/yielding once.
        constexpr uint64_t kCoarseThresholdNs = 2'000'000ull; // 2.0 ms
        constexpr uint64_t kSpinFinishNs      =   200'000ull; // 0.2 ms spin finish

        if (ns >= kCoarseThresholdNs) {
            WaitableTimer t;
            const uint64_t now = qpc_now();
            if (target > now) {
                const uint64_t remainNs = qpc_to_ns(target - now);
                const uint64_t coarseNs = (remainNs > kSpinFinishNs) ? (remainNs - kSpinFinishNs) : 0ull;
                if (coarseNs >= 100'000ull) { // avoid arming ultra-short waits
                    t.set_relative_ns(coarseNs);
                    t.wait();
                }
            }
        } else if (ns >= 100'000ull) {
            // Let the scheduler run something else once for medium short waits.
            std::this_thread::yield();
        }

        // Spin until the precise deadline.
        spin_until_qpc(target);
    }

    // Lightweight rolling stats used by FramePacer for overshoot compensation.
    struct RollingStats {
        static constexpr int N = 64;
        double samples[N] = {};
        int    idx = 0;
        int    count = 0;

        void push(double v) {
            samples[idx] = v;
            idx = (idx + 1) & (N - 1);
            if (count < N) ++count;
        }

        double mean() const {
            double s = 0.0;
            for (int i = 0; i < count; ++i) s += samples[i];
            return (count > 0) ? (s / count) : 0.0;
        }
    };

} // namespace cg_detail

// -------------------------------------------------------------------------------------------------
// Public API — unchanged signatures
// -------------------------------------------------------------------------------------------------

void HiResClock::init() {
    const uint32_t prev = gInitRefs.fetch_add(1, std::memory_order_acq_rel);
    if (prev != 0) return;

    ::QueryPerformanceFrequency(&gFreqLI);
    gInvFreq = 1.0L / static_cast<long double>(gFreqLI.QuadPart);
    gInitQpc = cg_detail::qpc_now();

    // Request 1ms scheduler granularity for this process (paired with shutdown).
    // On newer Windows this is effectively per-process, but always pair your calls.
    gPeriod1 = (timeBeginPeriod(1) == TIMERR_NOERROR);
}

uint64_t HiResClock::freq() {
    return static_cast<uint64_t>(gFreqLI.QuadPart);
}

uint64_t HiResClock::ticks() {
    return cg_detail::qpc_now();
}

double HiResClock::seconds() {
    // Monotonic seconds since unspecified QPC epoch
    const uint64_t t = HiResClock::ticks();
    return static_cast<double>(t) / static_cast<double>(HiResClock::freq());
}

void HiResClock::shutdown() {
    const uint32_t prev = gInitRefs.load(std::memory_order_acquire);
    if (prev == 0) return;

    if (gInitRefs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        if (gPeriod1) {
            timeEndPeriod(1);
            gPeriod1 = false;
        }
    }
}

// -------------------------------------------------------------------------------------------------
// Extras (helpers you can optionally expose in your header)
// -------------------------------------------------------------------------------------------------
namespace cg { namespace hires {

    // ----------------------------- Basic conversions & timestamps -------------------------------

    // Nanoseconds since QPC-based "init epoch" (call HiResClock::init() early in app).
    static inline uint64_t now_ns() {
        return cg_detail::qpc_to_ns(HiResClock::ticks() - gInitQpc);
    }

    // Unbiased seconds (does not include sleep/hibernate).
    static inline double unbiased_seconds() {
        return cg_detail::unbiased_seconds();
    }

    // Convert between QPC ticks and time units.
    static inline uint64_t qpc_to_ns(uint64_t qpc) { return cg_detail::qpc_to_ns(qpc); }
    static inline uint64_t ns_to_qpc(uint64_t ns)  { return cg_detail::ns_to_qpc(ns);  }

    // ----------------------------- Precise sleeping --------------------------------------------

    // Sleep for a precise duration in nanoseconds (coarse waitable timer + spin finish).
    static inline void sleep_for_ns(uint64_t ns) {
        cg_detail::precise_sleep_ns(ns);
    }

    // Sleep until an absolute QPC tick.
    static inline void sleep_until_ticks(uint64_t qpc_target) {
        const uint64_t now = HiResClock::ticks();
        if (qpc_target <= now) return;
        const uint64_t ns = cg_detail::qpc_to_ns(qpc_target - now);
        cg_detail::precise_sleep_ns(ns);
    }

    // Sleep until (now + dt_ns) using QPC origin.
    static inline void sleep_until_ns_from_now(uint64_t dt_ns) {
        const uint64_t target = HiResClock::ticks() + cg_detail::ns_to_qpc(dt_ns);
        sleep_until_ticks(target);
    }

    // ----------------------------- Frame pacing -------------------------------------------------

    // A small helper to pace frames to a target period (Hz or ns) with overshoot compensation.
    class FramePacer {
    public:
        explicit FramePacer(double targetHz = 60.0)
        {
            set_target_hz(targetHz);
        }

        void set_target_hz(double hz) {
            if (hz <= 0.0) hz = 60.0;
            target_ns_ = static_cast<uint64_t>((1.0e9) / hz + 0.5);
        }

        void set_target_ns(uint64_t ns) {
            target_ns_ = (ns == 0) ? 16'666'667ull : ns;
        }

        // Call at the start of each frame.
        uint64_t begin_frame() {
            last_start_qpc_ = HiResClock::ticks();
            return last_start_qpc_;
        }

        // Call before presenting the frame; sleeps/spins to align with the frame boundary.
        void finish_frame() {
            if (!last_start_qpc_) {
                begin_frame();
                return;
            }

            // Expected end time for this frame.
            const uint64_t target_qpc = last_start_qpc_ + cg_detail::ns_to_qpc(target_ns_);

            // Apply a small bias based on recent overshoot (ns), capped to reasonable bounds.
            double bias_ns = overshoot_.mean();
            if (bias_ns < -500'000.0) bias_ns = -500'000.0;
            if (bias_ns >  +500'000.0) bias_ns =  +500'000.0;

            const uint64_t biased_target_qpc = (bias_ns >= 0.0)
                ? (target_qpc + cg_detail::ns_to_qpc(static_cast<uint64_t>(bias_ns)))
                : (target_qpc - cg_detail::ns_to_qpc(static_cast<uint64_t>(-bias_ns)));

            // Sleep precisely until target.
            sleep_until_ticks(biased_target_qpc);

            // Measure overshoot/undershoot and update stats.
            const uint64_t end_qpc = HiResClock::ticks();
            const int64_t  err_qpc = static_cast<int64_t>(end_qpc) - static_cast<int64_t>(target_qpc);
            const double   err_ns  = static_cast<double>(cg_detail::qpc_to_ns(static_cast<uint64_t>(std::llabs(err_qpc))));
            const double   signed_err_ns = (err_qpc >= 0) ? err_ns : -err_ns;
            overshoot_.push(signed_err_ns);
        }

        uint64_t target_ns() const { return target_ns_; }

    private:
        uint64_t     target_ns_       = 16'666'667ull; // 60 Hz default
        uint64_t     last_start_qpc_  = 0;
        cg_detail::RollingStats overshoot_{};
    };

    // ----------------------------- Scoped profiling --------------------------------------------

    // RAII timer that prints to the debugger on destruction.
    class ScopedTimer {
    public:
        explicit ScopedTimer(const char* label) : label_(label), start_(HiResClock::ticks()) {}
        ~ScopedTimer() {
            const uint64_t end   = HiResClock::ticks();
            const uint64_t dtQpc = (end >= start_) ? (end - start_) : 0;
            const uint64_t us    = cg_detail::qpc_to_ns(dtQpc) / 1000ull;

            // Format a small message and send to debugger.
            std::ostringstream oss;
            oss << "[TIMER] " << label_ << " : " << us << " us\n";
            const std::string s = oss.str();
            ::OutputDebugStringA(s.c_str());
        }
        ScopedTimer(const ScopedTimer&) = delete;
        ScopedTimer& operator=(const ScopedTimer&) = delete;
    private:
        const char*  label_;
        uint64_t     start_;
    };

    // ----------------------------- Utility: coarse sleep fallback -------------------------------

    // Use OS Sleep for very long waits where precision is irrelevant (e.g., seconds).
    static inline void sleep_for_ms_coarse(uint32_t ms) {
        ::Sleep(ms);
    }

}} // namespace cg::hires

// -------------------------------------------------------------------------------------------------
// End of file
// -------------------------------------------------------------------------------------------------
