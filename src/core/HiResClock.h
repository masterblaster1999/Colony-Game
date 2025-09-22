#pragma once
// HiResClock.h — Windows-only high-resolution time & thread QoS toolkit for Colony-Game
//
// Stable public API (implemented in HiResClock.cpp):
//   - HiResClock::init() / shutdown()
//   - HiResClock::freq()  => QPC ticks/second
//   - HiResClock::ticks() => QPC tick 'now' (monotonic)
//   - HiResClock::seconds() => ticks()/freq()
//
// This header adds a *header-only* toolbox on top of the above API:
//   cg::timeutil   - Conversions, TimePoint/TimeSpan, chrono adapters, formatting
//   cg::timers     - Stopwatch, LapTimer, ScopedChronoLog (debug)
//   cg::metrics    - FrameStats<N>, FpsAverager, FrameBudget
//   cg::loop       - FixedStepLoop (spiral-of-death guarded fixed update)
//   cg::sync       - Busy wait helpers, RateLimiter
//   cg::threading  - SystemAwakeScope, PowerThrottlingScope, BackgroundModeScope,
//                    MMCSSScope (dynamic Avrt), ThreadAffinityScope, ThreadGroupAffinityScope
//
// Build notes:
//   * Link `winmm` for timeBeginPeriod/timeEndPeriod in your CMake (used by the .cpp).
//   * This header dynamically loads Avrt APIs (no avrt.lib dependency).
//   * All new facilities are Windows-only.
//
// References used for design decisions:
//   - QPC frequency is fixed after boot, consistent across processors. :contentReference[oaicite:5]{index=5}
//   - High‑res waitable timers & TolerableDelay (used by your .cpp). :contentReference[oaicite:6]{index=6}
//   - Unbiased interrupt time (sleep‑agnostic). :contentReference[oaicite:7]{index=7}
//   - timeBeginPeriod pairing, 2004+ per‑process behavior; see MS Learn. :contentReference[oaicite:8]{index=8}
//   - Thread QoS and throttling controls. :contentReference[oaicite:9]{index=9}
//   - MMCSS thread classes for games/audio. :contentReference[oaicite:10]{index=10}

#include <cstdint>
#include <cmath>
#include <array>
#include <chrono>
#include <limits>
#include <type_traits>
#include <thread>
#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <cstdio>

#if !defined(_WIN32)
#  error "HiResClock is Windows-only. Provide a non-Windows backend before including this header."
#endif

// ======= Configuration toggles ===============================================================
#ifndef CG_TIME_WIN_LEAN
#  define CG_TIME_WIN_LEAN 1
#endif

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#if CG_TIME_WIN_LEAN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#endif

#include <windows.h>
#include <processthreadsapi.h> // SetThreadInformation, THREAD_* (SDK 10+)
#include <winbase.h>           // SetThreadExecutionState, OutputDebugString

// Some SDKs (older kits) may lack a few defines; provide them for compatibility.
#ifndef THREAD_POWER_THROTTLING_CURRENT_VERSION
#  define THREAD_POWER_THROTTLING_CURRENT_VERSION 1
#endif
#ifndef THREAD_POWER_THROTTLING_EXECUTION_SPEED
#  define THREAD_POWER_THROTTLING_EXECUTION_SPEED 0x1
#endif

// ======= Stable facade implemented in HiResClock.cpp =========================================
class HiResClock final {
public:
    static void         init();
    static std::uint64_t freq();
    static std::uint64_t ticks();
    static double       seconds();
    static void         shutdown();

    HiResClock() = delete;
    ~HiResClock() = delete;

    class Scope {
    public:
        Scope()  { HiResClock::init(); }
        ~Scope() { HiResClock::shutdown(); }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    };
};

// =================================================================================================
// Header-only utilities (no .cpp changes)
// =================================================================================================

namespace cg {

// -------------------------------------------------------------------------------------------------
// timeutil — conversions, adapters & light wrappers over QPC
// -------------------------------------------------------------------------------------------------
namespace timeutil {

    // -------------- basic conversions (QPC ticks ⇄ time) --------------------
    inline long double inv_freq_ld() {
        return 1.0L / static_cast<long double>(HiResClock::freq());
    }

