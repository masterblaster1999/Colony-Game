#include "AtmosphereGameplayBridge.hpp"
#include <queue>
#include <algorithm>
#include <cmath>

// -------------------------------------------------------------------------------------------------
// Improvements in this version
// -------------------------------------------------------------------------------------------------
// 1) **Fix C4244 (int → uint8_t) narrowing** in all fills/assigns of tmpVisited_
//    by passing `static_cast<uint8_t>(0)` and writing `static_cast<uint8_t>(1)`.
//
// 2) **Safer, clearer stage classification**: simplify branches and keep the original intent
//    (CO₂ can bump severity).
//
// 3) **Fast paths & robustness**
//    - Early return if the starting tile is already breathable.
//    - Bounds & nullptr checks remain defensive.
//    - Recompute visited buffer size when the atmosphere grid resizes.
//
// 4) **Path realism in BFS** (optional but low risk):
//    - When exploring neighbors we now expand only through *passable* cells. This reduces
//      suggestions that are unreachable (e.g., through walls), pushing the evac target selection
//      toward realistically reachable tiles. Target tiles must still be breathable.
//
// 5) **Micro-optimizations**
//    - Common neighbor offsets made file-static constants.
//    - Avoid repeated reallocation of `tmpVisited_` except on dimension change.
// -------------------------------------------------------------------------------------------------

// 4-neighborhood offsets (right, left, down, up)
static constexpr int kDX[4] = { 1, -1,  0,  0 };
static constexpr int kDY[4] = { 0,  0,  1, -1 };

AtmosphereGameplayBridge::AtmosphereGameplayBridge(AtmosphereAdapter adapter, BreathConfig cfg, World* world)
: atm_(adapter), cfg_(cfg), world_(world)
{
    W_ = atm_.width ? atm_.width() : 0;
    H_ = atm_.height ? atm_.height() : 0;
    // PATCH: remove int → uint8_t narrowing by casting the fill value
    tmpVisited_.assign(static_cast<size_t>(W_) * static_cast<size_t>(H_), static_cast<uint8_t>(0));
}

BreathStage AtmosphereGameplayBridge::classifyStage(float PO2_kPa, float CO2_frac) const {
    // CO2 can elevate severity one notch if very high.
    const int bump =
        (CO2_frac >= cfg_.lethal_CO2_frac) ? 2 :
        (CO2_frac >= cfg_.high_CO2_frac)   ? 1 : 0;

    // Simplified & clarified (first branch was redundant in the original).
    if (PO2_kPa <= cfg_.dying_PO2_kPa)   return BreathStage::DYING;
    if (PO2_kPa <= cfg_.downed_PO2_kPa)  return (bump >= 1) ? BreathStage::DYING  : BreathStage::DOWNED;
    if (PO2_kPa <= cfg_.dizzy_PO2_kPa)   return (bump >= 1) ? BreathStage::DOWNED : BreathStage::DIZZY;
    if (PO2_kPa <= cfg_.safe_PO2_kPa)    return (bump >= 1) ? BreathStage::DIZZY  : BreathStage::OK;
    return BreathStage::OK;
}

bool AtmosphereGameplayBridge::isBreathable(const AtmosphereCell& c) const {
    const float po2 = PO2(c);
    return (po2 >= cfg_.safe_PO2_kPa * 0.95f) && (c.co2_frac < cfg_.high_CO2_frac) && c.passable;
}

