#pragma once
#include <array>
#include "SeededRng.hpp"

namespace pcg {

enum class Weather { Clear, Overcast, Rain, Storm, Heatwave, Snow };

struct WeatherSystem {
    // row-major transition probabilities P[next | current]
    std::array<std::array<float,6>,6> P{};
    Weather state = Weather::Clear;
    Rng rng;

    WeatherSystem(uint64_t seed);
    void set_default_temperate();
    void step(); // advance 1 tick/day
};

} // namespace pcg
