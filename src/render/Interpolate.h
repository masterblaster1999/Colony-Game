#pragma once
#include <cmath>

namespace cg {
template<typename T> inline T lerp(const T& a, const T& b, float t) { return static_cast<T>(a + (b - a) * t); }

// Example POD type
struct Float2 { float x, y; };
inline Float2 lerp(const Float2& a, const Float2& b, float t) {
  return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
}
} // namespace cg