int AtmosphereGameplayBridge::findNearestBreathable(int startIdx) const {
    if (!atm_.cellAt) return -1;
    const int total = W_ * H_;
    if (startIdx < 0 || startIdx >= total) return -1;

    // Fast path: starting tile already good.
    {
        const AtmosphereCell c0 = atm_.cellAt(startIdx);
        if (isBreathable(c0)) return startIdx;
    }

    // PATCH: remove int → uint8_t narrowing in fill value
    std::fill(tmpVisited_.begin(), tmpVisited_.end(), static_cast<uint8_t>(0));

    std::queue<int> q;
    q.push(startIdx);
    tmpVisited_[startIdx] = static_cast<uint8_t>(1);

    while (!q.empty()) {
        const int i = q.front(); q.pop();
        const AtmosphereCell c = atm_.cellAt(i);
        if (isBreathable(c)) return i;

        const int x = i % W_;
        const int y = i / W_;
        for (int k = 0; k < 4; ++k) {
            const int nx = x + kDX[k];
            const int ny = y + kDY[k];
            if (!inBounds(nx, ny)) continue;

            const int ni = toIndex(nx, ny);
            if (tmpVisited_[ni]) continue;

            // **Upgrade**: expand only through passable tiles to avoid unreachable suggestions.
            const AtmosphereCell nc = atm_.cellAt(ni);
            if (!nc.passable) continue;

            tmpVisited_[ni] = static_cast<uint8_t>(1);
            q.push(ni);
        }
    }
    return -1;
}

float AtmosphereGameplayBridge::breathabilityCostAt(int idx) const {
    if (!atm_.cellAt) return 0.0f;
    const AtmosphereCell c = atm_.cellAt(idx);
    const float po2 = PO2(c);
    const float co2 = c.co2_frac;

    // 0 = safe; grows quickly as PO2 drops or CO2 rises.
    // Add tiny eps to avoid divide-by-zero at config limits.
    constexpr float EPS = 1e-4f;
    const float po2_den = std::max(cfg_.safe_PO2_kPa - cfg_.dying_PO2_kPa, EPS);
    const float co2_den = std::max(cfg_.lethal_CO2_frac - cfg_.high_CO2_frac, EPS);

    const float po2_pen = (po2 >= cfg_.safe_PO2_kPa) ? 0.f : (cfg_.safe_PO2_kPa - po2) / po2_den;
    const float co2_pen = (co2 <= cfg_.high_CO2_frac) ? 0.f : (co2 - cfg_.high_CO2_frac) / co2_den;
    const float pass_pen = (c.passable ? 0.f : 10.f); // non-passable: huge

    return (po2_pen * 4.f + co2_pen * 2.f + pass_pen);
}

ColonistBreathReport AtmosphereGameplayBridge::tickColonist(EntityId id, Vec2 worldPos, float dt, const BridgeHooks& hooks) {
    ColonistBreathReport out{};
    if (!atm_.cellAt || !atm_.width || !atm_.height) return out;

    // Refresh cached dimensions and resize visited buffer as needed.
    if (W_ != atm_.width() || H_ != atm_.height()) {
        W_ = atm_.width(); H_ = atm_.height();
        // PATCH: remove int → uint8_t narrowing in assign value
        tmpVisited_.assign(static_cast<size_t>(W_) * static_cast<size_t>(H_), static_cast<uint8_t>(0));
    }

    int idx = -1;
    if (atm_.worldToIndex) {
        idx = atm_.worldToIndex(worldPos);
    } else {
        // Fallback: assume world coords == tile coords
        const int x = static_cast<int>(std::floor(worldPos.x));
        const int y = static_cast<int>(std::floor(worldPos.y));
        if (inBounds(x, y)) idx = toIndex(x, y);
    }
    if (idx < 0) return out;

    const AtmosphereCell c = atm_.cellAt(idx);
    out.PO2_kPa  = PO2(c);
    out.P_kPa    = c.pressure_kpa;
    out.CO2_frac = c.co2_frac;

    auto& st = colonist_[id];

    // Stage classification
    const BreathStage stage = classifyStage(out.PO2_kPa, out.CO2_frac);
    out.stage = stage;

    // Update reserve (positive in safe air, negative in bad air)
    const bool goodAir = (stage == BreathStage::OK) && (out.PO2_kPa >= cfg_.safe_PO2_kPa);
    if (goodAir) st.o2_reserve += cfg_.o2_recovery_per_s * dt;
    else         st.o2_reserve -= cfg_.o2_debt_per_s     * dt;
    st.o2_reserve = clampf(st.o2_reserve, -20.f, 60.f);

    // Damage & downed state
    float dmg = 0.f;
    switch (stage) {
        case BreathStage::OK:     break;
        case BreathStage::DIZZY:  dmg = cfg_.dizzy_damage_per_s   * dt; break;
        case BreathStage::DOWNED: dmg = cfg_.downed_damage_per_s  * dt; break;
        case BreathStage::DYING:  dmg = cfg_.dying_damage_per_s   * dt; break;
    }
    // Debt exaggerates damage a bit
    if (st.o2_reserve < 0) dmg *= (1.0f + clampf(-st.o2_reserve / 10.f, 0.f, 1.5f));

    if (dmg > 0.f && hooks.applyDamage) hooks.applyDamage(id, dmg, "asphyxia");

    // Downed toggle
    if (hooks.setDowned) {
        if ((stage >= BreathStage::DOWNED) && st.stage < BreathStage::DOWNED) hooks.setDowned(id, true);
        if ((stage <  BreathStage::DOWNED) && st.stage >= BreathStage::DOWNED) hooks.setDowned(id, false);
    }

    // Toasts
    if (hooks.toast) {
        if (stage == BreathStage::DIZZY && st.stage == BreathStage::OK)         hooks.toast("Colonist dizzy: low O₂");
        if (stage == BreathStage::DOWNED && st.stage < BreathStage::DOWNED)    hooks.toast("Colonist downed: severe hypoxia");
        if (stage == BreathStage::DYING && st.stage < BreathStage::DYING)      hooks.toast("Colonist dying: critical asphyxia");
        if (out.CO2_frac >= cfg_.high_CO2_frac && out.CO2_frac < cfg_.lethal_CO2_frac && st.stage == BreathStage::OK)
            hooks.toast("High CO₂ levels detected");
    }

    // Auto-evac
    maybeAutoEvac(id, idx, st, hooks, c, dt);

    st.stage = stage;
    out.unsafe = (stage != BreathStage::OK);
    return out;
}

