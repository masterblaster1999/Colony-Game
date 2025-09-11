#include "HudOverlays.hpp"
// Keep the TU self‑sufficient and make Windows include paths robust:
#include "gameplay/AtmosphereGameplayBridge.hpp"

#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

// ------------------------------------------------------------
// Internal helpers (no public API changes)
// ------------------------------------------------------------
namespace {

// Clamp helpers
inline float clamp01(float v)            { return std::max(0.0f, std::min(1.0f, v)); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float inv_lerp(float a, float b, float v) {
    if (a == b) return 0.0f;
    return (v - a) / (b - a);
}

// A simple gradient sampler (piecewise linear between color stops).
struct ColorStop { float t; std::uint8_t r, g, b; };

inline void sampleGradient(const ColorStop* stops, std::size_t count, float t,
                           std::uint8_t& R, std::uint8_t& G, std::uint8_t& B)
{
    if (count == 0) { R = G = B = 0; return; }
    if (t <= stops[0].t) { R = stops[0].r; G = stops[0].g; B = stops[0].b; return; }
    if (t >= stops[count - 1].t) { R = stops[count - 1].r; G = stops[count - 1].g; B = stops[count - 1].b; return; }

    for (std::size_t i = 0; i + 1 < count; ++i) {
        if (t >= stops[i].t && t <= stops[i + 1].t) {
            const float w = (t - stops[i].t) / (stops[i + 1].t - stops[i].t);
            R = static_cast<std::uint8_t>(std::round(lerpf(stops[i].r, stops[i + 1].r, w)));
            G = static_cast<std::uint8_t>(std::round(lerpf(stops[i].g, stops[i + 1].g, w)));
            B = static_cast<std::uint8_t>(std::round(lerpf(stops[i].b, stops[i + 1].b, w)));
            return;
        }
    }
    // Fallback (shouldn't hit)
    R = G = B = 0;
}

// SDL pixel format cache so encodeColor() is always correct for the actual texture format.
static SDL_PixelFormat*  g_sdlFmt   = nullptr;
static Uint32            g_fmtId    = 0;
static int               g_instances= 0;

// Map RGBA through the actual SDL format; if unavailable, fall back to a sane little-endian packing.
inline std::uint32_t mapRGBA(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    if (g_sdlFmt) {
        return SDL_MapRGBA(g_sdlFmt, r, g, b, a);
    }
    // Fallback for RGBA8888-like formats (little-endian -> AABBGGRR)
    return (static_cast<std::uint32_t>(a) << 24) |
           (static_cast<std::uint32_t>(b) << 16) |
           (static_cast<std::uint32_t>(g) <<  8) |
           (static_cast<std::uint32_t>(r) <<  0);
}

#if SDL_VERSION_ATLEAST(2,0,12)
inline void setTextureScaleMode(SDL_Texture* t) {
    // Crisp pixel-art by default; change to SDL_ScaleModeLinear if you prefer smooth scaling.
    SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest);
}
#else
inline void setTextureScaleMode(SDL_Texture*) {}
#endif

} // namespace

// ------------------------------------------------------------
// Construction / Destruction
// ------------------------------------------------------------
HudOverlays::HudOverlays(SDL_Renderer* r, AtmosphereAdapter atm)
    : r_(r), atm_(std::move(atm))
{
    ++g_instances;
    W_ = atm_.width  ? atm_.width()  : 0;
    H_ = atm_.height ? atm_.height() : 0;
}

HudOverlays::~HudOverlays() {
    if (tex_) { SDL_DestroyTexture(tex_); tex_ = nullptr; }

    if (--g_instances == 0) {
        if (g_sdlFmt) {
            SDL_FreeFormat(g_sdlFmt);
            g_sdlFmt = nullptr;
            g_fmtId  = 0;
        }
    }
}

// ------------------------------------------------------------
// Public API
// ------------------------------------------------------------
void HudOverlays::setOverlay(OverlayKind kind) {
    if (kind_ != kind) {
        kind_ = kind;
        accum_ = 9999.0f; // force an immediate refresh on next update()
    }
}

