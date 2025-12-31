#include "game/PrototypeGame_Impl.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cmath>

namespace colony::game {

namespace {

[[nodiscard]] float clampf(float v, float lo, float hi) noexcept
{
    return std::max(lo, std::min(v, hi));
}

[[nodiscard]] std::uint32_t MakeSeed() noexcept
{
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return static_cast<std::uint32_t>(now);
}

} // namespace

void PrototypeGame::Impl::updateAlerts(float dtSeconds) noexcept
{
    if (!alertsEnabled)
        return;

    // Sanity clamps (edited live via UI).
    alertsCheckIntervalSeconds = std::clamp(alertsCheckIntervalSeconds, 0.1f, 10.0f);
    alertsLowWoodThreshold     = std::max(0, alertsLowWoodThreshold);
    alertsLowFoodThreshold     = std::max(0.0f, alertsLowFoodThreshold);
    alertsStarvingThreshold    = std::clamp(alertsStarvingThreshold, 0.0f, 10.0f);

    alertsAccumSeconds += dtSeconds;
    if (alertsAccumSeconds < alertsCheckIntervalSeconds)
        return;
    alertsAccumSeconds = 0.0f;

    const auto& inv = world.inventory();

    // ------------------------------------------------------------
    // Low resources
    // ------------------------------------------------------------
    const bool lowWoodNow = (alertsLowWoodThreshold > 0) && (inv.wood <= alertsLowWoodThreshold);
    if (lowWoodNow && !alertState.lowWood)
    {
        char buf[128] = {};
        (void)std::snprintf(buf, sizeof(buf), "Low wood: %d (<= %d)", inv.wood, alertsLowWoodThreshold);
        pushNotificationAutoToast(util::NotifySeverity::Warning, buf);
    }
    else if (!lowWoodNow && alertState.lowWood && alertsShowResolveMessages)
    {
        char buf[128] = {};
        (void)std::snprintf(buf, sizeof(buf), "Wood recovered: %d", inv.wood);
        pushNotification(util::NotifySeverity::Info, buf, /*toastTtlSeconds=*/1.5f);
    }
    alertState.lowWood = lowWoodNow;

    const bool lowFoodNow = (alertsLowFoodThreshold > 0.0f) && (inv.food <= alertsLowFoodThreshold);
    if (lowFoodNow && !alertState.lowFood)
    {
        char buf[128] = {};
        (void)std::snprintf(buf, sizeof(buf), "Low food: %.1f (<= %.1f)", inv.food, alertsLowFoodThreshold);
        pushNotificationAutoToast(util::NotifySeverity::Warning, buf);
    }
    else if (!lowFoodNow && alertState.lowFood && alertsShowResolveMessages)
    {
        char buf[128] = {};
        (void)std::snprintf(buf, sizeof(buf), "Food recovered: %.1f", inv.food);
        pushNotification(util::NotifySeverity::Info, buf, /*toastTtlSeconds=*/1.5f);
    }
    alertState.lowFood = lowFoodNow;

    // ------------------------------------------------------------
    // Logistics
    // ------------------------------------------------------------
    const bool noStockpilesNow = (world.looseWoodTotal() > 0) && (world.builtCount(proto::TileType::Stockpile) == 0);
    if (noStockpilesNow && !alertState.noStockpiles)
    {
        pushNotificationAutoToast(util::NotifySeverity::Warning,
                                  "Loose wood exists but there are no stockpiles. Build a stockpile to enable hauling.");
    }
    else if (!noStockpilesNow && alertState.noStockpiles && alertsShowResolveMessages)
    {
        pushNotification(util::NotifySeverity::Info, "Stockpiles available.", /*toastTtlSeconds=*/1.5f);
    }
    alertState.noStockpiles = noStockpilesNow;

    // ------------------------------------------------------------
    // Workforce capability checks
    // ------------------------------------------------------------
    int buildCapable = 0;
    int farmCapable  = 0;
    int haulCapable  = 0;
    int buildEnabled = 0;
    int farmEnabled  = 0;
    int haulEnabled  = 0;
    for (const auto& c : world.colonists())
    {
        const auto caps = c.role.caps();
        const bool canBuild = HasAny(caps, Capability::Building);
        const bool canFarm  = HasAny(caps, Capability::Farming);
        const bool canHaul  = HasAny(caps, Capability::Hauling);
        if (canBuild) ++buildCapable;
        if (canFarm)  ++farmCapable;
        if (canHaul)  ++haulCapable;
        if (canBuild && c.workPrio.build > 0) ++buildEnabled;
        if (canFarm  && c.workPrio.farm  > 0) ++farmEnabled;
        if (canHaul  && c.workPrio.haul  > 0) ++haulEnabled;
    }

    const bool needBuilders = (world.plannedCount() > 0);
    const bool noBuildersNow = needBuilders && ((buildCapable == 0) || (buildEnabled == 0));
    if (noBuildersNow && !alertState.noBuilders)
    {
        if (buildCapable == 0)
            pushNotificationAutoToast(util::NotifySeverity::Warning, "Plans exist but no colonists can Build (role/capabilities)." );
        else
            pushNotificationAutoToast(util::NotifySeverity::Warning, "Plans exist but Building work is disabled by priorities (all Off)." );
    }
    else if (!noBuildersNow && alertState.noBuilders && alertsShowResolveMessages)
    {
        pushNotification(util::NotifySeverity::Info, "Builders available again.", /*toastTtlSeconds=*/1.5f);
    }
    alertState.noBuilders = noBuildersNow;

    const bool needFarmers = (world.harvestableFarmCount() > 0);
    const bool noFarmersNow = needFarmers && ((farmCapable == 0) || (farmEnabled == 0));
    if (noFarmersNow && !alertState.noFarmers)
    {
        if (farmCapable == 0)
            pushNotificationAutoToast(util::NotifySeverity::Warning, "Harvests are ready but no colonists can Farm (role/capabilities)." );
        else
            pushNotificationAutoToast(util::NotifySeverity::Warning, "Harvests are ready but Farming work is disabled by priorities (all Off)." );
    }
    else if (!noFarmersNow && alertState.noFarmers && alertsShowResolveMessages)
    {
        pushNotification(util::NotifySeverity::Info, "Farmers available again.", /*toastTtlSeconds=*/1.5f);
    }
    alertState.noFarmers = noFarmersNow;

    const bool needHaulers = (world.looseWoodTotal() > 0);
    const bool noHaulersNow = needHaulers && ((haulCapable == 0) || (haulEnabled == 0));
    if (noHaulersNow && !alertState.noHaulers)
    {
        if (haulCapable == 0)
            pushNotificationAutoToast(util::NotifySeverity::Warning, "Loose wood exists but no colonists can Haul (role/capabilities)." );
        else
            pushNotificationAutoToast(util::NotifySeverity::Warning, "Loose wood exists but Hauling work is disabled by priorities (all Off)." );
    }
    else if (!noHaulersNow && alertState.noHaulers && alertsShowResolveMessages)
    {
        pushNotification(util::NotifySeverity::Info, "Haulers available again.", /*toastTtlSeconds=*/1.5f);
    }
    alertState.noHaulers = noHaulersNow;

    // ------------------------------------------------------------
    // Critical: starvation when the colony has no food left
    // ------------------------------------------------------------
    int starvingCount = 0;
    int starvingColonistId = -1;
    for (const auto& c : world.colonists())
    {
        if (c.personalFood <= alertsStarvingThreshold)
        {
            ++starvingCount;
            if (starvingColonistId < 0)
                starvingColonistId = c.id;
        }
    }

    const bool criticalStarvingNow = (inv.food <= 0.0f) && (starvingCount > 0);
    if (criticalStarvingNow && !alertState.criticalStarving)
    {
        char buf[160] = {};
        (void)std::snprintf(buf, sizeof(buf), "STARVATION: %d colonist(s) at or below %.2f personal food, and colony food is 0.",
                            starvingCount,
                            static_cast<double>(alertsStarvingThreshold));

        const util::NotifyTarget tgt = (starvingColonistId >= 0)
            ? util::NotifyTarget::Colonist(starvingColonistId)
            : util::NotifyTarget::None();

        pushNotificationAutoToast(util::NotifySeverity::Error, buf, tgt);

        if (alertsAutoPauseOnCritical && !paused)
        {
            paused = true;
            setStatus("Paused: critical starvation alert", 3.0f);
        }
    }
    else if (!criticalStarvingNow && alertState.criticalStarving && alertsShowResolveMessages)
    {
        pushNotification(util::NotifySeverity::Info, "Starvation resolved.", /*toastTtlSeconds=*/2.0f);
    }
    alertState.criticalStarving = criticalStarvingNow;
}

PrototypeGame::Impl::Impl()
    : world(64, 64, 0xC0FFEEu)
{
    // Load bindings at startup (logs errors but doesn't hard-fail).
    (void)loadBindings();

    // Center the camera on the world.
    const float cx = std::max(0.0f, static_cast<float>(world.width()) * 0.5f);
    const float cy = std::max(0.0f, static_cast<float>(world.height()) * 0.5f);
    (void)camera.ApplyPan(cx, cy);
    (void)camera.ApplyZoomFactor(1.0f);
}

void PrototypeGame::Impl::resetWorld()
{
    // A reset replaces world state; don't allow an old queued autosave to write after this.
    invalidatePendingAutosaves();

    const std::uint32_t seed = worldResetUseRandomSeed ? MakeSeed() : worldResetSeed;
    worldResetSeed = seed;
    world.reset(worldResetW, worldResetH, seed);

    clearPlanHistory();

    // Clear selection state (tile + colonists).
    selectedX = -1;
    selectedY = -1;
    clearColonistSelection();
    selectedRoomId = -1;

    // Recenter camera.
    const DebugCameraState& s = camera.State();
    const float cx            = std::max(0.0f, static_cast<float>(world.width()) * 0.5f);
    const float cy            = std::max(0.0f, static_cast<float>(world.height()) * 0.5f);
    (void)camera.ApplyPan(cx - s.panX, cy - s.panY);

    simAccumulator = 0.0;
    paused         = false;
    simSpeed       = 1.f;

    setStatus("World reset", 2.0f);
}

bool PrototypeGame::Impl::Update(float dtSeconds, bool uiWantsKeyboard, bool /*uiWantsMouse*/) noexcept
{
    if (!std::isfinite(dtSeconds) || dtSeconds <= 0.f)
        return false;

    dtSeconds = clampf(dtSeconds, 0.f, 0.25f);

    // Track real-time playtime for save metadata.
    playtimeSeconds += static_cast<double>(dtSeconds);

    // Advance toast timers (real-time).
    notify.tick(dtSeconds);

    // Auto status fade
    if (statusTtl > 0.f) {
        statusTtl = std::max(0.f, statusTtl - dtSeconds);
        if (statusTtl == 0.f)
            statusText.clear();
    }

    // Background save completions (update status UI if needed).
    pollAsyncSaves();

    // Hot reload input bindings
    pollBindingHotReload(dtSeconds);

    // Keyboard camera pan/zoom
    bool cameraChanged = updateCameraKeyboard(dtSeconds, uiWantsKeyboard);

    // Simulation (fixed-step)
    if (!paused) {
        const double scaled = static_cast<double>(dtSeconds) * static_cast<double>(simSpeed);
        simAccumulator += scaled;

        int steps = 0;
        while (simAccumulator >= fixedDt && steps < maxCatchupSteps) {
            world.tick(fixedDt);
            simAccumulator -= fixedDt;
            ++steps;
        }

        if (steps == maxCatchupSteps && simAccumulator >= fixedDt) {
            // Drop extra time if we fell behind.
            simAccumulator = std::fmod(simAccumulator, fixedDt);
        }
    }

    // Optional camera follow (selection-driven).
    if (followSelectedColonist && selectedColonistId >= 0)
    {
        for (const proto::Colonist& c : world.colonists())
        {
            if (c.id != selectedColonistId)
                continue;

            const DebugCameraState& s = camera.State();
            cameraChanged |= camera.ApplyPan(c.x - s.panX, c.y - s.panY);
            break;
        }
    }

    // Autosave is based on real time (not simulation-scaled time).
    if (autosaveEnabled && autosaveIntervalSeconds > 0.f)
    {
        autosaveAccumSeconds += dtSeconds;
        if (autosaveAccumSeconds >= autosaveIntervalSeconds)
        {
            autosaveAccumSeconds = 0.f;
            (void)autosaveWorld();
        }
    }

    // Alert evaluation is based on real time and current world state.
    updateAlerts(dtSeconds);

    return cameraChanged;
}

} // namespace colony::game
