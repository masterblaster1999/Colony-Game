// pathfinding/JpsCore.hpp
#pragma once

// Keep this header a lightweight shim that exposes only declarations
// relied upon by Jps.cpp, and *no* private implementation details.
// Public API (IGrid, Cell, JpsOptions, jps_find_path) lives in Jps.hpp.
#include "Jps.hpp"

#include <utility>      // std::pair
#include <vector>       // std::vector
#include <type_traits>  // static_assert checks (no runtime cost)

// =======================
// Version & feature flags
// =======================
#ifndef COLONY_JPS_CORE_VERSION
#  define COLONY_JPS_CORE_VERSION 0x010000  // 1.0.0
#endif

// Default toggles: lean by default, richer in Debug
#ifndef COLONY_JPS_ENABLE_PROFILING
#  ifndef NDEBUG
#    define COLONY_JPS_ENABLE_PROFILING 1
#  else
#    define COLONY_JPS_ENABLE_PROFILING 0
#  endif
#endif

#ifndef COLONY_JPS_HEAVY_ASSERTS
#  ifndef NDEBUG
#    define COLONY_JPS_HEAVY_ASSERTS 1
#  else
#    define COLONY_JPS_HEAVY_ASSERTS 0
#  endif
#endif

// ==========================
// Small cross-TU helper defs
// ==========================

// Force-inline & branch hints (MSVC-friendly)
#if defined(_MSC_VER)
#  define JPS_FORCE_INLINE __forceinline
#  define JPS_ASSUME(expr) __assume(expr)
#else
#  define JPS_FORCE_INLINE inline
#  define JPS_ASSUME(expr) ((void)0)
#endif

// [[likely]]/[[unlikely]] are C++20, but keep macros harmless on older modes.
#if defined(__cpp_attributes) && __cplusplus >= 202002L
#  define JPS_LIKELY(x)   (x)
#  define JPS_UNLIKELY(x) (x)
#else
#  define JPS_LIKELY(x)   (x)
#  define JPS_UNLIKELY(x) (x)
#endif

// Heavy asserts (compile-time controlled, no cost when disabled)
#if COLONY_JPS_HEAVY_ASSERTS
#  include <cassert>
#  define JPS_ASSERT(x) assert(x)
#else
#  define JPS_ASSERT(x) ((void)0)
#endif

// Lightweight, opt-in scoped timer that writes to the Windows debugger.
// Enabled when COLONY_JPS_ENABLE_PROFILING != 0 (Debug by default).
#if COLONY_JPS_ENABLE_PROFILING
#  include <chrono>
#  include <cinttypes>
#  if defined(_WIN32)
#    include <windows.h>
#  endif
namespace colony::path::detail {
struct ScopedTimer {
    const char* name;
    std::chrono::high_resolution_clock::time_point t0;
    explicit ScopedTimer(const char* n) noexcept
        : name(n), t0(std::chrono::high_resolution_clock::now()) {}
    ~ScopedTimer() noexcept {
        using namespace std::chrono;
        const auto us = duration_cast<microseconds>(high_resolution_clock::now() - t0).count();
    #if defined(_WIN32)
        char buf[160];
        // NOLINTNEXTLINE(clang-diagnostic-format-security)
        sprintf_s(buf, "[JPS] %s: %" PRId64 " us\n", name, static_cast<long long>(us));
        OutputDebugStringA(buf);  // Visible in VS Debug Output window
    #endif
    }
};
} // namespace colony::path::detail

#  define JPS_CONCAT_INNER(a,b) a##b
#  define JPS_CONCAT(a,b)       JPS_CONCAT_INNER(a,b)
#  define JPS_SCOPED_TIMER(label_literal) \
        ::colony::path::detail::ScopedTimer JPS_CONCAT(_jps_scoped_, __LINE__){label_literal}
#else
#  define JPS_SCOPED_TIMER(label_literal) ((void)0)
#endif

// ===============================
// Public types are declared in API
// ===============================
// IGrid, JpsOptions, Cell come from Jps.hpp.
// We keep only helper *declarations* here. Definitions live in Jps.cpp.

namespace colony::path {
namespace detail {

// ---- Helper API (definitions live in Jps.cpp; no private structs here) ----
int   idx(int x, int y, int W);
bool  in_bounds(const IGrid& g, int x, int y);
bool  passable (const IGrid& g, int x, int y);
bool  can_step (const IGrid& g, int x, int y, int dx, int dy, const JpsOptions& o);

float heuristic (int x0, int y0, int x1, int y1, const JpsOptions& o);
float dist_cost (int x0, int y0, int x1, int y1, const JpsOptions& o);
float tiebreak  (int x, int y, int sx, int sy, int gx, int gy);

bool  has_forced_neighbors_straight(const IGrid& g, int x, int y, int dx, int dy);
bool  has_forced_neighbors_diag    (const IGrid& g, int x, int y, int dx, int dy);

void  pruned_dirs(const IGrid& g, int x, int y, int px, int py,
                  const JpsOptions& o, std::vector<std::pair<int,int>>& out);

bool  jump(const IGrid& g, int x, int y, int dx, int dy,
           int gx, int gy, const JpsOptions& o, int& outx, int& outy);

bool  los_supercover(const IGrid& g, int x0, int y0, int x1, int y1, const JpsOptions& o);

// ================================
// Compile-time interface safeguards
// ================================
// These ensure public API drift is caught at compile time without pulling in heavy headers.

namespace _iface_check {

template <class T, class = void> struct has_width    : std::false_type {};
template <class T> struct has_width<T,    std::void_t<decltype(std::declval<const T&>().width())>>  : std::true_type {};

template <class T, class = void> struct has_height   : std::false_type {};
template <class T> struct has_height<T,   std::void_t<decltype(std::declval<const T&>().height())>> : std::true_type {};

template <class T, class = void> struct has_walkable : std::false_type {};
template <class T>
struct has_walkable<T, std::void_t<decltype(std::declval<const T&>().walkable(0,0))>> : std::true_type {};

} // namespace _iface_check

static_assert(_iface_check::has_width<IGrid>::value,    "IGrid must provide int width() const");
static_assert(_iface_check::has_height<IGrid>::value,   "IGrid must provide int height() const");
static_assert(_iface_check::has_walkable<IGrid>::value, "IGrid must provide bool walkable(int,int) const");

static_assert(std::is_copy_constructible_v<Cell> && std::is_copy_assignable_v<Cell>,
              "Cell should be a simple value type (copyable)");
static_assert(std::is_copy_constructible_v<JpsOptions>,
              "JpsOptions should be copyable (passed by const ref)");

} // namespace detail
} // namespace colony::path
