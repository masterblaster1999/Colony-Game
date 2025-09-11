#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include <algorithm>
#include <utility>

// We store AtmosphereAdapter by value in this class, so we must include the full definition.
#include "gameplay/AtmosphereGameplayBridge.hpp"

struct SDL_Renderer;
struct SDL_Texture;

/**
 * Which overlay to draw on the HUD.
 */
enum class OverlayKind : std::uint8_t { None = 0, OxygenPO2, Pressure, CO2 };

/**
 * Lightweight HUD overlays for atmosphere visualization (PO₂ / Pressure / CO₂).
 * 
 * Notes:
 *  - Windows-only builds: this header is platform-neutral, but the implementation uses SDL on Windows.
 *  - We keep the public API compatible, and add small, useful helpers (cycle, enable/disable, etc.).
 */
class HudOverlays {
public:
    HudOverlays(SDL_Renderer* r, AtmosphereAdapter atm);
    ~HudOverlays();

    // ----- Existing API (unchanged) -----
    void setOverlay(OverlayKind kind);                                 // choose which overlay to show
    [[nodiscard]] OverlayKind overlay() const noexcept { return kind_; }

    void update(float dt);                                              // call @ ~60Hz; internally throttled
    void render(int screenW, int screenH, float worldToScreenScale,
                float camX, float camY);

    // optional: always-on small status bar in a corner
    void renderMiniBar(int x, int y);

    // ----- New small, useful helpers (inline; no external deps) -----

    // Quickly cycle overlays (handy for key bindings)
    void cycleOverlayNext() noexcept {
        switch (kind_) {
            case OverlayKind::None:      kind_ = OverlayKind::OxygenPO2; break;
            case OverlayKind::OxygenPO2: kind_ = OverlayKind::Pressure;  break;
            case OverlayKind::Pressure:  kind_ = OverlayKind::CO2;       break;
            case OverlayKind::CO2:       kind_ = OverlayKind::None;      break;
        }
    }
    void cycleOverlayPrev() noexcept {
        switch (kind_) {
            case OverlayKind::None:      kind_ = OverlayKind::CO2;       break;
            case OverlayKind::CO2:       kind_ = OverlayKind::Pressure;  break;
            case OverlayKind::Pressure:  kind_ = OverlayKind::OxygenPO2; break;
            case OverlayKind::OxygenPO2: kind_ = OverlayKind::None;      break;
        }
    }

    // Enable/disable overlays without losing the previous selection
    void setEnabled(bool on) noexcept {
        if (on) {
            if (kind_ == OverlayKind::None) kind_ = (prevKind_ == OverlayKind::None)
                                                        ? OverlayKind::OxygenPO2 : prevKind_;
        } else {
            prevKind_ = kind_;
            kind_     = OverlayKind::None;
        }
    }
    [[nodiscard]] bool isEnabled() const noexcept { return kind_ != OverlayKind::None; }

    // Swap renderer at runtime (e.g., device reset). We drop the texture so it will be recreated.
    void setRenderer(SDL_Renderer* r) noexcept {
        if (r_ != r) {
            r_ = r;
            tex_ = nullptr; // will be recreated by ensureTexture()
            W_ = H_ = 0;
        }
    }

    // Update/replace the atmosphere data source.
    void setDataSource(const AtmosphereAdapter& atm) { atm_ = atm; }
    void setDataSource(AtmosphereAdapter&& atm) noexcept { atm_ = std::move(atm); }

    // If the device/renderer is lost, call this to force a texture re-create on next render.
    void onDeviceLost() noexcept { tex_ = nullptr; W_ = H_ = 0; }

    // Optional: simple legend alias—by default just delegates to the mini bar.
    void renderLegend(int x, int y, int /*w*/, int /*h*/) { renderMiniBar(x, y); }

    // Optional: toggle whether renderMiniBar should be used by the game loop
    void setMiniBarEnabled(bool enabled) noexcept { miniBarEnabled_ = enabled; }
    [[nodiscard]] bool miniBarEnabled() const noexcept { return miniBarEnabled_; }

    // Utility: pack a color in ABGR (SDL's default for SDL_TEXTUREACCESS_STREAMING + ARGB8888-like)
    // Adjust if your pixel format differs.
    static constexpr std::uint32_t packRGBA(std::uint8_t r, std::uint8_t g,
                                            std::uint8_t b, std::uint8_t a = 180) noexcept
    {
        // SDL's common 32-bit formats are little-endian; many pipelines expect 0xAABBGGRR.
        // Keep this consistent with encodeColor() implementation in the .cpp.
        return (static_cast<std::uint32_t>(a) << 24) |
               (static_cast<std::uint32_t>(b) << 16) |
               (static_cast<std::uint32_t>(g) <<  8) |
               (static_cast<std::uint32_t>(r) <<  0);
    }

private:
    // ----- Data -----
    SDL_Renderer*  r_{nullptr};
    AtmosphereAdapter atm_;
    SDL_Texture*   tex_{nullptr};

    OverlayKind    kind_{OverlayKind::None};
    OverlayKind    prevKind_{OverlayKind::OxygenPO2}; // restored when re-enabling

    float          accum_{0.0f}; // refresh throttle
    int            W_{0}, H_{0};

    bool           miniBarEnabled_{true};

    // ----- Internal helpers (implemented in .cpp) -----
    void ensureTexture();
    void refreshTexture(); // repaints from atmosphere

    // Packs a pixel (must match texture format used in the .cpp)
    std::uint32_t encodeColor(std::uint8_t r, std::uint8_t g,
                              std::uint8_t b, std::uint8_t a = 180);

    // Color maps (write RGB components)
    void colorForPO2(float po2_kpa, std::uint8_t& R, std::uint8_t& G, std::uint8_t& B);
    void colorForPressure(float p_kpa, std::uint8_t& R, std::uint8_t& G, std::uint8_t& B);
    void colorForCO2(float co2_frac, std::uint8_t& R, std::uint8_t& G, std::uint8_t& B);
};