void AtmosphereGameplayBridge::maybeAutoEvac(EntityId id, int hereIdx, ColonistBreathState& st,
                                             const BridgeHooks& hooks, const AtmosphereCell& here, float dt)
{
    if (!cfg_.auto_evac_enabled || !hooks.suggestMoveTo) return;

    const bool hereSafe = isBreathable(here);
    st.time_since_evac_s += dt;

    // Re-path only when needed and throttled.
    const bool need = !hereSafe || (st.stage >= BreathStage::DIZZY) || (st.o2_reserve < 2.0f);
    if (!need) return;
    if (st.time_since_evac_s < cfg_.evac_repath_interval) return;

    st.time_since_evac_s = 0.0f;

    // Local BFS outward for a breathable tile, limited radius.
    int goal = -1;

    // PATCH: remove int → uint8_t narrowing in fill value
    std::fill(tmpVisited_.begin(), tmpVisited_.end(), static_cast<uint8_t>(0));
    std::queue<std::pair<int,int>> q; // (idx, dist)
    q.emplace(hereIdx, 0);
    tmpVisited_[hereIdx] = static_cast<uint8_t>(1);

    while (!q.empty()) {
        const auto [i, dist] = q.front(); q.pop();
        const AtmosphereCell c = atm_.cellAt(i);

        if (isBreathable(c)) { goal = i; break; }
        if (dist >= cfg_.evac_search_radius) continue;

        const int x = i % W_;
        const int y = i / W_;
        for (int k = 0; k < 4; ++k) {
            const int nx = x + kDX[k];
            const int ny = y + kDY[k];
            if (!inBounds(nx, ny)) continue;

            const int ni = toIndex(nx, ny);
            if (tmpVisited_[ni]) continue;

            // **Upgrade**: expand only through passable tiles to avoid unreachable targets.
            const AtmosphereCell nc = atm_.cellAt(ni);
            if (!nc.passable) continue;

            tmpVisited_[ni] = static_cast<uint8_t>(1);
            q.emplace(ni, dist + 1);
        }
    }

    if (goal >= 0) hooks.suggestMoveTo(id, goal);
}
