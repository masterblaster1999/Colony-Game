#pragma once
#include <cstdint>
#include <functional>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

struct AtmosphereAdapter; // from the bridge header

enum class OverlayKind : uint8_t { None=0, OxygenPO2, Pressure, CO2 };

class HudOverlays {
public:
    HudOverlays(SDL_Renderer* r, AtmosphereAdapter atm);
    ~HudOverlays();

    void setOverlay(OverlayKind kind);     // choose which overlay to show
    OverlayKind overlay() const { return kind_; }

    void update(float dt);                 // call @ ~60Hz; internally throttled
    void render(int screenW, int screenH, float worldToScreenScale, float camX, float camY);

    // optional: always-on small status bar in a corner
    void renderMiniBar(int x, int y);

private:
    SDL_Renderer* r_;
    AtmosphereAdapter atm_;
    SDL_Texture* tex_{nullptr};
    OverlayKind kind_{OverlayKind::None};
    float accum_{0.0f}; // refresh throttle

    int W_{0}, H_{0};

    void ensureTexture();
    void refreshTexture(); // repaints from atmosphere
    uint32_t encodeColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a=180);

    // color maps
    void colorForPO2(float po2_kpa, uint8_t& R, uint8_t& G, uint8_t& B);
    void colorForPressure(float p_kpa, uint8_t& R, uint8_t& G, uint8_t& B);
    void colorForCO2(float co2_frac, uint8_t& R, uint8_t& G, uint8_t& B);
};
