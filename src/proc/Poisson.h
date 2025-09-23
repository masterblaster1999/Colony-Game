#pragma once
#include <vector>
#include <random>
#include <cmath>

struct P2 { float x,y; };
inline float d2(const P2&a,const P2&b){float dx=a.x-b.x,dy=a.y-b.y;return dx*dx+dy*dy;}

// Bridson 2D Poisson-disk sampling in [0,W]x[0,H]
std::vector<P2> Poisson2D(float W, float H, float r, uint32_t seed=1337, int k=30);
