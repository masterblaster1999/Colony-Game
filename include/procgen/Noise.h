#pragma once
#include <array>
#include <vector>
#include <random>
#include <cmath>

namespace procgen {

class Perlin {
    std::array<int,512> p{};
    static inline float fade(float t){ return t*t*t*(t*(t*6-15)+10); }
    static inline float lerp(float a,float b,float t){ return a + t*(b-a); }
    static inline float grad(int h, float x, float y, float z){
        int b = h & 15;
        float u = b<8?x:y;
        float v = b<4?y:(b==12||b==14?x:z);
        return ((b&1)?-u:u) + ((b&2)?-v:v);
    }
public:
    explicit Perlin(uint32_t seed=1337u){
        std::array<int,256> base{};
        for (int i=0;i<256;++i) base[i]=i;
        std::mt19937 rng(seed);
        for (int i=255;i>=0;--i){ std::uniform_int_distribution<int> d(0,i); std::swap(base[i], base[d(rng)]); }
        for (int i=0;i<512;++i) p[i]=base[i&255];
    }

    float noise(float x,float y,float z=0.f) const {
        int X = (int)std::floor(x) & 255;
        int Y = (int)std::floor(y) & 255;
        int Z = (int)std::floor(z) & 255;
        x -= std::floor(x); y -= std::floor(y); z -= std::floor(z);
        float u=fade(x), v=fade(y), w=fade(z);

        int A = p[X]+Y, AA = p[A]+Z, AB = p[A+1]+Z;
        int B = p[X+1]+Y, BA = p[B]+Z, BB = p[B+1]+Z;

        float res =
          lerp( lerp( lerp( grad(p[AA  ], x  , y  , z  ),
                            grad(p[BA  ], x-1, y  , z  ), u),
                      lerp( grad(p[AB  ], x  , y-1, z  ),
                            grad(p[BB  ], x-1, y-1, z  ), u), v),
                lerp( lerp( grad(p[AA+1], x  , y  , z-1),
                            grad(p[BA+1], x-1, y  , z-1), u),
                      lerp( grad(p[AB+1], x  , y-1, z-1),
                            grad(p[BB+1], x-1, y-1, z-1), u), v), w);
        // res in [-1,1]
        return res;
    }

    float fbm2(float x, float y, int oct=5, float lac=2.0f, float gain=0.5f, float freq=1.0f) const {
        float amp=1.0f, sum=0.0f, norm=0.0f;
        for(int i=0;i<oct;++i){
            sum += amp * noise(x*freq, y*freq, 0.0f);
            norm += amp;
            amp *= gain;
            freq *= lac;
        }
        return sum / norm; // ~[-1,1]
    }

    // Domain warp trick.
    float warped2(float x, float y, float freq, float warpAmp, float warpFreq) const {
        float dx = noise(x*warpFreq, y*warpFreq, 37.0f) * warpAmp;
        float dy = noise((x+5.2f)*warpFreq, (y+1.3f)*warpFreq, 11.0f) * warpAmp;
        return noise((x+dx)*freq, (y+dy)*freq, 0.0f);
    }
};

} // namespace procgen
