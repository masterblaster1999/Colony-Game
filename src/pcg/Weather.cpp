#include "Weather.hpp"
#include <algorithm>

namespace pcg {

WeatherSystem::WeatherSystem(uint64_t seed) : rng(Rng::from_seed(seed)) {
    set_default_temperate();
}

void WeatherSystem::set_default_temperate() {
    // Simple, hand-tuned; rows sum to 1
    P = {{
        {{0.70f,0.20f,0.08f,0.01f,0.01f,0.0f}}, // Clear ->
        {{0.40f,0.40f,0.18f,0.02f,0.0f, 0.0f}}, // Overcast ->
        {{0.20f,0.50f,0.25f,0.05f,0.0f, 0.0f}}, // Rain ->
        {{0.30f,0.40f,0.20f,0.10f,0.0f, 0.0f}}, // Storm ->
        {{0.60f,0.30f,0.05f,0.0f, 0.05f,0.0f}}, // Heatwave ->
        {{0.20f,0.30f,0.0f, 0.0f, 0.0f, 0.50f}}, // Snow ->
    }};
}

void WeatherSystem::step() {
    int s = static_cast<int>(state);
    float r = rng.rangef(0.0f, 1.0f);
    float acc=0;
    for (int i=0;i<6;++i) { acc += P[s][i]; if (r <= acc){ state = static_cast<Weather>(i); break; } }
}

} // namespace pcg
