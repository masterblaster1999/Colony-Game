// src/procgen/WeatherPatternGen.hpp
#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
#include <random>
#include <string>
#include <algorithm>

namespace colony::procgen {

enum class Condition : uint8_t { Sunny, Clouds, Rain, Storm, Snow };

struct DayWeather {
    int   dayIndex = 0;   // 0..N-1
    float tempC    = 0.f; // daily mean
    float precipMM = 0.f; // precipitation
    float windKph  = 0.f;
    Condition cond = Condition::Sunny;
};

struct WeatherParams {
    int   days           = 60;
    float latitude01     = 0.35f; // 0=pole, 1=equator
    float baseTempC      = 10.0f;
    float seasonalRangeC = 18.0f;
    float noiseScale     = 0.08f;
    uint64_t seed        = 12345;
};

static inline uint32_t hash1u(uint32_t x){ x ^= x>>16; x*=0x7feb352d; x^=x>>15; x*=0x846ca68b; x^=x>>16; return x; }
static inline float value1D(float t, uint32_t seed){
    int i = (int)std::floor(t);
    float ft = t - i;
    auto v = [&](int I){ return (hash1u((uint32_t)(I + (int)seed)) & 0xffffff)*(1.f/16777215.f); };
    auto fade = [](float q){ return q*q*q*(q*(q*6-15)+10); };
    float a=v(i), b=v(i+1); return (1.f-fade(ft))*a + fade(ft)*b; // [0,1]
}
static inline float fbm1D(float t, int oct, float lac, float gain, uint32_t seed){
    float amp=0.5f, freq=1.f, sum=0.f, norm=0.f;
    for(int i=0;i<oct;i++){ sum += amp*(value1D(t*freq, seed+i*131u)*2.f-1.f); norm+=amp; amp*=gain; freq*=lac; }
    return sum/std::max(1e-6f,norm);
}

static inline std::vector<DayWeather> generate_weather(const WeatherParams& P) {
    std::vector<DayWeather> days(P.days);
    const uint32_t s32=(uint32_t)(P.seed^(P.seed>>32));

    // crude seasonal curve over the horizon of N days
    auto seasonal = [&](int d){
        float yearT = (float)d / 365.f * 6.2831853f; // 0..2π
        float season = std::sin(yearT - 1.5708f);    // shift so peak ~mid-year
        float latCool = 1.0f - P.latitude01;        // cooler toward poles
        return P.baseTempC + P.seasonalRangeC*season*std::max(0.2f, latCool);
    };

    // Pre-generate correlated noise signals
    for(int d=0; d<P.days; ++d){
        float t = d * P.noiseScale;
        float tempJit = fbm1D(t,4,2.0f,0.5f,s32^0x55);
        float wetness = (fbm1D(t+37.1f,4,2.0f,0.5f,s32^0x77) + 1.f)*0.5f; // 0..1
        float wind    = (fbm1D(t+73.7f,3,2.0f,0.5f,s32^0x99) + 1.f)*0.5f; // 0..1

        float meanC = seasonal(d) + tempJit*6.0f;
        float precip = std::max(0.f, (wetness - 0.55f)*60.0f); // mm/day
        float windKph = 4.0f + 28.0f*wind;

        Condition cond = Condition::Sunny;
        if (P.latitude01 < 0.25f && meanC < -1.0f && precip > 1.5f) cond = Condition::Snow;
        else if (precip > 25.0f && windKph > 18.0f) cond = Condition::Storm;
        else if (precip > 4.0f) cond = Condition::Rain;
        else if (precip > 1.0f) cond = Condition::Clouds;
        else cond = (windKph>20.0f ? Condition::Clouds : Condition::Sunny);

        days[d] = DayWeather{d, meanC, precip, windKph, cond};
    }
    return days;
}

} // namespace colony::procgen

#ifdef COLONY_PROCGEN_DEMOS
// Usage:
// auto wx = generate_weather(WeatherParams{.days=30,.latitude01=0.4f,.baseTempC=12});
// for(auto& d: wx){ /* HUD/UI update per day */ }
#endif
