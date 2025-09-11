// -------------------------------------------------------------------------------------------------
// HudOverlays.cpp
// Windows-only SDL2 HUD overlays for Colony-Game atmosphere visualization.
// Robust SDL include (fixes "SDL.h not found"), fast gradients, palette LUTs, and safer rendering.
// -------------------------------------------------------------------------------------------------
// Build notes (Windows/MSVC):
//   - Define SDL_MAIN_HANDLED in your build OR this file (we do it below) if you provide your own WinMain.
//   - Prefer CMake’s `find_package(SDL2 CONFIG REQUIRED)` and link `SDL2::SDL2 SDL2::SDL2main`.
//   - This TU does not depend on Linux/macOS APIs.
// -------------------------------------------------------------------------------------------------

#include "HudOverlays.hpp"
#include "gameplay/AtmosphereGameplayBridge.hpp"

// --- SDL include patch (robust) ---------------------------------------------------------------
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif

#if __has_include(<SDL.h>)
  #include <SDL.h>
  #if __has_include(<SDL_main.h>)
    #include <SDL_main.h>
  #endif
#elif __has_include(<SDL2/SDL.h>)
  #include <SDL2/SDL.h>
  #if __has_include(<SDL2/SDL_main.h>)
    #include <SDL2/SDL_main.h>
  #endif
#else
  #error "SDL2 headers not found. Install SDL2 and add include path (CMake: find_package(SDL2 CONFIG REQUIRED))."
#endif
// -----------------------------------------------------------------------------------------------

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <limits>

// ------------------------------------------------------------
// Internal helpers (no public API changes)
// ------------------------------------------------------------
namespace {

// Clamp helpers
inline float clamp01(float v)                 { return std::max(0.0f, std::min(1.0f, v)); }
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
            R = static_cast<std::uint8_t>(std::lround(lerpf(stops[i].r, stops[i + 1].r, w)));
            G = static_cast<std::uint8_t>(std::lround(lerpf(stops[i].g, stops[i + 1].g, w)));
            B = static_cast<std::uint8_t>(std::lround(lerpf(stops[i].b, stops[i + 1].b, w)));
            return;
        }
    }
    // Fallback (shouldn't hit)
    R = G = B = 0;
}

// SDL pixel format cache so encodeColor() is always correct for the actual texture format.
static SDL_PixelFormat*  g_sdlFmt    = nullptr;
static Uint32            g_fmtId     = 0;
static int               g_instances = 0;

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

// A tiny state backup to avoid leaking SDL render state changes to the rest of the game.
struct SDLRendererStateGuard {
    SDL_Renderer* r = nullptr;
    Uint8 cr=0,cg=0,cb=0,ca=0;
    SDL_BlendMode bm = SDL_BLENDMODE_NONE;
    explicit SDLRendererStateGuard(SDL_Renderer* rr) : r(rr) {
        if (!r) return;
        SDL_GetRenderDrawColor(r, &cr, &cg, &cb, &ca);
        SDL_GetRenderDrawBlendMode(r, &bm);
    }
    ~SDLRendererStateGuard() {
        if (!r) return;
        SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
        SDL_SetRenderDrawBlendMode(r, bm);
    }
};

// Palette LUTs (fast path). Rebuilt when pixel format changes.
struct PaletteLUT {
    std::array<std::uint32_t, 256> lut{}; // encoded with current g_sdlFmt and fixed overlay alpha
    Uint32 fmtId = 0;
    bool   built = false;
    void reset() { built = false; fmtId = 0; }
};

static PaletteLUT g_palPO2;
static PaletteLUT g_palPressure;
static PaletteLUT g_palCO2;

// Common alpha for overlays (tweakable here without changing any API).
constexpr Uint8 kOverlayAlpha = 180;

