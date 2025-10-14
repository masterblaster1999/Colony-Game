// 3D Perlin + 3D Worley + fBm + domain warping
// References: Improved Perlin (GPU Gems 2 ch26), Worley 96, Book of Shaders (noise & cellular), IQ domain warping.
// (HLSL port; no external dependencies)  [See citations in the main text]

static const float PI = 3.14159265359;

// ---- Hash & helpers ----
float3 hash33(float3 p) {
    // Dave Hoskins-like hash
    p = frac(p * 0.3183099 + float3(0.71, 0.113, 0.419));
    p += dot(p, p.yzx + 19.19);
    return frac(float3((p.x + p.y)*p.z, (p.x + p.z)*p.y, (p.y + p.z)*p.x));
}

float fade(float t) { return t*t*t*(t*(t*6 - 15) + 10); }

// ---- Gradient noise (Perlin 3D) ----
float grad(float3 ip, float3 fp) {
    float3 g = normalize(hash33(ip) * 2.0 - 1.0);
    return dot(g, fp);
}

// Scalar Perlin 3D in [0,1]
float perlin3(float3 p) {
    float3 i = floor(p);
    float3 f = frac(p);
    float3 u = float3(fade(f.x), fade(f.y), fade(f.z));

    float n000 = grad(i + float3(0,0,0), f - float3(0,0,0));
    float n001 = grad(i + float3(0,0,1), f - float3(0,0,1));
    float n010 = grad(i + float3(0,1,0), f - float3(0,1,0));
    float n011 = grad(i + float3(0,1,1), f - float3(0,1,1));
    float n100 = grad(i + float3(1,0,0), f - float3(1,0,0));
    float n101 = grad(i + float3(1,0,1), f - float3(1,0,1));
    float n110 = grad(i + float3(1,1,0), f - float3(1,1,0));
    float n111 = grad(i + float3(1,1,1), f - float3(1,1,1));

    float nx00 = lerp(n000, n100, u.x);
    float nx01 = lerp(n001, n101, u.x);
    float nx10 = lerp(n010, n110, u.x);
    float nx11 = lerp(n011, n111, u.x);
    float nxy0 = lerp(nx00, nx10, u.y);
    float nxy1 = lerp(nx01, nx11, u.y);
    float nxyz = lerp(nxy0, nxy1, u.z);

    return 0.5 + 0.5 * nxyz;
}

float fbm3(float3 p, int octaves, float lacunarity, float gain) {
    float a = 0.5;
    float f = 1.0;
    float sum = 0.0;
    [unroll(8)]
    for (int i = 0; i < octaves; ++i) {
        sum += a * perlin3(p * f);
        f *= lacunarity;
        a *= gain;
    }
    return sum;
}

// ---- Worley 3D (F1) ----
float worley3(float3 p) {
    float3 ip = floor(p);
    float3 fp = frac(p);
    float d = 1e9;
    [unroll]
    for (int z=-1; z<=1; ++z)
    [unroll]
    for (int y=-1; y<=1; ++y)
    [unroll]
    for (int x=-1; x<=1; ++x) {
        float3 cell = ip + float3(x,y,z);
        float3 rnd = hash33(cell);
        float3 feature = rnd; // feature point in cell [0,1]^3
        float3 r = float3(x,y,z) + feature - fp;
        d = min(d, dot(r,r));
    }
    return sqrt(d); // Euclidean
}

// Map Worley to [0,1] with contrast
float worley01(float3 p, float contrast) {
    float w = saturate(1.0 - worley3(p));
    return pow(w, contrast);
}

// ---- Domain warp (IQ-style) ----
float3 domainWarp(float3 p, float freq1, float amp1, float freq2, float amp2) {
    float3 q = p + (float3(perlin3(p*freq1), perlin3(p*freq1 + 19.1), perlin3(p*freq1 + 3.7)) - 0.5) * 2.0 * amp1;
    float3 r = p + (float3(perlin3(q*freq2), perlin3(q*freq2 + 7.7), perlin3(q*freq2 + 13.3)) - 0.5) * 2.0 * amp2;
    return r;
}
