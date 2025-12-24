// ============================ Local helpers ============================

static inline float RandRange(std::mt19937& rng, float lo, float hi) {
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(rng);
}

static inline uint32_t MakeTag(char a, char b, char c, char d) {
    return (uint32_t)(uint8_t)a | ((uint32_t)(uint8_t)b << 8) | ((uint32_t)(uint8_t)c << 16) | ((uint32_t)(uint8_t)d << 24);
}

static inline float Length(const Vec3& v) {
    return std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
}

static inline Vec3 Sub(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

static inline float SafeDiv(float a, float b, float def = 0.0f) {
    return (std::abs(b) < 1e-6f) ? def : (a / b);
}

static inline float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

// Constant-power stereo pan helper (-1..+1)
static void BuildStereoPanMatrix(float pan /*-1..+1*/, UINT srcCh, UINT dstCh, std::vector<float>& out) {
    out.assign(srcCh * dstCh, 0.0f);
    if (dstCh < 2) return; // only meaningful for stereo-or-more outputs
    float t = Clamp01(0.5f * (pan + 1.0f));         // map to [0..1]
    float l = std::cos(0.5f * float(M_PI) * t);     // constant power
    float r = std::sin(0.5f * float(M_PI) * t);

    // If mono source, send to L/R; if stereo source, scale each accordingly.
    if (srcCh == 1) {
        out[0 * dstCh + 0] = l;
        out[0 * dstCh + 1] = r;
    } else {
        // Simplistic: scale first two channels
        out[0 * dstCh + 0] = l; // left->left
        out[1 * dstCh + 1] = r; // right->right
    }
}

