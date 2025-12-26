#include "game/PrototypeGame_Impl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace colony::game {

void PrototypeGame::Impl::DrawUI() noexcept
{
#if defined(COLONY_WITH_IMGUI)
    drawUI();
#else
    (void)this;
#endif
}

#if defined(COLONY_WITH_IMGUI)

namespace {

[[nodiscard]] float clampf(float v, float lo, float hi) noexcept
{
    return std::max(lo, std::min(v, hi));
}

struct CanvasXform {
    ImVec2 p0{};
    ImVec2 p1{};
    ImVec2 center{};
    float  tilePx = 24.f;
};

[[nodiscard]] ImVec2 worldToScreen(const DebugCameraState& cam, const CanvasXform& cx, ImVec2 world) noexcept
{
    // cam.panX/panY represent the world position at the *canvas center*.
    const float sx = cx.center.x + (world.x - cam.panX) * cx.tilePx;
    const float sy = cx.center.y + (world.y - cam.panY) * cx.tilePx;
    return {sx, sy};
}

[[nodiscard]] ImVec2 screenToWorld(const DebugCameraState& cam, const CanvasXform& cx, ImVec2 screen) noexcept
{
    const float wx = cam.panX + (screen.x - cx.center.x) / cx.tilePx;
    const float wy = cam.panY + (screen.y - cx.center.y) / cx.tilePx;
    return {wx, wy};
}

[[nodiscard]] ImU32 tileFillColor(proto::TileType t) noexcept
{
    switch (t) {
    case proto::TileType::Empty: return IM_COL32(18, 18, 20, 255);
    case proto::TileType::Floor: return IM_COL32(70, 70, 80, 255);
    case proto::TileType::Wall: return IM_COL32(30, 30, 34, 255);
    case proto::TileType::Farm: return IM_COL32(40, 90, 40, 255);
    case proto::TileType::Stockpile: return IM_COL32(110, 80, 30, 255);
    }
    return IM_COL32(255, 0, 255, 255);
}

[[nodiscard]] ImU32 tilePlanColor(proto::TileType t) noexcept
{
    // Same palette, but semi-transparent.
    const ImU32 c = tileFillColor(t);
    return (c & 0x00FFFFFFu) | 0x88000000u;
}

} // namespace

void PrototypeGame::Impl::drawHelpWindow()
{
    if (!showHelp)
        return;

    ImGui::SetNextWindowSize({460, 280}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Help", &showHelp)) {
        ImGui::TextUnformatted("Prototype Controls");
        ImGui::Separator();

        ImGui::BulletText("Mouse wheel: zoom (over World)");
        ImGui::BulletText("Middle-drag OR Space + Left-drag: pan");
        ImGui::BulletText("Left-drag: paint current tool");
        ImGui::BulletText("Right-drag: erase plans");

        ImGui::Separator();
        ImGui::BulletText("1: Inspect");
        ImGui::BulletText("2: Floor");
        ImGui::BulletText("3: Wall");
        ImGui::BulletText("4: Farm");
        ImGui::BulletText("5: Stockpile");
        ImGui::BulletText("6: Erase");

        ImGui::Separator();
        ImGui::BulletText("F1: Toggle panels");
        ImGui::BulletText("F2: Toggle help");
        ImGui::BulletText("F3: Runtime hotkeys (popup)");
        ImGui::BulletText("R: Reset world");
        ImGui::BulletText("P: Pause simulation");
        ImGui::BulletText("F5: Reload input bindings");
    }
    ImGui::End();
}