    inline double qpc_to_seconds(std::uint64_t qpc) {
        return static_cast<double>(qpc) / static_cast<double>(HiResClock::freq());
    }
    inline std::uint64_t qpc_to_ns(std::uint64_t qpc) {
        const long double ns = static_cast<long double>(qpc) * (1.0e9L * inv_freq_ld());
        return static_cast<std::uint64_t>(ns + 0.5L);
    }
    inline std::uint64_t qpc_to_us(std::uint64_t qpc) {
        const long double us = static_cast<long double>(qpc) * (1.0e6L * inv_freq_ld());
        return static_cast<std::uint64_t>(us + 0.5L);
    }
    inline std::uint64_t qpc_to_ms(std::uint64_t qpc) {
        const long double ms = static_cast<long double>(qpc) * (1.0e3L * inv_freq_ld());
        return static_cast<std::uint64_t>(ms + 0.5L);
    }

    inline std::uint64_t ns_to_qpc(std::uint64_t ns) {
        const long double qpc = static_cast<long double>(ns)
                              * (static_cast<long double>(HiResClock::freq()) / 1.0e9L);
        return static_cast<std::uint64_t>(qpc + 0.5L);
    }
    inline std::uint64_t us_to_qpc(std::uint64_t us) {
        const long double qpc = static_cast<long double>(us)
                              * (static_cast<long double>(HiResClock::freq()) / 1.0e6L);
        return static_cast<std::uint64_t>(qpc + 0.5L);
    }
    inline std::uint64_t ms_to_qpc(std::uint64_t ms) {
        const long double qpc = static_cast<long double>(ms)
                              * (static_cast<long double>(HiResClock::freq()) / 1.0e3L);
        return static_cast<std::uint64_t>(qpc + 0.5L);
    }

    // -------------- 100ns (FILETIME-style) adapters -------------------------
    inline std::uint64_t qpc_to_100ns(std::uint64_t qpc) {
        // 100ns = (qpc / freq) * 1e7
        const long double t = static_cast<long double>(qpc) * (1.0e7L * inv_freq_ld());
        return static_cast<std::uint64_t>(t + 0.5L);
    }
    inline std::uint64_t ns_to_100ns(std::uint64_t ns) { return (ns + 50ull) / 100ull; }
    inline std::uint64_t _100ns_to_ns(std::uint64_t h) { return h * 100ull; }

    // -------------- chrono adapters -----------------------------------------
    template <class Rep, class Period>
    inline std::uint64_t chrono_to_qpc(std::chrono::duration<Rep, Period> d) {
        using ns_t = std::chrono::nanoseconds;
        return ns_to_qpc(static_cast<std::uint64_t>(std::chrono::duration_cast<ns_t>(d).count()));
    }
    template <class Rep, class Period>
    inline std::chrono::duration<Rep, Period> qpc_to_chrono(std::uint64_t qpc) {
        using ns_t = std::chrono::nanoseconds;
        return std::chrono::duration_cast<std::chrono::duration<Rep, Period>>(ns_t(qpc_to_ns(qpc)));
    }

