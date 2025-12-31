#pragma once

#include "game/PrototypeGame.h"

#include "game/editor/Blueprint.h"
#include "game/editor/BlueprintLibrary.h"
#include "game/editor/PlanHistory.h"
#include "game/save/SaveMeta.h"

#include "game/util/NotificationLog.h"

#include "game/proto/ProtoWorld.h"
#include "input/InputMapper.h"
#include "loop/DebugCamera.h"
#include "platform/win32/Win32Debug.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if defined(COLONY_WITH_IMGUI)
    #include <imgui.h>
#endif

namespace colony::game {

namespace fs = std::filesystem;

// DebugCamera types live in colony::appwin. The game code uses them heavily, so
// we import them here for readability in implementation files.
using colony::appwin::DebugCameraController;
using colony::appwin::DebugCameraState;

// Background save worker (prototype). Defined in PrototypeGame_SaveLoad.cpp.
struct AsyncSaveManager;

struct AsyncSaveManagerDeleter {
    void operator()(AsyncSaveManager* p) const noexcept;
};

using AsyncSaveManagerPtr = std::unique_ptr<AsyncSaveManager, AsyncSaveManagerDeleter>;

struct PrototypeGame::Impl {
    colony::input::InputMapper input;
    DebugCameraController camera;
    proto::World world;

    // Plan placement undo/redo.
    colony::game::editor::PlanHistory planHistory;

    // Copy/paste-able plan blueprint (Inspect selection -> Blueprint tool).
    colony::game::editor::PlanBlueprint blueprint;

    enum class Tool : std::uint8_t {
        Inspect = 0,
        Floor,
        Wall,
        Door,
        Bed,
        Farm,
        Stockpile,
        Demolish,
        Erase,
        Priority,  // edits priority on existing plans (does not place new plans)
        Blueprint, // stamps a copied plan blueprint
    };

    Tool tool = Tool::Floor;

    // Plan placement tuning.
    //  - 0..3 (displayed as 1..4)
    int  planBrushPriority  = 1;
    bool showPlanPriorities = false;

    // When enabled, batch plan placement (Shift-rect + blueprint stamp) is transactional:
    // it either fully applies or does nothing (if insufficient wood).
    bool atomicPlanPlacement = false;

    // Rooms / indoors overlay
    bool showRoomsOverlay = false;
    bool roomsOverlayIndoorsOnly = true;

    bool showRoomIds      = false;
    bool showRoomIdsIndoorsOnly = true;

    // Room selection (for inspector/overlay)
    int  selectedRoomId = -1;
    bool showSelectedRoomOutline = true;

    // Selection (Inspect tool)
    int selectedX = -1;
    int selectedY = -1;

    // Selected colonists (Inspect tool).
    //
    // - selectedColonistIds: multi-selection set (unique, sorted for stable UI)
    // - selectedColonistId:  primary selection (used for Follow + manual order queue UI)
    //
    // Selection UX (implemented in UI):
    //  - Left-click selects a single colonist
    //  - Ctrl+Left-click toggles colonists in/out of the selection
    //  - Move orders apply to all selected colonists
    //  - Build/Harvest orders apply to the primary selection only
    std::vector<int> selectedColonistIds{};
    int  selectedColonistId   = -1;
    bool followSelectedColonist = false;

    [[nodiscard]] bool isColonistInSelection(int id) const noexcept
    {
        for (const int v : selectedColonistIds)
            if (v == id)
                return true;
        return false;
    }

    void normalizeColonistSelection() noexcept
    {
        if (selectedColonistIds.empty())
        {
            selectedColonistId = -1;
            followSelectedColonist = false;
            return;
        }

        std::sort(selectedColonistIds.begin(), selectedColonistIds.end());
        selectedColonistIds.erase(std::unique(selectedColonistIds.begin(), selectedColonistIds.end()),
                                  selectedColonistIds.end());

        if (selectedColonistId < 0 || !isColonistInSelection(selectedColonistId))
            selectedColonistId = selectedColonistIds.front();
    }

    void clearColonistSelection() noexcept
    {
        selectedColonistIds.clear();
        selectedColonistId = -1;
        followSelectedColonist = false;
    }

    void selectColonistExclusive(int id) noexcept
    {
        selectedColonistIds.clear();
        if (id >= 0)
            selectedColonistIds.push_back(id);
        selectedColonistId = id;
        if (id < 0)
            followSelectedColonist = false;
        normalizeColonistSelection();
    }