void PrototypeGame::Impl::drawPanelsWindow()
{
    if (!showPanels)
        return;

    ImGui::SetNextWindowSize({360, 520}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Colony")) {
        auto& inv = world.inventory();

        ImGui::Text("Population: %d", static_cast<int>(world.colonists().size()));
        ImGui::Text("Wood: %d", inv.wood);
        ImGui::Text("Food: %.1f", inv.food);
        ImGui::Text("Built Farms: %d", world.builtCount(proto::TileType::Farm));

        ImGui::Separator();
        ImGui::Text("Plans Pending: %d", world.plannedCount());

        if (ImGui::Button("Clear Plans")) {
            world.clearAllPlans();
            setStatus("Plans cleared");
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset World")) {
            resetWorld();
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Build Tools");

        auto toolRadio = [&](Tool t, const char* label, proto::TileType tile) {
            const bool selected = (tool == t);
            if (ImGui::RadioButton(label, selected)) {
                tool = t;
            }

            if (tile != proto::TileType::Empty) {
                ImGui::SameLine();
                ImGui::TextDisabled("(wood %d, %.1fs)",
                                   proto::TileWoodCost(tile),
                                   proto::TileBuildTimeSeconds(tile));
            }
        };

        toolRadio(Tool::Inspect, "1  Inspect", proto::TileType::Empty);
        toolRadio(Tool::Floor, "2  Floor", proto::TileType::Floor);
        toolRadio(Tool::Wall, "3  Wall", proto::TileType::Wall);
        toolRadio(Tool::Farm, "4  Farm", proto::TileType::Farm);
        toolRadio(Tool::Stockpile, "5  Stockpile", proto::TileType::Stockpile);
        toolRadio(Tool::Erase, "6  Erase", proto::TileType::Empty);

        ImGui::Separator();
        ImGui::TextUnformatted("Simulation");
        ImGui::Checkbox("Paused (P)", &paused);
        ImGui::SliderFloat("Speed", &simSpeed, 0.1f, 4.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);

        ImGui::Separator();
        ImGui::TextUnformatted("Input Bindings");
        ImGui::Checkbox("Hot Reload", &bindingHotReloadEnabled);
        ImGui::SliderFloat("Poll Interval (s)", &bindingsPollInterval, 0.1f, 5.0f, "%.1f");

        if (!statusText.empty() && statusTtl > 0.f) {
            ImGui::Separator();
            ImGui::TextWrapped("%s", statusText.c_str());
        }
    }
    ImGui::End();
}

void PrototypeGame::Impl::drawWorldWindow()
{
    // First-run layout: attempt to occupy the available space.
    if (ImGuiViewport* vp = ImGui::GetMainViewport()) {
        ImGui::SetNextWindowPos(vp->WorkPos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(vp->WorkSize, ImGuiCond_FirstUseEver);
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!showPanels)
        flags |= ImGuiWindowFlags_NoCollapse;

    if (!ImGui::Begin("World", nullptr, flags)) {
        ImGui::End();
        return;
    }

    // Canvas
    const ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
    ImVec2 canvas_sz       = ImGui::GetContentRegionAvail();
    if (canvas_sz.x < 50.f)
        canvas_sz.x = 50.f;
    if (canvas_sz.y < 50.f)
        canvas_sz.y = 50.f;
    const ImVec2 canvas_p1 = {canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y};

    ImGui::InvisibleButton("world_canvas", canvas_sz,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);

    const bool canvas_hovered = ImGui::IsItemHovered();
    const bool canvas_active  = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(10, 10, 12, 255));
    dl->AddRect(canvas_p0, canvas_p1, IM_COL32(60, 60, 70, 255));

    const DebugCameraState& cam = camera.State();

    CanvasXform cx;
    cx.p0     = canvas_p0;
    cx.p1     = canvas_p1;
    cx.center = {canvas_p0.x + canvas_sz.x * 0.5f, canvas_p0.y + canvas_sz.y * 0.5f};
    cx.tilePx = 24.f * std::max(0.05f, cam.zoom);

    // Zoom around mouse cursor (only when hovering the canvas)
    if (canvas_hovered) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.f) {
            const float oldTilePx = cx.tilePx;
            const ImVec2 mouse    = ImGui::GetIO().MousePos;

            const ImVec2 worldBefore = screenToWorld(cam, cx, mouse);

            // Update zoom
            const int detents = static_cast<int>(wheel);
            (void)camera.ApplyWheelDetents(detents);

            const DebugCameraState& cam2 = camera.State();
            cx.tilePx                    = 24.f * std::max(0.05f, cam2.zoom);

            // Adjust pan to keep the world point under the mouse stable.
            if (cx.tilePx != oldTilePx) {
                const float newPanX = worldBefore.x - (mouse.x - cx.center.x) / cx.tilePx;
                const float newPanY = worldBefore.y - (mouse.y - cx.center.y) / cx.tilePx;
                (void)camera.ApplyPan(newPanX - cam2.panX, newPanY - cam2.panY);
            }
        }
    }

    // Pan
    if (canvas_hovered && canvas_active) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;

        const bool middleDrag = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        const bool spaceDrag  = ImGui::IsKeyDown(ImGuiKey_Space) && ImGui::IsMouseDown(ImGuiMouseButton_Left);

        if ((middleDrag || spaceDrag) && cx.tilePx > 0.f) {
            // "Grab" style: drag right -> world moves right
            const float wx = -d.x / cx.tilePx;
            const float wy = -d.y / cx.tilePx;
            (void)camera.ApplyPan(wx, wy);
        }
    }

    // Visible tile bounds
    const DebugCameraState& cam3 = camera.State();
    const ImVec2 worldMin        = screenToWorld(cam3, cx, canvas_p0);
    const ImVec2 worldMax        = screenToWorld(cam3, cx, canvas_p1);

    const int minX = std::max(0, static_cast<int>(std::floor(std::min(worldMin.x, worldMax.x))) - 1);
    const int minY = std::max(0, static_cast<int>(std::floor(std::min(worldMin.y, worldMax.y))) - 1);
    const int maxX = std::min(world.width() - 1, static_cast<int>(std::ceil(std::max(worldMin.x, worldMax.x))) + 1);
    const int maxY = std::min(world.height() - 1, static_cast<int>(std::ceil(std::max(worldMin.y, worldMax.y))) + 1);

    // Draw tiles
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const proto::Cell& c = world.cell(x, y);

            const ImVec2 tileCenter = worldToScreen(cam3, cx, {x + 0.5f, y + 0.5f});
            const ImVec2 half       = {cx.tilePx * 0.5f, cx.tilePx * 0.5f};
            const ImVec2 p0         = {tileCenter.x - half.x, tileCenter.y - half.y};
            const ImVec2 p1         = {tileCenter.x + half.x, tileCenter.y + half.y};

            // Built layer
            dl->AddRectFilled(p0, p1, tileFillColor(c.built));

            // Planned overlay
            if (c.planned != proto::TileType::Empty && c.planned != c.built) {
                dl->AddRectFilled(p0, p1, tilePlanColor(c.planned));
                dl->AddRect(p0, p1, IM_COL32(220, 220, 255, 140));

                // Progress bar if reserved
                if (c.reservedBy >= 0 && c.workRemaining > 0.f) {
                    const float denom = std::max(0.01f, proto::TileBuildTimeSeconds(c.planned));
                    const float t = clampf(1.0f - (c.workRemaining / denom), 0.f, 1.f);
                    const ImVec2 bar0 = {p0.x + 2.f, p1.y - 6.f};
                    const ImVec2 bar1 = {p0.x + 2.f + (cx.tilePx - 4.f) * t, p1.y - 2.f};
                    dl->AddRectFilled(bar0, bar1, IM_COL32(255, 255, 255, 160));
                }
            }

            // Optional grid lines when zoomed in
            if (cx.tilePx >= 10.f) {
                dl->AddRect(p0, p1, IM_COL32(0, 0, 0, 40));
            }
        }
    }

    // Draw colonists
    for (const proto::Colonist& c : world.colonists()) {
        const ImVec2 pos = worldToScreen(cam3, cx, {c.x, c.y});
        dl->AddCircleFilled(pos, std::max(2.f, cx.tilePx * 0.18f), IM_COL32(240, 240, 90, 255));

        if (c.hasJob) {
            const ImVec2 tgt = worldToScreen(cam3, cx, {c.targetX + 0.5f, c.targetY + 0.5f});
            dl->AddLine(pos, tgt, IM_COL32(240, 240, 90, 80), 1.0f);
        }
    }

    // Hover / interaction
    if (canvas_hovered) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const ImVec2 w     = screenToWorld(cam3, cx, mouse);
        const int tx       = static_cast<int>(std::floor(w.x));
        const int ty       = static_cast<int>(std::floor(w.y));

        if (world.inBounds(tx, ty)) {
            // Hover highlight
            const ImVec2 tileCenter = worldToScreen(cam3, cx, {tx + 0.5f, ty + 0.5f});
            const ImVec2 half       = {cx.tilePx * 0.5f, cx.tilePx * 0.5f};
            const ImVec2 p0         = {tileCenter.x - half.x, tileCenter.y - half.y};
            const ImVec2 p1         = {tileCenter.x + half.x, tileCenter.y + half.y};
            dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 120), 0.f, 0, 2.f);

            // Tooltip
            const proto::Cell& cell = world.cell(tx, ty);
            ImGui::BeginTooltip();
            ImGui::Text("(%d, %d)", tx, ty);
            ImGui::Text("Built: %s", proto::TileTypeName(cell.built));
            if (cell.planned != proto::TileType::Empty && cell.planned != cell.built)
                ImGui::Text("Plan:  %s", proto::TileTypeName(cell.planned));
            ImGui::EndTooltip();

            // Painting
            const bool spaceHeld = ImGui::IsKeyDown(ImGuiKey_Space);

            if (!spaceHeld) {
                // Left paint: place current tool (except Inspect)
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && tool != Tool::Inspect) {
                    if (tx != lastPaintX || ty != lastPaintY) {
                        const auto result = world.placePlan(tx, ty, toolTile());
                        if (result == proto::PlacePlanResult::NotEnoughWood) {
                            setStatus("Not enough wood");
                        }
                        lastPaintX = tx;
                        lastPaintY = ty;
                    }
                }

                // Right paint: erase plan
                if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                    if (tx != lastPaintX || ty != lastPaintY) {
                        (void)world.placePlan(tx, ty, proto::TileType::Empty);
                        lastPaintX = tx;
                        lastPaintY = ty;
                    }
                }

                // Inspect selection
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && tool == Tool::Inspect) {
                    setStatus(std::string("Selected ") + std::to_string(tx) + "," + std::to_string(ty));
                }
            }

            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                lastPaintX = std::numeric_limits<int>::min();
                lastPaintY = std::numeric_limits<int>::min();
            }
        }
    }

    // Minimal HUD (top-left)
    {
        const auto& inv = world.inventory();
        char buf[256]   = {};
        (void)std::snprintf(buf,
                            sizeof(buf),
                            "Tool: %s | Wood: %d | Food: %.1f | Plans: %d",
                            toolName(),
                            inv.wood,
                            inv.food,
                            world.plannedCount());
        dl->AddText({canvas_p0.x + 8.f, canvas_p0.y + 8.f}, IM_COL32(255, 255, 255, 200), buf);
    }

    ImGui::End();
}

void PrototypeGame::Impl::drawUI()
{
    drawWorldWindow();
    drawPanelsWindow();
    drawHelpWindow();
}

#endif // COLONY_WITH_IMGUI

} // namespace colony::game
