#pragma once
#include <algorithm>
#include <cmath>

namespace sdf {

struct float2{ float x,y; };

inline float2 abs2(float2 v){ return { std::fabs(v.x), std::fabs(v.y) }; }
inline float sdCircle(float2 p, float r){ return std::sqrt(p.x*p.x + p.y*p.y) - r; }
inline float sdRoundBox(float2 p, float2 b, float r){
    float2 q = { abs2(p).x - b.x, abs2(p).y - b.y };
    float qx = std::max(q.x, 0.f), qy = std::max(q.y, 0.f);
    return std::sqrt(qx*qx + qy*qy) + std::min(std::max(q.x, q.y), 0.f) - r;
}
inline float opSmoothUnion(float d1,float d2,float k){
    float h = std::clamp(0.5f + 0.5f*(d2-d1)/k, 0.f, 1.f);
    return std::lerp(d2, d1, h) - k*h*(1.f-h);
}
inline float aaCoverage(float d, float aa){ // analytic coverage around edge
    float t = std::clamp((aa - d) / (2.f*aa), 0.f, 1.f);
    return t*t*(3.f - 2.f*t);
}

} // namespace sdf
