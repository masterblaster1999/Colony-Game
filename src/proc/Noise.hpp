#pragma once
#include <cmath>
#include <algorithm>
#include "PCG32.hpp"

namespace proc {

inline float fade(float t){ return t*t*t*(t*(t*6-15)+10); }
inline float lerp(float a,float b,float t){ return a + (b-a)*t; }
inline uint32_t hash2i(int x,int y,uint32_t seed=0x9E3779B9u){
    uint32_t h=seed; h^=uint32_t(x)*0x27d4eb2dU; h=(h<<13)|(h>>19); h*=0x85ebca6bU;
    h^=uint32_t(y)*0x165667b1U; h^=(h>>16); h*=0xc2b2ae35U; h^=(h>>16); return h;
}
inline float rand01(int x,int y,uint32_t seed=0){ return (hash2i(x,y,seed)&0xffff)*(1.f/65535.f); }

inline float value2D(float x,float y,uint32_t seed=0){
    int xi=(int)std::floor(x), yi=(int)std::floor(y);
    float xf=x-xi, yf=y-yi;
    float v00=rand01(xi,yi,seed), v10=rand01(xi+1,yi,seed),
          v01=rand01(xi,yi+1,seed), v11=rand01(xi+1,yi+1,seed);
    float u=fade(xf), v=fade(yf);
    return lerp(lerp(v00,v10,u), lerp(v01,v11,u), v);
}

inline float fbm2D(float x,float y,int oct=5,float lac=2.0f,float gain=0.5f,uint32_t seed=0){
    float a=1.f, f=1.f, sum=0.f, norm=0.f;
    for(int i=0;i<oct;++i){ sum+=a*value2D(x*f,y*f,seed+i*131); norm+=a; a*=gain; f*=lac; }
    return sum/(norm>0?norm:1.f);
}

// Worley cell noise F1 (distance to nearest jittered point in grid cell neighborhood)
inline float worley2D(float x,float y, uint32_t seed=0){
    int xi=(int)std::floor(x), yi=(int)std::floor(y);
    float fx=x - xi, fy=y - yi, dmin=1e9f;
    for(int j=-1;j<=1;++j) for(int i=-1;i<=1;++i){
        uint32_t h = hash2i(xi+i, yi+j, seed);
        float rx = (float)((h&0xffff))/65535.0f;
        float ry = (float)((h>>16))/65535.0f;
        float px = (float)(i) + rx; float py = (float)(j) + ry;
        float dx = fx - px, dy = fy - py;
        float d2 = dx*dx + dy*dy;
        dmin = std::min(dmin, d2);
    }
    return std::sqrt(dmin); // [0, ~1]
}

} // namespace proc
