#pragma once

#include "game/PrototypeGame.h"

#include "game/editor/Blueprint.h"
#include "game/editor/PlanHistory.h"
#include "game/save/SaveMeta.h"

#include "game/proto/ProtoWorld.h"
#include "input/InputMapper.h"
#include "loop/DebugCamera.h"
#include "platform/win32/Win32Debug.h"

#include <array>
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

    // Selection (Inspect tool)
    int selectedX = -1;
    int selectedY = -1;

    // Selected colonist (Inspect tool). -1 = none.
    int  selectedColonistId   = -1;
    bool followSelectedColonist = false;

    // Selection rectangle (Inspect + Shift + drag)
    bool selectRectActive = false;
    bool selectRectHas    = false;
    int  selectRectStartX = 0;
    int  selectRectStartY = 0;
    int  selectRectEndX   = 0;
    int  selectRectEndY   = 0;

    // Blueprint copy/paste options.
    bool blueprintCopyPlansOnly     = false; // if true, copies only active plans (ignores built)
    bool blueprintPasteIncludeEmpty = false; // if true, Empty cells erase plans

    enum class BlueprintAnchor : std::uint8_t { TopLeft = 0, Center = 1 };
    BlueprintAnchor blueprintAnchor = BlueprintAnchor::TopLeft;

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

    // Save browser state (uses small sidecar meta files to avoid parsing huge world JSON in UI).
    struct SaveBrowserEntry
    {
        enum class Kind : std::uint8_t { Slot = 0, Autosave };

        Kind kind = Kind::Slot;
        int index = 0; // slot number or autosave index

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
