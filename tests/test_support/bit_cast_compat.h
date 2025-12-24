#pragma once
//
// Portable bit_cast shim for tests.
// Prefer std::bit_cast (C++20, <bit>) when available; otherwise use a safe memcpy fallback.
// Exposes colony::bit_cast so test code can call it uniformly.
//
// Notes:
// - We intentionally avoid injecting into namespace std.
// - The memcpy fallback is not constexpr pre-C++20 (because std::memcpy isn't constexpr there).
//
#include <type_traits>
#include <cstring>

#if __has_include(<bit>)
  #include <bit>
#endif

namespace colony {

#if defined(__cpp_lib_bit_cast) && (__cpp_lib_bit_cast >= 201806L)

using std::bit_cast;

#else

template <class To, class From,
          std::enable_if_t<
              (sizeof(To) == sizeof(From)) &&
              std::is_trivially_copyable_v<To> &&
              std::is_trivially_copyable_v<From>,
              int> = 0>
inline To bit_cast(const From& src) noexcept
{
    To dst{};
    std::memcpy(&dst, &src, sizeof(To));
    return dst;
}

#endif

} // namespace colony
