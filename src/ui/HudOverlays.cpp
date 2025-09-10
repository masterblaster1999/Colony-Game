#include "HudOverlays.hpp"
#include "AtmosphereGameplayBridge.hpp"
#include <SDL.h>
#include <algorithm>
#include <cmath>

HudOverlays::HudOverlays(SDL_Renderer* r, AtmosphereAdapter atm)
: r_(r), atm_(atm)
{
    W_ = atm_.width ? atm_.width() : 0;
    H_ = atm_.height ? atm_.height() : 0;
}

HudOverlays::~HudOverlays() {
    if (tex_) SDL_DestroyTexture(tex_);
}

void HudOverlays::setOverlay(OverlayKind kind) {
    kind_ = kind;
}

void HudOverlays::ensureTexture() {
    if (!tex_ || W_!=atm_.width() || H_!=atm_.height()) {
        if (tex_) { SDL_DestroyTexture(tex_); tex_ = nullptr; }
        W_=atm_.width(); H_=atm_.height();
        tex_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, W_, H_);
        SDL_SetTextureBlendMode(tex_, SDL_BLENDMODE_BLEND);
    }
}

uint32_t HudOverlays::encodeColor(uint8_t R, uint8_t G, uint8_t B, uint8_t A) {
    return (uint32_t(R)<<24) | (uint32_t(G)<<16) | (uint32_t(B)<<8) | uint32_t(A);
}

void HudOverlays::colorForPO2(float po2, uint8_t& R, uint8_t& G, uint8_t& B) {
    // 0..20 kPa → red→yellow→green→cyan
    float t = std::clamp(po2/20.f, 0.f, 1.f);
    if (t < 0.33f) { // red -> yellow
        float k = t/0.33f;
        R = 255;
        G = uint8_t(255*k);
        B = 0;
    } else if (t < 0.66f) { // yellow -> green
        float k = (t-0.33f)/0.33f;
        R = uint8_t(255*(1.f-k));
        G = 255;
        B = 0;
    } else { // green -> cyan
        float k = (t-0.66f)/0.34f;
        R = 0;
        G = 255;
        B = uint8_t(255*k);
    }
}
void HudOverlays::colorForPressure(float p, uint8_t& R, uint8_t& G, uint8_t& B) {
    // Expect ~101 kPa at sea level; <70 bad (red), 70-90 (orange), 90-110 (green), >130 (purple)
    if (p < 70.f) { R=220; G=20;  B=20;  return; }
    if (p < 90.f) { R=255; G=140; B=0;   return; }
    if (p <=110.f){ R=20;  G=200; B=80;  return; }
    if (p <=130.f){ R=120; G=60;  B=200; return; }
    R=200; G=0; B=200;
}
void HudOverlays::colorForCO2(float c, uint8_t& R, uint8_t& G, uint8_t& B) {
    // 0..10% → dark->magenta
    float t = std::clamp(c/0.10f, 0.f, 1.f);
    R = uint8_t(80 + 175*t);
    G = uint8_t(10 +  30*(1.f-t));
    B = uint8_t(120 + 120*t);
}

void HudOverlays::refreshTexture() {
    if (!tex_ || !atm_.cellAt) return;

    void* pixels = nullptr; int pitch = 0;
    if (SDL_LockTexture(tex_, nullptr, &pixels, &pitch) != 0) return;

    uint8_t* row = static_cast<uint8_t*>(pixels);
    for (int y=0; y<H_; ++y) {
        uint32_t* col = reinterpret_cast<uint32_t*>(row);
        for (int x=0; x<W_; ++x) {
            auto c = atm_.cellAt(y*W_ + x);
            uint8_t R=0,G=0,B=0;
            switch (kind_) {
                case OverlayKind::OxygenPO2: { colorForPO2(c.o2_frac * c.pressure_kpa, R,G,B); break; }
                case OverlayKind::Pressure:  { colorForPressure(c.pressure_kpa, R,G,B); break; }
                case OverlayKind::CO2:       { colorForCO2(c.co2_frac, R,G,B); break; }
                case OverlayKind::None:      { R=0;G=0;B=0; break; }
            }
            col[x] = encodeColor(R,G,B, kind_==OverlayKind::None ? 0 : 180);
        }
        row += pitch;
    }
    SDL_UnlockTexture(tex_);
}

void HudOverlays::update(float dt) {
    if (kind_ == OverlayKind::None) return;
    accum_ += dt;
    // Refresh at ~4 Hz to keep it cheap.
    if (accum_ >= 0.25f) {
        ensureTexture();
        refreshTexture();
        accum_ = 0.0f;
    }
}

void HudOverlays::render(int screenW, int screenH, float worldToScreenScale, float camX, float camY) {
    if (kind_ == OverlayKind::None || !tex_) return;

    // Draw the texture scaled to the world grid, offset by camera.
    SDL_FRect dst;
    dst.x = -camX * worldToScreenScale;
    dst.y = -camY * worldToScreenScale;
    dst.w = float(W_) * worldToScreenScale;
    dst.h = float(H_) * worldToScreenScale;

    SDL_RenderCopyF(r_, tex_, nullptr, &dst);
}

void HudOverlays::renderMiniBar(int x, int y) {
    if (kind_ == OverlayKind::None) return;
    SDL_Rect r{ x, y, 160, 8};
    // simple legend bar (no text dependency)
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    for (int i=0;i<160;i++){
        uint8_t R=0,G=0,B=0;
        if (kind_==OverlayKind::OxygenPO2) colorForPO2((i/160.f)*20.f, R,G,B);
        else if (kind_==OverlayKind::Pressure) colorForPressure(60.f + (i/160.f)*80.f, R,G,B);
        else colorForCO2((i/160.f)*0.10f, R,G,B);
        SDL_SetRenderDrawColor(r_, R,G,B, 220);
        SDL_RenderDrawPoint(r_, x+i, y);
        SDL_RenderDrawPoint(r_, x+i, y+1);
        SDL_RenderDrawPoint(r_, x+i, y+2);
        SDL_RenderDrawPoint(r_, x+i, y+3);
    }
}
