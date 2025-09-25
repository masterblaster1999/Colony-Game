#pragma once
// File: src/ui/HUDPanel.hpp
//
// Minimal Dear ImGui HUD for Colony-Game.
// Shows resource counts, colonist count, and time controls (Pause / 1x / 3x).
//
// Drop this file into src/ui/, include it where you render ImGui each frame,
// fill HUDViewModel from your game state, call DrawHUD(vm, actions),
// and apply actions to your simulation (see usage snippet in README above).

#include <imgui.h>
#include <cmath>    // std::fabsf
#include <cstddef>  // std::size_t

namespace colony { namespace ui {

// -----------------------------
// Data passed *into* the HUD
// -----------------------------

struct HUDResource {
    const char* name = nullptr;  // e.g., "Wood"
    int         amount = 0;      // e.g., 42
};

struct HUDViewModel {
    // Counts
    int colonistCount   = 0;

    // Resource array (pointer + size so you can feed stack arrays)
    const HUDResource* resources   = nullptr;
    int                resourceCount = 0;

    // Time state
    bool  paused     = false;
    float timeScale  = 1.0f;   // e.g., 1.0f or 3.0f
};

// -----------------------------
// Actions emitted by the HUD
// -----------------------------

struct HUDActions {
    bool  togglePause  = false;
    bool  setTimeScale = false;
    float newTimeScale = 1.0f;
};

// -----------------------------
// Helpers
// -----------------------------

inline bool approxEq(float a, float b, float eps = 0.01f) {
    return std::fabs(a - b) <= eps;
}

// Draw a compact label/value pair on one line.
inline void InlineStat(const char* label, int value, bool dimLabel = true) {
    if (dimLabel) ImGui::TextDisabled("%s", label);
    else          ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::Text("%d", value);
}

// Draw "A: 10 | B: 5 | C: 2" sequence from resource array.
inline void ResourceStrip(const HUDResource* res, int count) {
    if (!res || count <= 0) return;
    for (int i = 0; i < count; ++i) {
        if (i > 0) {
            ImGui::SameLine(0.0f, 12.0f);
            ImGui::TextDisabled("|");
            ImGui::SameLine(0.0f, 12.0f);
        }
        ImGui::Text("%s:", res[i].name ? res[i].name : "Resource");
        ImGui::SameLine();
        ImGui::Text("%d", res[i].amount);
    }
}

// -----------------------------
// Main HUD draw
// -----------------------------
//
// - Anchors a translucent top bar to the top of the screen
// - Left: resources   |  Colonists: N
// - Right: [Pause/Resume] [1x] [3x] with keyboard shortcuts
//
// Return: void (actions emitted through the HUDActions out param)

inline void DrawHUD(const HUDViewModel& vm, HUDActions& outActions)
{
    // === Window bootstrap ===
    ImGuiIO& io = ImGui::GetIO();

    // Full-width top bar; fixed position.
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, 0.0f), ImGuiCond_Always); // height auto
    ImGui::SetNextWindowBgAlpha(0.35f); // translucent

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoCollapse;

    ImGuiStyle& style = ImGui::GetStyle();

    // A bit of padding and no border for the HUD bar
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::Begin("HUD##TopBar", nullptr, flags))
    {
        // --- Left side: resources & colonists ---
        ResourceStrip(vm.resources, vm.resourceCount);

        // Colonist count (placed after resources with spacing)
        if (vm.resourceCount > 0) {
            ImGui::SameLine(0.0f, 16.0f);
            ImGui::TextDisabled("|");
            ImGui::SameLine(0.0f, 16.0f);
        }
        InlineStat("Colonists:", vm.colonistCount);

        // --- Right side: time controls ---
        // Compute the width needed by the three buttons so we can right-align them
        const char* pauseLabel  = vm.paused ? u8"Resume" : u8"Pause";
        const char* oneXLabel   = "1x";
        const char* threeXLabel = "3x";

        const float wPause = ImGui::CalcTextSize(pauseLabel).x + style.FramePadding.x * 2.0f;
        const float w1x    = ImGui::CalcTextSize(oneXLabel).x + style.FramePadding.x * 2.0f;
        const float w3x    = ImGui::CalcTextSize(threeXLabel).x + style.FramePadding.x * 2.0f;
        const float wSp    = style.ItemSpacing.x * 2.0f; // two gaps between three buttons
        const float controlsW = wPause + w1x + w3x + wSp;

        // Move cursor to the right edge (inside content region)
        float regionMaxX = ImGui::GetWindowContentRegionMax().x; // local space
        float rightX     = regionMaxX - controlsW;
        float oldX       = ImGui::GetCursorPosX();
        if (rightX > oldX) ImGui::SetCursorPosX(rightX);

        // Pause / Resume button
        if (ImGui::Button(pauseLabel)) {
            outActions.togglePause = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Shortcut: Space");

        ImGui::SameLine();

        // 1x button (disabled if already 1x and not paused)
        bool active1x = (!vm.paused && approxEq(vm.timeScale, 1.0f));
        ImGui::BeginDisabled(active1x);
        if (ImGui::Button(oneXLabel)) {
            outActions.setTimeScale = true;
            outActions.newTimeScale = 1.0f;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Shortcut: 1");

        ImGui::SameLine();

        // 3x button (disabled if already 3x and not paused)
        bool active3x = (!vm.paused && approxEq(vm.timeScale, 3.0f));
        ImGui::BeginDisabled(active3x);
        if (ImGui::Button(threeXLabel)) {
            outActions.setTimeScale = true;
            outActions.newTimeScale = 3.0f;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Shortcut: 3");

        // --- Keyboard shortcuts (handled anywhere on the frame) ---
        // These do not require the HUD bar to be focused.
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
            outActions.togglePause = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_1, false)) {
            outActions.setTimeScale = true;
            outActions.newTimeScale = 1.0f;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_3, false)) {
            outActions.setTimeScale = true;
            outActions.newTimeScale = 3.0f;
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(); // WindowBorderSize
    ImGui::PopStyleVar(); // WindowPadding
}

}} // namespace colony::ui