    void addColonistToSelection(int id, bool makePrimary) noexcept
    {
        if (id < 0)
            return;

        if (!isColonistInSelection(id))
            selectedColonistIds.push_back(id);

        if (makePrimary)
            selectedColonistId = id;

        normalizeColonistSelection();
    }

    void removeColonistFromSelection(int id) noexcept
    {
        if (selectedColonistIds.empty())
            return;

        selectedColonistIds.erase(std::remove(selectedColonistIds.begin(), selectedColonistIds.end(), id),
                                  selectedColonistIds.end());

        if (selectedColonistId == id)
            selectedColonistId = -1;

        normalizeColonistSelection();
    }

    void toggleColonistSelection(int id, bool makePrimaryIfAdding) noexcept
    {
        if (id < 0)
            return;

        if (isColonistInSelection(id))
            removeColonistFromSelection(id);
        else
            addColonistToSelection(id, makePrimaryIfAdding);
    }

    // Selection rectangle (Inspect + Shift + drag)
    bool selectRectActive = false;
    bool selectRectHas    = false;
    int  selectRectStartX = 0;
    int  selectRectStartY = 0;
    int  selectRectEndX   = 0;
    int  selectRectEndY   = 0;

    // Blueprint copy/paste options.
    bool blueprintCopyPlansOnly         = false; // if true, copies only active plans (ignores built)
    bool blueprintCopyTrimEmptyBorders  = false; // if true, trims empty rows/cols when copying a selection
    bool blueprintPasteIncludeEmpty     = false; // if true, Empty cells erase plans

    enum class BlueprintAnchor : std::uint8_t { TopLeft = 0, Center = 1 };
    BlueprintAnchor blueprintAnchor = BlueprintAnchor::TopLeft;

    // Blueprint library (disk) - keeps a small, user-managed collection of reusable plan blueprints.
    std::array<char, 64> blueprintSaveNameBuf{};
    bool blueprintSaveOverwrite = false;
    bool blueprintLibraryDirty  = true;
    int  blueprintLibrarySelected = -1;
    std::vector<colony::game::editor::BlueprintFileInfo> blueprintLibraryFiles;
    colony::game::editor::PlanBlueprint blueprintLibraryPreview;
    std::string blueprintLibraryPreviewName;
    std::string blueprintLibraryLastError;


    // Minimap
    bool  showMinimap         = true;
    int   minimapSizePx       = 200;
    bool  minimapShowPlans    = true;
    bool  minimapShowColonists = true;
    bool  minimapShowViewport = true;
    float lastWorldCanvasW    = 0.f;
    float lastWorldCanvasH    = 0.f;

    bool showPanels = true;
    bool showHelp   = false;

    // Simulation
    bool   paused          = false;
    float  simSpeed        = 1.f;
    double simAccumulator  = 0.0;
    double fixedDt         = 1.0 / 60.0;
    int    maxCatchupSteps = 8;

    // UI feedback
    std::string statusText;
    float       statusTtl = 0.f;

    // -----------------------------------------------------------------
    // Notifications + alerts (prototype)
    // -----------------------------------------------------------------
    colony::game::util::NotificationLog notify;

    bool  alertsEnabled              = true;
    bool  alertsShowToasts           = true;
    bool  alertsShowResolveMessages  = true;
    bool  alertsAutoPauseOnCritical  = false;
    float alertsCheckIntervalSeconds = 1.0f;

    int   alertsLowWoodThreshold     = 10;
    float alertsLowFoodThreshold     = 5.0f;
    float alertsStarvingThreshold    = 0.25f; // personalFood
    float alertsToastSecondsInfo     = 2.5f;
    float alertsToastSecondsWarning  = 3.0f;
    float alertsToastSecondsError    = 4.0f;

    float alertsAccumSeconds = 0.0f;

    struct AlertState
    {
        bool lowWood        = false;
        bool lowFood        = false;
        bool noStockpiles   = false;

        bool noBuilders     = false;
        bool noFarmers      = false;
        bool noHaulers      = false;

        bool criticalStarving = false;
    };

    AlertState alertState{};

    void logMessage(colony::game::util::NotifySeverity sev,
                    std::string text,
                    colony::game::util::NotifyTarget target = colony::game::util::NotifyTarget::None()) noexcept
    {
        notify.push(std::move(text), sev, playtimeSeconds, /*toastTtlSeconds=*/0.0f, target, /*pushToast=*/false);
    }

