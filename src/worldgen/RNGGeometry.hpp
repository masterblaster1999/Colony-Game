// src/worldgen/RNGGeometry.hpp
#pragma once
#include <array>
#include <cmath>
#include "RNGCore.hpp"

namespace cg::worldgen {

// Uniform point in unit disk (radius <= 1)
[[nodiscard]] inline std::pair<double,double> sample_in_unit_disk(RNG256& rng) noexcept {
    const double r = std::sqrt(rng.next_double01());
    const double t = CG_TWO_PI * rng.next_double01();
    return { r*std::cos(t), r*std::sin(t) };
}

// ON unit sphere (surface) — Marsaglia (1972)
[[nodiscard]] inline std::array<double,3> sample_on_unit_sphere(RNG256& rng) noexcept {
    double u,v,s;
    do {
        u = 2.0*rng.next_double01()-1.0;
        v = 2.0*rng.next_double01()-1.0;
        s = u*u+v*v;
    } while (s>=1.0 || s==0.0);
    const double factor = 2.0*std::sqrt(1.0 - s);
    return { u*factor, v*factor, 1.0 - 2.0*s };
}

// IN unit sphere (volume)
[[nodiscard]] inline std::array<double,3> sample_in_unit_sphere(RNG256& rng) noexcept {
    const auto d = sample_on_unit_sphere(rng);
    const double r = std::cbrt(rng.next_double01());
    return { d[0]*r, d[1]*r, d[2]*r };
}

// Shoemake (1992): uniform random unit quaternion (x,y,z,w)
[[nodiscard]] inline std::array<double,4> random_unit_quaternion(RNG256& rng) noexcept {
    const double u1 = rng.next_double01();
    const double u2 = rng.next_double01();
    const double u3 = rng.next_double01();
    const double r1 = std::sqrt(1.0 - u1);
    const double r2 = std::sqrt(u1);
    const double t1 = CG_TWO_PI * u2;
    const double t2 = CG_TWO_PI * u3;
    const double x = r1 * std::sin(t1);
    const double y = r1 * std::cos(t1);
    const double z = r2 * std::sin(t2);
    const double w = r2 * std::cos(t2);
    return { x,y,z,w };
}

} // namespace cg::worldgen