    // -------------- formatting ----------------------------------------------
    inline std::string format_duration_ns(std::uint64_t ns) {
        char buf[64];
        if (ns < 1'000ull)             { std::snprintf(buf, sizeof(buf), "%lluns", (unsigned long long)ns); }
        else if (ns < 1'000'000ull)    { std::snprintf(buf, sizeof(buf), "%.3fus", ns / 1000.0); }
        else if (ns < 1'000'000'000ull){ std::snprintf(buf, sizeof(buf), "%.3fms", ns / 1.0e6); }
        else                           { std::snprintf(buf, sizeof(buf), "%.3fs",  ns / 1.0e9); }
        return std::string(buf);
    }

    // -------------- TimePoint / TimeSpan ------------------------------------
    struct TimePoint { std::uint64_t qpc = 0; static TimePoint now(){ return { HiResClock::ticks() }; } };
    struct TimeSpan  {
        std::uint64_t qpc = 0;
        static TimeSpan from_qpc(std::uint64_t t){ return { t }; }
        static TimeSpan from_ns (std::uint64_t ns){ return { ns_to_qpc(ns) }; }
        static TimeSpan from_ms (std::uint64_t ms){ return { ms_to_qpc(ms) }; }
        static TimeSpan from_us (std::uint64_t us){ return { us_to_qpc(us) }; }
        static TimeSpan from_sec(double s){ return { static_cast<std::uint64_t>(s * HiResClock::freq() + 0.5) }; }

        double        seconds() const { return qpc_to_seconds(qpc); }
        std::uint64_t ns() const      { return qpc_to_ns(qpc); }
        std::uint64_t us() const      { return qpc_to_us(qpc); }
        std::uint64_t ms() const      { return qpc_to_ms(qpc); }

        TimeSpan operator+(TimeSpan r) const { return { qpc + r.qpc }; }
        TimeSpan operator-(TimeSpan r) const { return { qpc - r.qpc }; }
        TimeSpan& operator+=(TimeSpan r)     { qpc += r.qpc; return *this; }
        TimeSpan& operator-=(TimeSpan r)     { qpc -= r.qpc; return *this; }
        TimeSpan  scaled(double k) const     { return { static_cast<std::uint64_t>(qpc * k + 0.5) }; }
    };
    inline TimeSpan  operator-(TimePoint a, TimePoint b){ return { a.qpc - b.qpc }; }
    inline TimePoint operator+(TimePoint a, TimeSpan d){ return { a.qpc + d.qpc }; }
    inline TimePoint operator-(TimePoint a, TimeSpan d){ return { a.qpc - d.qpc }; }

} // namespace timeutil

// -------------------------------------------------------------------------------------------------
// timers — lightweight profiling & timing helpers
// -------------------------------------------------------------------------------------------------
namespace timers {

    class Stopwatch {
    public:
        Stopwatch() : start_(HiResClock::ticks()), running_(true), elapsed_(0) {}
        void reset()   { start_ = HiResClock::ticks(); elapsed_ = 0; running_ = true; }
        void restart() { reset(); }
        void start()   { if (!running_) { start_ = HiResClock::ticks(); running_ = true; } }
        void stop()    { if (running_) { elapsed_ += (HiResClock::ticks() - start_); running_ = false; } }
        bool running() const { return running_; }

        std::uint64_t elapsed_qpc() const {
            return running_ ? (elapsed_ + (HiResClock::ticks() - start_)) : elapsed_;
        }
        std::uint64_t elapsed_ns() const { return timeutil::qpc_to_ns(elapsed_qpc()); }
        std::uint64_t elapsed_us() const { return timeutil::qpc_to_us(elapsed_qpc()); }
        std::uint64_t elapsed_ms() const { return timeutil::qpc_to_ms(elapsed_qpc()); }
        double        elapsed_sec()const { return timeutil::qpc_to_seconds(elapsed_qpc()); }

    private:
        std::uint64_t start_;
        bool          running_;
        std::uint64_t elapsed_;
    };

    class LapTimer {
    public:
        LapTimer() : last_(HiResClock::ticks()) {}
        timeutil::TimeSpan lap() {
            const auto now = HiResClock::ticks();
            const auto dt  = now - last_;
            last_ = now;
            return timeutil::TimeSpan::from_qpc(dt);
        }
    private:
        std::uint64_t last_;
    };

    // RAII debug logger (uses OutputDebugStringA)
    class ScopedChronoLog {
    public:
        explicit ScopedChronoLog(const char* label) : label_(label), t0_(HiResClock::ticks()) {}
        ~ScopedChronoLog() {
            const auto us = timeutil::qpc_to_us(HiResClock::ticks() - t0_);
            char buf[160];
            std::snprintf(buf, sizeof(buf), "[TIMER] %s: %llu us\n", label_, (unsigned long long)us);
            ::OutputDebugStringA(buf);
        }
        ScopedChronoLog(const ScopedChronoLog&) = delete;
        ScopedChronoLog& operator=(const ScopedChronoLog&) = delete;
    private:
        const char*   label_;
        std::uint64_t t0_;
    };

} // namespace timers

// -------------------------------------------------------------------------------------------------
// metrics — frame statistics & budgeting
// -------------------------------------------------------------------------------------------------
namespace metrics {

    template <int N = 64>
    class FrameStats {
        static_assert((N & (N-1)) == 0, "N must be power of two");
    public:
        void push(double v) { buf_[idx_] = v; idx_ = (idx_ + 1) & (N-1); if (count_ < N) ++count_; }
        int  count() const  { return count_; }
        double mean() const { if (!count_) return 0.0; double s=0; for(int i=0;i<count_;++i) s+=buf_[i]; return s/count_; }
        double min()  const { if (!count_) return 0.0; double m=buf_[0]; for(int i=1;i<count_;++i) m=std::min(m,buf_[i]); return m; }
        double max()  const { if (!count_) return 0.0; double m=buf_[0]; for(int i=1;i<count_;++i) m=std::max(m,buf_[i]); return m; }
        double stddev() const { if (count_<=1) return 0.0; const double m=mean(); double a=0; for(int i=0;i<count_;++i){const double d=buf_[i]-m;a+=d*d;} return std::sqrt(a/(count_-1)); }
    private:
        std::array<double,N> buf_{};
        int idx_ = 0, count_ = 0;
    };

    class FpsAverager {
    public:
        explicit FpsAverager(double alpha=0.25) : alpha_(alpha) {}
        void add_frame_ns(std::uint64_t ns) {
            if (!ns) return;
            const double fps = 1.0e9 / static_cast<double>(ns);
            if (!has_) { ema_ = fps; has_ = true; } else { ema_ = alpha_*fps + (1.0-alpha_)*ema_; }
        }
        bool valid() const { return has_; }
        double fps() const { return has_ ? ema_ : 0.0; }
    private:
        double alpha_, ema_ = 0.0; bool has_ = false;
    };

    struct FrameBudget {
        std::uint64_t frame_start_qpc = 0;
        std::uint64_t target_ns       = 16'666'667ull; // ~60 Hz
        static FrameBudget start(double target_hz=60.0) {
            FrameBudget fb{};
            fb.frame_start_qpc = HiResClock::ticks();
            fb.target_ns = static_cast<std::uint64_t>((1.0e9 / std::max(1e-9, target_hz)) + 0.5);
            return fb;
        }
        std::uint64_t remaining_qpc() const {
            const auto now = HiResClock::ticks();
            const auto end = frame_start_qpc + timeutil::ns_to_qpc(target_ns);
            return (now >= end) ? 0ull : (end - now);
        }
        std::uint64_t remaining_ns() const { return timeutil::qpc_to_ns(remaining_qpc()); }
        bool past_deadline() const { return remaining_qpc() == 0; }
    };

} // namespace metrics

// -------------------------------------------------------------------------------------------------
// loop — fixed timestep helper (spiral-of-death guard)
// -------------------------------------------------------------------------------------------------
namespace loop {

    // Typical usage:
    //   FixedStepLoop loop{ std::chrono::microseconds{16667} }; // 60 Hz
    //   loop.tick([&](double dt) { simulate(dt); }, []{ render(); });
    class FixedStepLoop {
    public:
        using UpdateFn = void(*)(double dtSeconds);
        using RenderFn = void(*)(void);

        explicit FixedStepLoop(std::chrono::nanoseconds step = std::chrono::microseconds(16667),
                               int maxCatchUpSteps = 5)
        : step_ns_(static_cast<std::uint64_t>(step.count())),
          max_steps_(std::max(1, maxCatchUpSteps)),
          acc_qpc_(0), last_qpc_(HiResClock::ticks()) {}

        template <class UpdateCallable, class RenderCallable>
        void tick(UpdateCallable&& update, RenderCallable&& render) {
            const auto now = HiResClock::ticks();
            acc_qpc_ += (now - last_qpc_);
            last_qpc_ = now;

            const auto step_qpc = timeutil::ns_to_qpc(step_ns_);
            int steps = 0;
            while (acc_qpc_ >= step_qpc && steps < max_steps_) {
                std::forward<UpdateCallable>(update)(static_cast<double>(step_ns_) / 1.0e9);
                acc_qpc_ -= step_qpc;
                ++steps;
            }
            std::forward<RenderCallable>(render)();
            // If we fell behind, drop extra accumulated time to avoid spiraling.
            if (steps == max_steps_) acc_qpc_ = 0;
        }

        void set_step(std::chrono::nanoseconds s) { step_ns_ = static_cast<std::uint64_t>(s.count()); }
        void set_max_catchup_steps(int n)         { max_steps_ = std::max(1, n); }

    private:
        std::uint64_t step_ns_;
        int           max_steps_;
        std::uint64_t acc_qpc_;
        std::uint64_t last_qpc_;
    };

} // namespace loop

// -------------------------------------------------------------------------------------------------
// sync — busy wait & rate-limiting
// -------------------------------------------------------------------------------------------------
namespace sync {

    inline void cpu_relax() {
    #if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
        _mm_pause();
    #else
        std::this_thread::yield();
    #endif
    }

    inline void busy_wait_until(std::uint64_t target_qpc) {
        while (HiResClock::ticks() < target_qpc) cpu_relax();
    }

    inline void busy_wait_ns(std::uint64_t ns) {
        if (!ns) return;
        const auto target = HiResClock::ticks() + timeutil::ns_to_qpc(ns);
        busy_wait_until(target);
    }

    class RateLimiter {
    public:
        explicit RateLimiter(std::uint64_t min_interval_ns = 16'666'667ull)
        : min_qpc_(timeutil::ns_to_qpc(min_interval_ns)), last_(0) {}
        void set_min_interval_ns(std::uint64_t ns) { min_qpc_ = timeutil::ns_to_qpc(ns); }
        bool allow() {
            const auto now = HiResClock::ticks();
            if (now - last_ >= min_qpc_) { last_ = now; return true; }
            return false;
        }
        std::uint64_t wait_time_ns() const {
            const auto now = HiResClock::ticks();
            if (now - last_ >= min_qpc_) return 0ull;
            return timeutil::qpc_to_ns(min_qpc_ - (now - last_));
        }
    private:
        std::uint64_t min_qpc_, last_;
    };

} // namespace sync

// -------------------------------------------------------------------------------------------------
// threading — Windows QoS/affinity helpers (all RAII, dynamic-load where needed)
// -------------------------------------------------------------------------------------------------
namespace threading {

    // Prevent system sleep while scope is alive (optionally keep display on).
    // Uses SetThreadExecutionState; pair ES_CONTINUOUS with ES_SYSTEM_REQUIRED/ES_DISPLAY_REQUIRED. :contentReference[oaicite:11]{index=11}
    class SystemAwakeScope {
    public:
        explicit SystemAwakeScope(bool keepDisplayOn=false) : ok_(false) {
            EXECUTION_STATE flags = ES_CONTINUOUS | ES_SYSTEM_REQUIRED;
            if (keepDisplayOn) flags |= ES_DISPLAY_REQUIRED;
            ok_ = (::SetThreadExecutionState(flags) != 0);
        }
        ~SystemAwakeScope() {
            if (ok_) ::SetThreadExecutionState(ES_CONTINUOUS); // clear requirements
        }
        bool ok() const { return ok_; }
    private:
        bool ok_;
    };

    // Disable Windows power throttling for the current thread (Win10 1709+).
    // Calls SetThreadInformation(ThreadPowerThrottling, THREAD_POWER_THROTTLING_STATE). :contentReference[oaicite:12]{index=12}
    class PowerThrottlingScope {
    public:
        PowerThrottlingScope() : ok_(false) {
            using State = THREAD_POWER_THROTTLING_STATE;
            State st{};
            st.Version     = THREAD_POWER_THROTTLING_CURRENT_VERSION;
            st.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
            st.StateMask   = 0; // 0 => disable throttling for EXECUTION_SPEED
            ok_ = ::SetThreadInformation(::GetCurrentThread(), ThreadPowerThrottling, &st, sizeof(st));
        }
        ~PowerThrottlingScope() {
            using State = THREAD_POWER_THROTTLING_STATE;
            State st{};
            st.Version     = THREAD_POWER_THROTTLING_CURRENT_VERSION;
            st.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
            st.StateMask   = THREAD_POWER_THROTTLING_EXECUTION_SPEED; // allow automatic again
            ::SetThreadInformation(::GetCurrentThread(), ThreadPowerThrottling, &st, sizeof(st));
        }
        bool ok() const { return ok_ != 0; }
    private:
        BOOL ok_;
    };

    // Enter/leave background mode (affects I/O and memory priorities and scheduler hints). :contentReference[oaicite:13]{index=13}
    class BackgroundModeScope {
    public:
        BackgroundModeScope() : ok_(::SetThreadPriority(::GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN)) {}
        ~BackgroundModeScope() { ::SetThreadPriority(::GetCurrentThread(), THREAD_MODE_BACKGROUND_END); }
        bool ok() const { return ok_ != 0; }
    private:
        BOOL ok_;
    };

    // ---- MMCSS (Avrt.dll) — dynamic load; gives regular, short, high-priority bursts for A/V/game threads. ----
    // Docs: AvSetMmThreadCharacteristics*, AvSetMmThreadPriority. :contentReference[oaicite:14]{index=14}
    class MMCSSScope {
    public:
        enum class Task {
            Games,           // "Games"
            Audio,           // "Audio"
            Playback,        // "Playback"
            ProAudio,        // "Pro Audio"
            Capture,         // "Capture"
            Distribution     // "Distribution"
        };
        enum class Pri {
            VeryLow, Low, Normal, High, VeryHigh, Critical
        };

        MMCSSScope(Task t = Task::Games, Pri p = Pri::High) : hAvrt_(nullptr), pRevert_(nullptr) {
            HMODULE mod = ::LoadLibraryW(L"avrt.dll");
            if (!mod) return;
            auto pSetChar = reinterpret_cast<HANDLE (WINAPI*)(LPCWSTR, LPDWORD)>(::GetProcAddress(mod, "AvSetMmThreadCharacteristicsW"));
            auto pSetPri  = reinterpret_cast<BOOL   (WINAPI*)(HANDLE, int)    >(::GetProcAddress(mod, "AvSetMmThreadPriority"));
            pRevert_      = reinterpret_cast<BOOL   (WINAPI*)(HANDLE)         >(::GetProcAddress(mod, "AvRevertMmThreadCharacteristics"));
            if (!pSetChar || !pSetPri || !pRevert_) { ::FreeLibrary(mod); return; }

            DWORD taskIdx = 0;
            const wchar_t* name = L"Games";
            switch (t) {
                case Task::Games:        name = L"Games"; break;
                case Task::Audio:        name = L"Audio"; break;
                case Task::Playback:     name = L"Playback"; break;
                case Task::ProAudio:     name = L"Pro Audio"; break;
                case Task::Capture:      name = L"Capture"; break;
                case Task::Distribution: name = L"Distribution"; break;
            }
            HANDLE h = pSetChar(name, &taskIdx);
            if (!h) { ::FreeLibrary(mod); return; }

            const int pri = [p]{
                switch (p) {
                    case Pri::VeryLow:   return -2;
                    case Pri::Low:       return -1;
                    case Pri::Normal:    return  0;
                    case Pri::High:      return  1;
                    case Pri::VeryHigh:  return  2;
                    case Pri::Critical:  return  3;
                }
                return 0;
            }();
            if (!pSetPri(h, pri)) { pRevert_(h); ::FreeLibrary(mod); return; }

            hAvrt_ = h;
            mod_   = mod;
        }

        ~MMCSSScope() {
            if (hAvrt_ && pRevert_) pRevert_(hAvrt_);
            if (mod_) ::FreeLibrary(mod_);
        }

        bool ok() const { return hAvrt_ != nullptr; }

    private:
        HMODULE mod_ = nullptr;
        HANDLE  hAvrt_;
        BOOL  (WINAPI* pRevert_)(HANDLE);
    };

    // ---- Affinity helpers ----
    // Pin the current thread to a set of CPUs (bitmask must be subset of process mask). :contentReference[oaicite:15]{index=15}
    class ThreadAffinityScope {
    public:
        explicit ThreadAffinityScope(DWORD_PTR mask) : prev_(0) {
            HANDLE th = ::GetCurrentThread();
            prev_ = ::SetThreadAffinityMask(th, mask);
        }
        ~ThreadAffinityScope() {
            if (prev_) ::SetThreadAffinityMask(::GetCurrentThread(), prev_);
        }
        DWORD_PTR previous() const { return prev_; }
    private:
        DWORD_PTR prev_;
    };

    // Restrict to a specific processor group (for >64 logical processors). :contentReference[oaicite:16]{index=16}
    class ThreadGroupAffinityScope {
    public:
        explicit ThreadGroupAffinityScope(WORD group, KAFFINITY mask)
        : has_(false) {
            // Save previous
            ::GetThreadGroupAffinity(::GetCurrentThread(), &old_);
            GROUP_AFFINITY ga{};
            ga.Group = group;
            ga.Mask  = mask;
            has_ = ::SetThreadGroupAffinity(::GetCurrentThread(), &ga, &old_);
        }
        ~ThreadGroupAffinityScope() {
            if (has_) ::SetThreadGroupAffinity(::GetCurrentThread(), &old_, nullptr);
        }
        bool ok() const { return has_ != 0; }
    private:
        GROUP_AFFINITY old_{};
        BOOL           has_;
    };

} // namespace threading

} // namespace cg