    void pushNotification(colony::game::util::NotifySeverity sev,
                          std::string text,
                          float toastTtlSeconds,
                          colony::game::util::NotifyTarget target = colony::game::util::NotifyTarget::None()) noexcept
    {
        const float toast = (alertsShowToasts ? std::max(0.0f, toastTtlSeconds) : 0.0f);
        notify.push(std::move(text), sev, playtimeSeconds, toast, target, /*pushToast=*/alertsShowToasts);
    }

    void pushNotificationAutoToast(colony::game::util::NotifySeverity sev,
                                   std::string text,
                                   colony::game::util::NotifyTarget target = colony::game::util::NotifyTarget::None()) noexcept
    {
        float ttl = alertsToastSecondsInfo;
        switch (sev)
        {
        case colony::game::util::NotifySeverity::Info: ttl = alertsToastSecondsInfo; break;
        case colony::game::util::NotifySeverity::Warning: ttl = alertsToastSecondsWarning; break;
        case colony::game::util::NotifySeverity::Error: ttl = alertsToastSecondsError; break;
        }
        pushNotification(sev, std::move(text), ttl, target);
    }

    void focusNotificationTarget(const colony::game::util::NotifyTarget& t) noexcept
    {
        const DebugCameraState& s = camera.State();

        switch (t.kind)
        {
        case colony::game::util::NotifyTarget::Kind::Tile:
            (void)camera.ApplyPan((static_cast<float>(t.tileX) + 0.5f) - s.panX,
                                  (static_cast<float>(t.tileY) + 0.5f) - s.panY);
            break;
        case colony::game::util::NotifyTarget::Kind::WorldPos:
            (void)camera.ApplyPan(t.worldX - s.panX, t.worldY - s.panY);
            break;
        case colony::game::util::NotifyTarget::Kind::Colonist:
            for (const auto& c : world.colonists())
            {
                if (c.id != t.colonistId)
                    continue;
                (void)camera.ApplyPan(c.x - s.panX, c.y - s.panY);
                break;
            }
            break;
        default:
            break;
        }
    }

    // Simple paint state (avoid re-placing on the same tile every frame while dragging).
    int lastPaintX = std::numeric_limits<int>::min();
    int lastPaintY = std::numeric_limits<int>::min();

    // Rectangle paint (Shift + drag) state.
    // This allows quickly placing/erasing a box of plans in one gesture.
    bool rectPaintActive = false;
    bool rectPaintErase  = false;
    int  rectPaintStartX = 0;
    int  rectPaintStartY = 0;
    int  rectPaintEndX   = 0;
    int  rectPaintEndY   = 0;

    // Debug drawing / UX toggles.
    bool showBrushPreview = true;
    bool showJobPaths     = false;
    bool showReservations = false;

    // World reset parameters (editable from UI).
    int worldResetW = 64;
    int worldResetH = 64;
    std::uint32_t worldResetSeed = 0xC0FFEEu;
    bool worldResetUseRandomSeed = true;

    // Persistence (prototype)
    int saveSlot = 0;

    // Autosave (prototype)
    bool  autosaveEnabled         = true;
    float autosaveIntervalSeconds = 300.f;
    int   autosaveKeepCount       = 5;
    float autosaveAccumSeconds    = 0.f;

    // Async save worker (keeps autosaves/manual saves from hitching the frame).
    AsyncSaveManagerPtr saveMgr;
    double playtimeSeconds = 0.0; // real-time seconds since launch (for save metadata)

    void updateAlerts(float dtSeconds) noexcept;

    // Save browser state (uses small sidecar meta files to avoid parsing huge world JSON in UI).
    struct SaveBrowserEntry
    {
        enum class Kind : std::uint8_t { Slot = 0, Autosave, Named };

        Kind kind = Kind::Slot;
        int index = 0; // slot number or autosave index (Named uses -1)

        // For Kind::Named only: friendly display name (typically filename stem).
        std::string displayName;

        fs::path path;
        fs::path metaPath;

        bool exists     = false;
        bool metaExists = false;
        bool metaOk     = false;

        std::uintmax_t sizeBytes = 0;

        // Best-effort timestamp for list sorting/display.
        // Prefer meta's savedUnixSecondsUtc; fall back to file's last_write_time.
        std::int64_t displayUnixSecondsUtc = 0;
        bool         timeFromMeta          = false;

        save::SaveSummary summary{};
        std::string metaError;
    };

    std::vector<SaveBrowserEntry> saveBrowserEntries;
    int   saveBrowserSelected          = -1;
    int   saveBrowserPendingDelete     = -1;
    float saveBrowserPendingDeleteTtl  = 0.f;
    bool  saveBrowserDirty             = true;

