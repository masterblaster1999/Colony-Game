#include "game/PrototypeGame_Impl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace colony::game {

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

using PlanSnapshot = colony::game::editor::PlanHistory::TileSnapshot;

[[nodiscard]] PlanSnapshot snapshotFromCell(const proto::Cell& c) noexcept
{
    PlanSnapshot s;
    s.planned = c.planned;
    s.planPriority = c.planPriority;
    s.workRemaining = c.workRemaining;
    return s;
}

} // namespace

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

    // Zoom around mouse cursor (only when hovering the canvas).
    //
    // NOTE: Zoom is driven through bindable input actions so the mouse wheel
    // can be re-mapped on Windows (WheelUp/WheelDown by default).
    if (canvas_hovered) {
        int detents = 0;
        for (const auto& ae : input.ActionEvents()) {
            if (ae.type != colony::input::ActionEventType::Pressed)
                continue;
            if (ae.action == colony::input::Action::CameraZoomIn)
                ++detents;
            else if (ae.action == colony::input::Action::CameraZoomOut)
                --detents;
        }

        if (detents != 0) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const ImVec2 before = screenToWorld(cam, cx, mouse);

            const float zoomFactorPerDetent = 1.1f;
            float newZoom = cam.zoom;
            if (detents > 0) {
                for (int i = 0; i < detents; ++i)
                    newZoom *= zoomFactorPerDetent;
            } else {
                for (int i = 0; i < -detents; ++i)
                    newZoom /= zoomFactorPerDetent;
            }
            newZoom = clampf(newZoom, 0.05f, 20.f);

            // Apply zoom.
            camera.SetZoom(newZoom);

            // Adjust pan so that the world under the cursor stays under the cursor.
            const DebugCameraState& cam2 = camera.State();
            CanvasXform cx2 = cx;
            cx2.tilePx = 24.f * std::max(0.05f, cam2.zoom);
            const ImVec2 after = screenToWorld(cam2, cx2, mouse);
            (void)camera.ApplyPan(before.x - after.x, before.y - after.y);
        }
    }

    // Pan (middle-drag or Space + Left-drag)
    if (canvas_active) {
        const bool middleDrag = ImGui::IsMouseDragging(ImGuiMouseButton_Middle);
        const bool spaceDrag  = ImGui::IsKeyDown(ImGuiKey_Space) && ImGui::IsMouseDragging(ImGuiMouseButton_Left);

        const ImVec2 d = ImGui::GetIO().MouseDelta;
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

                // Show plan priority (debug/overlay).
                const bool showPriority = showPlanPriorities && cx.tilePx >= 18.f;
                if (showPriority)
                {
                    char buf[16] = {};
                    (void)std::snprintf(buf, sizeof(buf), "P%d", static_cast<int>(c.planPriority) + 1);
                    dl->AddText({p0.x + 3.f, p0.y + 2.f}, IM_COL32(255, 255, 255, 200), buf);
                }

                // Debug: show reservation owner.
                if (showReservations && c.reservedBy >= 0 && cx.tilePx >= 18.f) {
                    char buf[16] = {};
                    (void)std::snprintf(buf, sizeof(buf), "%d", c.reservedBy);
                    const float yOff = showPriority ? 14.f : 2.f;
                    dl->AddText({p0.x + 3.f, p0.y + yOff}, IM_COL32(255, 255, 255, 200), buf);
                }
            }

            // Selection highlight.
            if (x == selectedX && y == selectedY)
                dl->AddRect(p0, p1, IM_COL32(255, 240, 120, 220), 0.f, 0, 3.f);

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

            // Optional: draw the path the colonist is following.
            if (showJobPaths && !c.path.empty())
            {
                ImVec2 prev = pos;
                for (std::size_t i = c.pathIndex; i < c.path.size(); ++i)
                {
                    const auto& p = c.path[i];
                    const ImVec2 pt = worldToScreen(cam3, cx, {p.x + 0.5f, p.y + 0.5f});
                    dl->AddLine(prev, pt, IM_COL32(240, 240, 90, 110), 1.0f);
                    prev = pt;
                }
            }
        }
    }

    // Helper: apply a rectangle of plans in one shot.
    auto applyPlanRect = [&](int x0, int y0, int x1, int y1, proto::TileType plan, bool reportNotEnoughWood) {
        const int rx0 = std::min(x0, x1);
        const int ry0 = std::min(y0, y1);
        const int rx1 = std::max(x0, x1);
        const int ry1 = std::max(y0, y1);

        // One undoable command.
        if (planHistory.HasActiveCommand())
            (void)planHistory.CommitCommand(world.inventory().wood);
        planHistory.BeginCommand(world.inventory().wood);

        const std::uint8_t priority = (plan == proto::TileType::Empty)
            ? static_cast<std::uint8_t>(0)
            : static_cast<std::uint8_t>(std::max(0, std::min(3, planBrushPriority)));

        int changed = 0;
        int attempted = 0;
        bool notEnough = false;

        for (int yy = ry0; yy <= ry1; ++yy) {
            for (int xx = rx0; xx <= rx1; ++xx) {
                if (!world.inBounds(xx, yy))
                    continue;
                ++attempted;
                const proto::Cell& beforeC = world.cell(xx, yy);
                const PlanSnapshot before = snapshotFromCell(beforeC);

                const auto r = world.placePlan(xx, yy, plan, priority);
                if (r == proto::PlacePlanResult::Ok)
                {
                    ++changed;
                    const proto::Cell& afterC = world.cell(xx, yy);
                    const PlanSnapshot after = snapshotFromCell(afterC);
                    planHistory.RecordChange(xx, yy, before, after);
                }
                else if (r == proto::PlacePlanResult::NotEnoughWood) {
                    notEnough = true;
                    // Remaining tiles will also fail for positive-cost placements.
                    if (reportNotEnoughWood)
                        goto done;
                }
            }
        }

    done:
        if (attempted == 0) {
            planHistory.CancelCommand();
            return;
        }

        const bool committed = planHistory.CommitCommand(world.inventory().wood);
        if (committed)
            world.CancelAllJobsAndClearReservations();

        if (notEnough && reportNotEnoughWood) {
            setStatus("Not enough wood (" + std::to_string(changed) + "/" + std::to_string(attempted) + ")");
        } else {
            if (plan != proto::TileType::Empty)
                setStatus("Placed " + std::to_string(changed) + " tiles (P" + std::to_string(static_cast<int>(priority) + 1) + ")");
            else
                setStatus("Erased " + std::to_string(changed) + " plans");
        }
    };

    // Helper: apply a rectangle of *priority changes* to existing active plans.
    // This does not change plan types or costs and is fully undoable.
    auto applyPriorityRect = [&](int x0, int y0, int x1, int y1) {
        const int rx0 = std::min(x0, x1);
        const int ry0 = std::min(y0, y1);
        const int rx1 = std::max(x0, x1);
        const int ry1 = std::max(y0, y1);

        if (planHistory.HasActiveCommand())
            (void)planHistory.CommitCommand(world.inventory().wood);
        planHistory.BeginCommand(world.inventory().wood);

        const std::uint8_t priority = static_cast<std::uint8_t>(std::max(0, std::min(3, planBrushPriority)));

        int changed = 0;
        int touched = 0;
        for (int yy = ry0; yy <= ry1; ++yy) {
            for (int xx = rx0; xx <= rx1; ++xx) {
                if (!world.inBounds(xx, yy))
                    continue;
                ++touched;

                const proto::Cell& beforeC = world.cell(xx, yy);
                if (beforeC.planned == proto::TileType::Empty || beforeC.planned == beforeC.built)
                    continue;
                if (beforeC.planPriority == priority)
                    continue;

                const PlanSnapshot before = snapshotFromCell(beforeC);
                const auto r = world.placePlan(xx, yy, beforeC.planned, priority);
                if (r == proto::PlacePlanResult::Ok)
                {
                    const proto::Cell& afterC = world.cell(xx, yy);
                    const PlanSnapshot after = snapshotFromCell(afterC);
                    planHistory.RecordChange(xx, yy, before, after);
                    ++changed;
                }
            }
        }

        if (touched == 0) {
            planHistory.CancelCommand();
            return;
        }

        const bool committed = planHistory.CommitCommand(world.inventory().wood);
        if (committed)
            world.CancelAllJobsAndClearReservations();

        setStatus("Priority paint: " + std::to_string(changed) + " changes (P" + std::to_string(static_cast<int>(priority) + 1) + ")");
    };

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

            // Interaction modifiers
            const bool spaceHeld = ImGui::IsKeyDown(ImGuiKey_Space);
            const bool shiftHeld = ImGui::GetIO().KeyShift;

            // Brush preview overlay (does not commit any plans).
            if (showBrushPreview && !spaceHeld && !rectPaintActive && tool != Tool::Inspect)
            {
                if (tool == Tool::Priority)
                {
                    dl->AddRectFilled(p0, p1, IM_COL32(120, 150, 220, 35));
                    dl->AddRect(p0, p1, IM_COL32(220, 220, 255, 180));

                    if (cx.tilePx >= 18.f)
                    {
                        const int pr = std::clamp(planBrushPriority, 0, 3);
                        char buf[16] = {};
                        (void)std::snprintf(buf, sizeof(buf), "P%d", pr + 1);
                        dl->AddText({p0.x + 3.f, p0.y + 2.f}, IM_COL32(255, 255, 255, 210), buf);
                    }
                }
                else
                {
                    const proto::TileType previewPlan = toolTile();
                    if (previewPlan == proto::TileType::Empty) {
                        dl->AddRectFilled(p0, p1, IM_COL32(220, 80, 80, 35));
                        dl->AddRect(p0, p1, IM_COL32(220, 80, 80, 160));
                    } else {
                        dl->AddRectFilled(p0, p1, tilePlanColor(previewPlan));
                        dl->AddRect(p0, p1, IM_COL32(220, 220, 255, 160));
                    }
                }
            }

            // Tooltip
            const proto::Cell& cell = world.cell(tx, ty);
            ImGui::BeginTooltip();
            ImGui::Text("(%d, %d)", tx, ty);
            ImGui::Text("Built: %s", proto::TileTypeName(cell.built));
            if (cell.planned != proto::TileType::Empty && cell.planned != cell.built)
            {
                ImGui::Text("Plan:  %s", proto::TileTypeName(cell.planned));
                ImGui::Text("Priority: P%d", static_cast<int>(cell.planPriority) + 1);
                if (cell.reservedBy >= 0)
                    ImGui::Text("Reserved: %d", cell.reservedBy);
            }
            ImGui::EndTooltip();



            if (!spaceHeld)
            {
                // --- Rectangle paint mode (Shift + drag) ---
                if (rectPaintActive)
                {
                    // Track end point while the cursor is in-bounds.
                    rectPaintEndX = tx;
                    rectPaintEndY = ty;

                    const ImGuiMouseButton btn = rectPaintErase ? ImGuiMouseButton_Right : ImGuiMouseButton_Left;
                    if (!ImGui::IsMouseDown(btn))
                    {
                        // Apply on release.
                        if (rectPaintErase)
                        {
                            applyPlanRect(rectPaintStartX, rectPaintStartY, rectPaintEndX, rectPaintEndY, proto::TileType::Empty, /*reportNotEnoughWood*/ false);
                        }
                        else if (tool == Tool::Priority)
                        {
                            applyPriorityRect(rectPaintStartX, rectPaintStartY, rectPaintEndX, rectPaintEndY);
                        }
                        else
                        {
                            const proto::TileType plan = toolTile();
                            applyPlanRect(rectPaintStartX, rectPaintStartY, rectPaintEndX, rectPaintEndY, plan, /*reportNotEnoughWood*/ true);
                        }

                        rectPaintActive = false;
                    }
                }
                else if (shiftHeld)
                {
                    // Shift is held: start rectangle if clicked.
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && tool != Tool::Inspect)
                    {
                        rectPaintActive = true;
                        rectPaintErase  = false;
                        rectPaintStartX = tx;
                        rectPaintStartY = ty;
                        rectPaintEndX   = tx;
                        rectPaintEndY   = ty;
                        lastPaintX      = std::numeric_limits<int>::min();
                        lastPaintY      = std::numeric_limits<int>::min();
                    }
                    else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                    {
                        rectPaintActive = true;
                        rectPaintErase  = true;
                        rectPaintStartX = tx;
                        rectPaintStartY = ty;
                        rectPaintEndX   = tx;
                        rectPaintEndY   = ty;
                        lastPaintX      = std::numeric_limits<int>::min();
                        lastPaintY      = std::numeric_limits<int>::min();
                    }

                    // Allow inspect clicks even with Shift held.
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && tool == Tool::Inspect)
                    {
                        selectedX = tx;
                        selectedY = ty;
                        setStatus(std::string("Selected ") + std::to_string(tx) + "," + std::to_string(ty));
                    }
                }
                else
                {
                    // --- Single-tile paint mode ---

                    auto tryApplySingle = [&](int x, int y, proto::TileType plan, bool reportNotEnoughWood) {
                        if (x == lastPaintX && y == lastPaintY)
                            return;
                        if (!world.inBounds(x, y))
                            return;

                        const int woodBefore = world.inventory().wood;
                        const proto::Cell& beforeC = world.cell(x, y);
                        const PlanSnapshot before = snapshotFromCell(beforeC);

                        const std::uint8_t priority = (plan == proto::TileType::Empty)
                            ? static_cast<std::uint8_t>(0)
                            : static_cast<std::uint8_t>(std::max(0, std::min(3, planBrushPriority)));

                        const auto result = world.placePlan(x, y, plan, priority);
                        if (result == proto::PlacePlanResult::Ok)
                        {
                            if (!planHistory.HasActiveCommand())
                                planHistory.BeginCommand(woodBefore);

                            const proto::Cell& afterC = world.cell(x, y);
                            const PlanSnapshot after = snapshotFromCell(afterC);
                            planHistory.RecordChange(x, y, before, after);
                        }
                        else if (result == proto::PlacePlanResult::NotEnoughWood && reportNotEnoughWood)
                        {
                            setStatus("Not enough wood");
                        }

                        lastPaintX = x;
                        lastPaintY = y;
                    };

                    auto tryApplyPrioritySingle = [&](int x, int y) {
                        if (x == lastPaintX && y == lastPaintY)
                            return;
                        if (!world.inBounds(x, y))
                            return;

                        const proto::Cell& beforeC = world.cell(x, y);
                        if (beforeC.planned == proto::TileType::Empty || beforeC.planned == beforeC.built)
                        {
                            // Nothing to change.
                            lastPaintX = x;
                            lastPaintY = y;
                            return;
                        }

                        const std::uint8_t priority = static_cast<std::uint8_t>(std::max(0, std::min(3, planBrushPriority)));
                        if (beforeC.planPriority == priority)
                        {
                            lastPaintX = x;
                            lastPaintY = y;
                            return;
                        }

                        const int woodBefore = world.inventory().wood;
                        const PlanSnapshot before = snapshotFromCell(beforeC);

                        const auto result = world.placePlan(x, y, beforeC.planned, priority);
                        if (result == proto::PlacePlanResult::Ok)
                        {
                            if (!planHistory.HasActiveCommand())
                                planHistory.BeginCommand(woodBefore);

                            const proto::Cell& afterC = world.cell(x, y);
                            const PlanSnapshot after = snapshotFromCell(afterC);
                            planHistory.RecordChange(x, y, before, after);
                        }

                        lastPaintX = x;
                        lastPaintY = y;
                    };

                    // Left paint: place current tool (except Inspect)
                    if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && tool != Tool::Inspect) {
                        if (tool == Tool::Priority)
                            tryApplyPrioritySingle(tx, ty);
                        else
                            tryApplySingle(tx, ty, toolTile(), /*reportNotEnoughWood=*/ true);
                    }

                    // Right paint: erase plan
                    if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                        tryApplySingle(tx, ty, proto::TileType::Empty, /*reportNotEnoughWood=*/ false);
                    }

                    // Inspect selection
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && tool == Tool::Inspect) {
                        selectedX = tx;
                        selectedY = ty;
                        setStatus(std::string("Selected ") + std::to_string(tx) + "," + std::to_string(ty));
                    }
                }
            }

            // Reset per-drag single-tile caching when the mouse is up.
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                lastPaintX = std::numeric_limits<int>::min();
                lastPaintY = std::numeric_limits<int>::min();
            }
        }
    }

    // Finalize single-tile drag strokes even if the mouse was released outside the canvas.
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        if (planHistory.HasActiveCommand())
        {
            const bool committed = planHistory.CommitCommand(world.inventory().wood);
            if (committed)
                world.CancelAllJobsAndClearReservations();
        }
    }

    // Finalize rectangle paint even if the mouse was released outside the canvas.
    if (rectPaintActive)
    {
        const ImGuiMouseButton btn = rectPaintErase ? ImGuiMouseButton_Right : ImGuiMouseButton_Left;
        if (!ImGui::IsMouseDown(btn))
        {
            if (rectPaintErase)
            {
                applyPlanRect(rectPaintStartX, rectPaintStartY,
                              rectPaintEndX, rectPaintEndY,
                              proto::TileType::Empty,
                              /*reportNotEnoughWood*/ false);
            }
            else if (tool == Tool::Priority)
            {
                applyPriorityRect(rectPaintStartX, rectPaintStartY, rectPaintEndX, rectPaintEndY);
            }
            else
            {
                const proto::TileType plan = toolTile();
                applyPlanRect(rectPaintStartX, rectPaintStartY,
                              rectPaintEndX, rectPaintEndY,
                              plan,
                              /*reportNotEnoughWood*/ true);
            }
            rectPaintActive = false;
        }
    }

    // Draw rectangle paint preview overlay (even if the cursor is no longer hovered).
    if (rectPaintActive)
    {
        const int rx0 = std::min(rectPaintStartX, rectPaintEndX);
        const int ry0 = std::min(rectPaintStartY, rectPaintEndY);
        const int rx1 = std::max(rectPaintStartX, rectPaintEndX);
        const int ry1 = std::max(rectPaintStartY, rectPaintEndY);

        const ImVec2 p0 = worldToScreen(cam3, cx, {static_cast<float>(rx0), static_cast<float>(ry0)});
        const ImVec2 p1 = worldToScreen(cam3, cx, {static_cast<float>(rx1 + 1), static_cast<float>(ry1 + 1)});

        const bool isPriorityRect = (!rectPaintErase && tool == Tool::Priority);
        const proto::TileType plan = rectPaintErase ? proto::TileType::Empty : toolTile();

        ImU32 fillCol = 0;
        ImU32 borderCol = 0;
        if (isPriorityRect)
        {
            fillCol   = IM_COL32(120, 150, 220, 35);
            borderCol = IM_COL32(220, 220, 255, 180);
        }
        else if (plan == proto::TileType::Empty) {
            fillCol   = IM_COL32(220, 80, 80, 40);
            borderCol = IM_COL32(220, 80, 80, 180);
        } else {
            fillCol   = tilePlanColor(plan);
            borderCol = IM_COL32(220, 220, 255, 180);
        }

        dl->AddRectFilled(p0, p1, fillCol);
        dl->AddRect(p0, p1, borderCol, 0.f, 0, 2.0f);
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

    // Status overlay (also visible when panels are hidden).
    if (!statusText.empty() && statusTtl > 0.f) {
        float a = 1.0f;
        if (statusTtl < 0.5f)
            a = clampf(statusTtl / 0.5f, 0.f, 1.f);

        const ImU32 textCol = IM_COL32(255, 255, 255, static_cast<int>(200 * a));
        const ImU32 bgCol   = IM_COL32(0, 0, 0, static_cast<int>(140 * a));

        const ImVec2 pos = {canvas_p0.x + 8.f, canvas_p0.y + 28.f};
        const ImVec2 sz  = ImGui::CalcTextSize(statusText.c_str());

        dl->AddRectFilled({pos.x - 4.f, pos.y - 2.f},
                          {pos.x + sz.x + 4.f, pos.y + sz.y + 2.f},
                          bgCol,
                          4.0f);
        dl->AddText(pos, textCol, statusText.c_str());
    }

    ImGui::End();
}

#endif // COLONY_WITH_IMGUI

} // namespace colony::game
