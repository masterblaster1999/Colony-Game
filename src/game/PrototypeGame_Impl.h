#pragma once

#include "game/PrototypeGame.h"

#include "game/proto/ProtoWorld.h"
#include "input/InputMapper.h"
#include "loop/DebugCamera.h"
#include "platform/win32/Win32Debug.h"

#include <array>
#include <filesystem>
#include <limits>
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

struct PrototypeGame::Impl {
    colony::input::InputMapper input;
    DebugCameraController camera;
    proto::World world;

    enum class Tool : std::uint8_t {
        Inspect = 0,
        Floor,
        Wall,
        Farm,
        Stockpile,
        Erase,
    };

    Tool tool = Tool::Floor;

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

    [[nodiscard]] proto::TileType toolTile() const noexcept;
    [[nodiscard]] const char* toolName() const noexcept;

    void setStatus(std::string text, float ttlSeconds = 2.5f);

    // Bindings
    bool loadBindings();
    void pollBindingHotReload(float dtSeconds);

    // Simulation
    void resetWorld();

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
