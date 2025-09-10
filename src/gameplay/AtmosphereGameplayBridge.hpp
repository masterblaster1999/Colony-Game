#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <limits>
#include <cmath>

// ---------- Forward decls (adapt to your actual types) ----------
struct SDL_Renderer; // only for optional logging overlays outside this file

struct AtmosphereSim; // your simulator
struct World;         // must provide tile/world mapping if you want nearest-safe BFS

using EntityId = uint32_t;

struct Vec2 { float x{0}, y{0}; };

// ---------- Adapter shims (EDIT: wire these to your AtmosphereSim) ----------
// 1) Tell the bridge how to sample a cell by tile index:
struct AtmosphereCell {
    float o2_frac;      // [0..1]
    float co2_frac;     // [0..1]
    float pressure_kpa; // absolute pressure in kPa
    bool  passable;     // for BFS / path costs
};
struct AtmosphereAdapter {
    // Required
    std::function<int()> width;    // tile width
    std::function<int()> height;   // tile height
    std::function<AtmosphereCell(int idx)> cellAt; // idx in [0, w*h)

    // Optional
    std::function<int(Vec2 world)> worldToIndex; // map world->tile index (or nullptr)
};

// ---------- Hooks the bridge calls back into your game ----------
enum class BreathStage : uint8_t { OK=0, DIZZY=1, DOWNED=2, DYING=3 };

struct BridgeHooks {
    // Apply health effects/damage per tick (dt already accounted for).
    std::function<void(EntityId, float /*damage*/, const char* /*type*/)> applyDamage = nullptr;

    // Mark downed/recovered.
    std::function<void(EntityId, bool /*downed*/)> setDowned = nullptr;

    // Show a toast or dev log.
    std::function<void(const std::string& /*msg*/)> toast = nullptr;

    // (Optional) Nudge AI toward a safe tile.
    std::function<void(EntityId, int /*tileIndex*/)> suggestMoveTo = nullptr;
};

// ---------- Config ----------
struct BreathConfig {
    // Physiology-ish defaults. Tune freely.
    float safe_PO2_kPa      = 13.0f; // ≈ altitude sickness threshold
    float dizzy_PO2_kPa     = 10.0f;
    float downed_PO2_kPa    = 8.0f;
    float dying_PO2_kPa     = 6.0f;

    float high_CO2_frac     = 0.05f; // 5% CO2: cognitive issues
    float lethal_CO2_frac   = 0.10f;

    float o2_recovery_per_s = 2.0f;  // blood O2 "reserve" recharge in safe air
    float o2_debt_per_s     = 3.0f;  // debt accumulation in bad air

    float dizzy_damage_per_s   = 1.0f;
    float downed_damage_per_s  = 5.0f;
    float dying_damage_per_s   = 20.0f;

    // Evac behavior
    bool  auto_evac_enabled    = true;
    float evac_repath_interval = 0.75f; // s
    int   evac_search_radius   = 60;    // tiles (Manhattan)
};

struct ColonistBreathState {
    float o2_reserve = 10.0f; // arbitrary "breath buffer" seconds
    BreathStage stage = BreathStage::OK;
    float time_since_evac_s = 0.0f;
};

struct ColonistBreathReport {
    float PO2_kPa{0}, P_kPa{0}, CO2_frac{0};
    BreathStage stage{BreathStage::OK};
    bool unsafe{false};
    int  nearest_safe_idx{-1}; // if searched
};

class AtmosphereGameplayBridge {
public:
    AtmosphereGameplayBridge(AtmosphereAdapter adapter, BreathConfig cfg, World* world=nullptr);

    // Call once per sim step for each colonist.
    ColonistBreathReport tickColonist(EntityId id, Vec2 worldPos, float dt, const BridgeHooks& hooks);

    // Utility: compute nearest breathable tile; returns -1 if none.
    int findNearestBreathable(int startIdx) const;

    // Breathability -> extra path cost (0 = good); safe if <= 1.0
    float breathabilityCostAt(int idx) const;

    // Allow runtime tuning.
    BreathConfig& config() { return cfg_; }

private:
    AtmosphereAdapter atm_;
    BreathConfig cfg_;
    World* world_;

    mutable std::vector<uint8_t> tmpVisited_; // BFS scratch
    int W_{0}, H_{0};

    std::unordered_map<EntityId, ColonistBreathState> colonist_;

    // helpers
    inline float clampf(float v, float a, float b) const { return (v < a) ? a : (v > b) ? b : v; }
    inline float PO2(const AtmosphereCell& c) const { return c.o2_frac * c.pressure_kpa; }

    BreathStage classifyStage(float PO2_kPa, float CO2_frac) const;
    void maybeAutoEvac(EntityId id, int hereIdx, ColonistBreathState& st, const BridgeHooks& hooks, const AtmosphereCell& c, float dt);

    bool isBreathable(const AtmosphereCell& c) const;
    bool inBounds(int x, int y) const { return (x>=0 && y>=0 && x<W_ && y<H_); }
    int  toIndex(int x, int y) const { return y*W_ + x; }
};