// Build a 256-entry LUT from a gradient and a domain.
inline void buildGradientLUT(PaletteLUT& pal,
                             const ColorStop* stops, std::size_t count,
                             float domainMin, float domainMax)
{
    pal.lut.fill(0);
    for (int i = 0; i < 256; ++i) {
        const float v = domainMin + (domainMax - domainMin) * (i / 255.0f);
        // Normalize to [0..1] for the given gradient; callers supply domainMin/max to match t mapping.
        float t = clamp01(inv_lerp(domainMin, domainMax, v));
        std::uint8_t R,G,B; sampleGradient(stops, count, t, R, G, B);
        pal.lut[static_cast<std::size_t>(i)] = mapRGBA(R, G, B, kOverlayAlpha);
    }
    pal.fmtId = g_fmtId;
    pal.built = true;
}

// Rebuild palettes lazily when we know the SDL pixel format (or it changed).
inline void rebuildPalettesIfNeeded() {
    if (!g_sdlFmt) return;
    if (g_palPO2.built && g_palPO2.fmtId == g_fmtId &&
        g_palPressure.built && g_palPressure.fmtId == g_fmtId &&
        g_palCO2.built && g_palCO2.fmtId == g_fmtId)
        return;

    // Oxygen partial pressure (kPa) — 0..21 range mapped to smooth daylight-like gradient.
    static const ColorStop s_po2[] = {
        {0.00f,  40,   0,  40}, // deep hypoxia
        {0.15f, 165,   0,   0},
        {0.30f, 255,  96,   0},
        {0.50f, 255, 255,   0},
        {0.75f,   0, 210,  90},
        {1.00f,   0, 200, 255}  // very high
    };

    // Absolute pressure (kPa) — 60..140 spans a wide, discernible gradient.
    static const ColorStop s_press[] = {
        {0.00f, 220,  20,  20},
        {0.25f, 255, 140,   0},
        {0.50f,  20, 200,  80},
        {0.70f,   0, 170, 180},
        {0.85f, 120,  60, 200},
        {1.00f, 200,   0, 200}
    };

    // CO2 fraction (0..10%) — violet→magenta ramp.
    static const ColorStop s_co2[] = {
        {0.00f,  10,  10,  30},
        {0.20f,  60,   0, 120},
        {0.50f, 140,   0, 180},
        {1.00f, 255,   0, 128}
    };

    buildGradientLUT(g_palPO2,      s_po2,    std::size(s_po2),    0.0f, 21.0f);
    buildGradientLUT(g_palPressure, s_press,  std::size(s_press), 60.0f,140.0f);
    buildGradientLUT(g_palCO2,      s_co2,    std::size(s_co2),    0.0f,  0.10f);
}

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
        g_palPO2.reset();
        g_palPressure.reset();
        g_palCO2.reset();
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
            // pixel format change means our LUTs need rebuild
            g_palPO2.reset();
            g_palPressure.reset();
            g_palCO2.reset();
        }
    }

    rebuildPalettesIfNeeded();
}

std::uint32_t HudOverlays::encodeColor(std::uint8_t R, std::uint8_t G, std::uint8_t B, std::uint8_t A) {
    return mapRGBA(R, G, B, A);
}

// ------------------------------------------------------------
// Color maps (domain-aware + smooth gradients)
//   (Kept for legend rendering and any ad-hoc color needs; runtime uses LUTs.)
// ------------------------------------------------------------