    // Named/manual saves (prototype)
    std::array<char, 64> namedSaveNameBuf{};
    bool namedSaveOverwrite = false;

    // Save browser UX state
    std::array<char, 64> saveBrowserFilterBuf{};
    int  saveBrowserSortMode = 0; // 0=Kind, 1=Time (newest), 2=Name
    bool saveBrowserShowSlots    = true;
    bool saveBrowserShowAutosaves = true;
    bool saveBrowserShowNamed    = true;

    int  saveBrowserLastSelected = -1;
    std::array<char, 64> saveBrowserRenameBuf{};
    bool saveBrowserRenameOverwrite = false;

    int  saveBrowserCopyToSlot = 0;
    bool saveBrowserCopyOverwrite = true;
    std::array<char, 64> saveBrowserCopyNameBuf{};
    bool saveBrowserCopyNameOverwrite = false;


    // Input binding hot reload
    bool                        bindingHotReloadEnabled = false;
    float                       bindingsPollAccum       = 0.f;
    float                       bindingsPollInterval    = 1.f;
    std::vector<std::pair<fs::path, fs::file_time_type>> bindingCandidates;

    // Path of the last successfully loaded bindings file (empty if using defaults).
    fs::path bindingsLoadedPath;

#if defined(COLONY_WITH_IMGUI)
    // Minimal bindings editor UI state.
    bool showBindingsEditor = false;
    bool bindingsEditorInit = false;
    fs::path bindingsEditorTargetPath;
    std::string bindingsEditorMessage;
    float bindingsEditorMessageTtl = 0.f;
    std::array<std::string, static_cast<std::size_t>(colony::input::Action::Count)> bindingsEditorText;

    // Optional quality-of-life: capture a chord by pressing keys/mouse instead of typing tokens.
    bool bindingsEditorCaptureActive = false;
    bool bindingsEditorCaptureAppend = false;
    int  bindingsEditorCaptureAction = -1; // Action enum index
    std::bitset<colony::input::kInputCodeCount> bindingsEditorCaptureDown{};
    std::vector<std::uint32_t> bindingsEditorCaptureCodes;
#endif

    Impl();

    // NOTE: Impl owns a std::unique_ptr<AsyncSaveManager> where AsyncSaveManager
    // is defined in PrototypeGame_SaveLoad.cpp.
    //
    // A std::unique_ptr to an incomplete type is fine *as a member*, but the
    // destructor must be out-of-line in a .cpp that sees the complete type.
    // Otherwise MSVC rightfully errors with "can't delete an incomplete type".
    ~Impl();

    [[nodiscard]] proto::TileType toolTile() const noexcept;
    [[nodiscard]] const char* toolName() const noexcept;

    void setStatus(std::string text, float ttlSeconds = 2.5f);

    // Bindings
    bool loadBindings();
    void pollBindingHotReload(float dtSeconds);

    // Simulation
    void resetWorld();

    // Persistence (prototype)
    [[nodiscard]] fs::path defaultWorldSavePath() const;
    [[nodiscard]] fs::path worldSaveDir() const;
    [[nodiscard]] fs::path blueprintDir() const;
    [[nodiscard]] fs::path worldSavePathForSlot(int slot) const;
    [[nodiscard]] fs::path autosavePathForIndex(int index) const;

    bool saveWorldToPath(const fs::path& path, bool showStatus);
    bool loadWorldFromPath(const fs::path& path, bool showStatus);
    bool autosaveWorld();

    bool saveWorld();
    bool loadWorld();

    // Save worker integration.
    void pollAsyncSaves() noexcept;
    void invalidatePendingAutosaves() noexcept;

    // Plan editing history
    bool undoPlans();
    bool redoPlans();
    void clearPlanHistory() noexcept;

    // Modules
    bool OnInput(std::span<const colony::input::InputEvent> events,
                 bool uiWantsKeyboard,
                 bool uiWantsMouse) noexcept;

    bool Update(float dtSeconds, bool uiWantsKeyboard, bool uiWantsMouse) noexcept;
    void DrawUI() noexcept;

    // Camera (keyboard)
    bool updateCameraKeyboard(float dtSeconds, bool uiWantsKeyboard) noexcept;

#if defined(COLONY_WITH_IMGUI)
    void drawHelpWindow();
    void drawPanelsWindow();
    void drawBindingsEditorWindow();
    void drawWorldWindow();
    void drawUI();
#endif
};

} // namespace colony::game