void HudOverlays::ensureTexture() {
    if (!r_) return;

    const int newW = atm_.width  ? atm_.width()  : 0;
    const int newH = atm_.height ? atm_.height() : 0;
    if (newW <= 0 || newH <= 0) {
        // No data yet; drop texture if any
        if (tex_) { SDL_DestroyTexture(tex_); tex_ = nullptr; }
        W_ = H_ = 0;
        return;
    }

    const bool needNewTex = (!tex_) || (newW != W_) || (newH != H_);
    if (!needNewTex) return;

    if (tex_) { SDL_DestroyTexture(tex_); tex_ = nullptr; }

    W_ = newW; H_ = newH;

    // Create a streaming texture we can write into.
    // We request RGBA8888 but we still query the real format for correctness.
    const Uint32 requested = SDL_PIXELFORMAT_RGBA8888;
    tex_ = SDL_CreateTexture(r_, requested, SDL_TEXTUREACCESS_STREAMING, W_, H_);
    if (!tex_) {
        SDL_Log("HudOverlays: SDL_CreateTexture failed: %s", SDL_GetError());
        return;
    }

    SDL_SetTextureBlendMode(tex_, SDL_BLENDMODE_BLEND);
    setTextureScaleMode(tex_);

    // Cache the actual texture pixel format for encodeColor():
    Uint32 actualFmt = 0; int aW=0, aH=0;
    if (SDL_QueryTexture(tex_, &actualFmt, nullptr, &aW, &aH) == 0) {
        if (!g_sdlFmt || actualFmt != g_fmtId) {
            if (g_sdlFmt) SDL_FreeFormat(g_sdlFmt);
            g_sdlFmt = SDL_AllocFormat(actualFmt);
            g_fmtId  = actualFmt;
        }
    }
}

std::uint32_t HudOverlays::encodeColor(std::uint8_t R, std::uint8_t G, std::uint8_t B, std::uint8_t A) {
    return mapRGBA(R, G, B, A);
}

// ------------------------------------------------------------
// Color maps (domain-aware + smooth gradients)
// ------------------------------------------------------------

// Oxygen partial pressure (kPa); typical "green" band around 10–16 kPa.
void HudOverlays::colorForPO2(float po2_kpa, std::uint8_t& R, std::uint8_t& G, std::uint8_t& B) {
    // Normalize to [0..1] over 0..21 kPa (rough sea-level inspired upper bound for visualization)
    const float t = clamp01(po2_kpa / 21.0f);

    // Dark magenta -> red -> orange -> yellow -> green -> cyan
    static const ColorStop stops[] = {
        {0.00f,  40,   0,  40},
        {0.15f, 165,   0,   0},
        {0.30f, 255,  96,   0},
        {0.50f, 255, 255,   0},
        {0.75f,   0, 210,  90},
        {1.00f,   0, 200, 255}
    };
    sampleGradient(stops, std::size(stops), t, R, G, B);
}

// Absolute pressure (kPa) visualization around the ~101 kPa norm
void HudOverlays::colorForPressure(float p_kpa, std::uint8_t& R, std::uint8_t& G, std::uint8_t& B) {
    // Normalize 60..140 kPa (cabin/colony relevant range) → [0..1]
    const float t = clamp01(inv_lerp(60.0f, 140.0f, p_kpa));

    // Deep red -> orange -> green -> teal -> purple -> magenta
    static const ColorStop stops[] = {
        {0.00f, 220,  20,  20}, // ~60 kPa (very low)
        {0.25f, 255, 140,   0}, // ~80
        {0.50f,  20, 200,  80}, // ~100 (good)
        {0.70f,   0, 170, 180}, // ~116
        {0.85f, 120,  60, 200}, // ~130
        {1.00f, 200,   0, 200}  // ~140 (very high)
    };
    sampleGradient(stops, std::size(stops), t, R, G, B);
}

// CO₂ fraction (0..1). We'll map the 0..10% band for color, with anything above clamped red/magenta.
void HudOverlays::colorForCO2(float co2_frac, std::uint8_t& R, std::uint8_t& G, std::uint8_t& B) {
    const float t = clamp01(co2_frac / 0.10f); // 0..10%
    // dark -> violet -> fuchsia -> hot pink
    static const ColorStop stops[] = {
        {0.00f,  10,  10,  30},
        {0.20f,  60,   0, 120},
        {0.50f, 140,   0, 180},
        {1.00f, 255,   0, 128}
    };
    sampleGradient(stops, std::size(stops), t, R, G, B);
}

