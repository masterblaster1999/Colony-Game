#pragma once
#include <cmath>

namespace colony {

struct Vec2 {
    double x = 0.0, y = 0.0;
    Vec2() = default;
    Vec2(double X, double Y) : x(X), y(Y) {}
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, double s) { return {a.x * s, a.y * s}; }

inline Vec2 lerp(Vec2 a, Vec2 b, double t) {
    return a + (b - a) * t;
}

} // namespace colony