// Oxygen partial pressure (kPa); typical "green" band around 10–16 kPa.
void HudOverlays::colorForPO2(float po2_kpa, std::uint8_t& R, std::uint8_t& G, std::uint8_t& B) {
    const float t = clamp01(po2_kpa / 21.0f);
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
    const float t = clamp01(inv_lerp(60.0f, 140.0f, p_kpa));
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

    rebuildPalettesIfNeeded();

    void* pixels = nullptr; int pitch = 0;
    if (SDL_LockTexture(tex_, nullptr, &pixels, &pitch) != 0) {
        SDL_Log("HudOverlays: SDL_LockTexture failed: %s", SDL_GetError());
        return;
    }

    // pitch is in bytes; we write 32-bit pixels
    auto* row = static_cast<std::uint8_t*>(pixels);

    // Common domain scales for fast LUT index derivation
    constexpr float po2Min = 0.0f, po2Max = 21.0f;
    constexpr float prMin  = 60.0f, prMax  = 140.0f;
    constexpr float co2Min = 0.0f, co2Max = 0.10f;

    const float po2Scale = 255.0f / (po2Max - po2Min);
    const float prScale  = 255.0f / (prMax  - prMin);
    const float co2Scale = 255.0f / (co2Max - co2Min);

    const bool draw = (kind_ != OverlayKind::None);
    const auto* lut = (kind_ == OverlayKind::OxygenPO2) ? g_palPO2.lut.data() :
                      (kind_ == OverlayKind::Pressure)  ? g_palPressure.lut.data() :
                      (kind_ == OverlayKind::CO2)       ? g_palCO2.lut.data() : nullptr;

    for (int y = 0; y < H_; ++y) {
        auto* px = reinterpret_cast<std::uint32_t*>(row);
        const int base = y * W_;
        if (!draw) {
            // Clear this scanline to transparent
            std::fill(px, px + W_, 0u);
        } else {
            switch (kind_) {
                case OverlayKind::OxygenPO2:
                    for (int x = 0; x < W_; ++x) {
                        const auto c = atm_.cellAt(base + x);
                        const float v = std::max(po2Min, std::min(po2Max, c.o2_frac * c.pressure_kpa));
                        const int   i = std::clamp(int((v - po2Min) * po2Scale + 0.5f), 0, 255);
                        px[x] = lut[i];
                    }
                    break;
                case OverlayKind::Pressure:
                    for (int x = 0; x < W_; ++x) {
                        const auto c = atm_.cellAt(base + x);
                        const float v = std::max(prMin, std::min(prMax, c.pressure_kpa));
                        const int   i = std::clamp(int((v - prMin) * prScale + 0.5f), 0, 255);
                        px[x] = lut[i];
                    }
                    break;
                case OverlayKind::CO2:
                    for (int x = 0; x < W_; ++x) {
                        const auto c = atm_.cellAt(base + x);
                        const float v = std::max(co2Min, std::min(co2Max, c.co2_frac));
                        const int   i = std::clamp(int((v - co2Min) * co2Scale + 0.5f), 0, 255);
                        px[x] = lut[i];
                    }
                    break;
                default:
                    std::fill(px, px + W_, 0u);
                    break;
            }
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
#ifndef HUDOVERLAYS_REFRESH_HZ
#define HUDOVERLAYS_REFRESH_HZ 4
#endif
    constexpr float kRefreshInterval = 1.0f / float(HUDOVERLAYS_REFRESH_HZ);
    if (accum_ >= kRefreshInterval) {
        refreshTexture();
        accum_ = 0.0f;
    }
}

void HudOverlays::render(int /*screenW*/, int /*screenH*/, float worldToScreenScale,
                         float camX, float camY)
{
    if (kind_ == OverlayKind::None || !tex_ || !r_) return;

    // The atmospheric grid is W_ x H_ in "world" cells; draw it scaled and offset by camera.
    float x = -camX * worldToScreenScale;
    float y = -camY * worldToScreenScale;
    float w = static_cast<float>(W_) * worldToScreenScale;
    float h = static_cast<float>(H_) * worldToScreenScale;

    // Snap to integer pixels when scale is near-integer for crisp pixel-art (reduces shimmer).
    auto snap_if_near_int = [](float v) {
        float r = std::round(v);
        return (std::fabs(v - r) < 1e-4f) ? r : v;
    };
    x = snap_if_near_int(x);
    y = snap_if_near_int(y);
    w = snap_if_near_int(w);
    h = snap_if_near_int(h);

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

    SDLRendererStateGuard _guard(r_); // avoid leaking blend mode or draw color

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