// ------------------------------------------------------------
// Painting
// ------------------------------------------------------------
void HudOverlays::refreshTexture() {
    if (!tex_) return;
    if (!atm_.cellAt) return; // std::function empty? then bail

    void* pixels = nullptr; int pitch = 0;
    if (SDL_LockTexture(tex_, nullptr, &pixels, &pitch) != 0) {
        SDL_Log("HudOverlays: SDL_LockTexture failed: %s", SDL_GetError());
        return;
    }

    // pitch is in bytes; we write 32-bit pixels
    auto* row = static_cast<std::uint8_t*>(pixels);

    const bool draw = (kind_ != OverlayKind::None);
    const std::uint8_t alpha = draw ? 180 : 0;

    for (int y = 0; y < H_; ++y) {
        auto* px = reinterpret_cast<std::uint32_t*>(row);
        const int base = y * W_;
        for (int x = 0; x < W_; ++x) {
            std::uint8_t R = 0, G = 0, B = 0;
            if (draw) {
                const auto c = atm_.cellAt(base + x);
                switch (kind_) {
                    case OverlayKind::OxygenPO2: {
                        const float po2 = c.o2_frac * c.pressure_kpa;
                        colorForPO2(po2, R, G, B);
                    } break;
                    case OverlayKind::Pressure: {
                        colorForPressure(c.pressure_kpa, R, G, B);
                    } break;
                    case OverlayKind::CO2: {
                        colorForCO2(c.co2_frac, R, G, B);
                    } break;
                    case OverlayKind::None: default:
                        break;
                }
            }
            px[x] = encodeColor(R, G, B, alpha);
        }
        row += pitch;
    }
    SDL_UnlockTexture(tex_);
}

void HudOverlays::update(float dt) {
    // Always ensure the texture reflects current grid size (if we have an overlay selected).
    if (kind_ == OverlayKind::None) return;

    // Guard against huge dt spikes so our throttle logic still works.
    if (!std::isfinite(dt)) dt = 0.0f;
    dt = std::min(dt, 0.5f);

    ensureTexture();
    if (!tex_) return;

    accum_ += dt;
    // Refresh at ~4 Hz to keep it cheap (or immediately after setOverlay())
    if (accum_ >= 0.25f) {
        refreshTexture();
        accum_ = 0.0f;
    }
}

void HudOverlays::render(int /*screenW*/, int /*screenH*/, float worldToScreenScale,
                         float camX, float camY)
{
    if (kind_ == OverlayKind::None || !tex_ || !r_) return;

    // The atmospheric grid is W_ x H_ in "world" cells; draw it scaled and offset by camera.
    const float x = -camX * worldToScreenScale;
    const float y = -camY * worldToScreenScale;
    const float w = static_cast<float>(W_) * worldToScreenScale;
    const float h = static_cast<float>(H_) * worldToScreenScale;

#if SDL_VERSION_ATLEAST(2,0,10)
    SDL_FRect dst { x, y, w, h };
    SDL_RenderCopyF(r_, tex_, nullptr, &dst);
#else
    SDL_Rect dst { static_cast<int>(std::lround(x)),
                   static_cast<int>(std::lround(y)),
                   static_cast<int>(std::lround(w)),
                   static_cast<int>(std::lround(h)) };
    SDL_RenderCopy(r_, tex_, nullptr, &dst);
#endif
}

void HudOverlays::renderMiniBar(int x, int y) {
    if (!r_ || kind_ == OverlayKind::None) return;

    // Simple, dependency-free legend bar with a 1px border and five tick marks.
    const int BAR_W = 160;
    const int BAR_H = 8;

    SDL_Rect border { x - 2, y - 2, BAR_W + 4, BAR_H + 4 };
    SDL_Rect back   { x - 1, y - 1, BAR_W + 2, BAR_H + 2 };

    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);

    // Background
    SDL_SetRenderDrawColor(r_, 10, 10, 14, 160);
    SDL_RenderFillRect(r_, &back);

    // Gradient strip
    for (int i = 0; i < BAR_W; ++i) {
        std::uint8_t R = 0, G = 0, B = 0;
        if (kind_ == OverlayKind::OxygenPO2) {
            colorForPO2((i / static_cast<float>(BAR_W - 1)) * 21.0f, R, G, B);
        } else if (kind_ == OverlayKind::Pressure) {
            colorForPressure(60.0f + (i / static_cast<float>(BAR_W - 1)) * (140.0f - 60.0f), R, G, B);
        } else { // CO2
            colorForCO2((i / static_cast<float>(BAR_W - 1)) * 0.10f, R, G, B);
        }
        SDL_SetRenderDrawColor(r_, R, G, B, 220);
        // Thicker line by drawing multiple rows
        for (int yy = 0; yy < BAR_H; ++yy) {
            SDL_RenderDrawPoint(r_, x + i, y + yy);
        }
    }

    // Ticks (0, 25, 50, 75, 100%)
    SDL_SetRenderDrawColor(r_, 240, 240, 240, 190);
    for (int t = 0; t <= 4; ++t) {
        const int ix = x + (t * (BAR_W - 1)) / 4;
        SDL_RenderDrawLine(r_, ix, y - 3, ix, y + BAR_H + 2);
    }

    // Border
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 220);
    SDL_RenderDrawRect(r_, &border);
}
