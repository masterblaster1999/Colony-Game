#include "game/PrototypeGame_Impl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <vector>

namespace colony::game {

#if defined(COLONY_WITH_IMGUI)

namespace {

[[nodiscard]] float clampf(float v, float lo, float hi) noexcept
{
    return std::max(lo, std::min(v, hi));
}

struct IntTile {
    int x = 0;
    int y = 0;
};

[[nodiscard]] std::vector<IntTile> gatherMoveDestinations(const proto::World& world,
                                                          int targetX, int targetY,
                                                          int count) noexcept
{
    std::vector<IntTile> out;
    if (count <= 0)
        return out;

    const auto& nav = world.nav();
    if (!world.inBounds(targetX, targetY) || !nav.passable(targetX, targetY))
        return out;

    const int w = world.width();
    const int h = world.height();

    out.reserve(static_cast<std::size_t>(count));

    std::vector<std::uint8_t> visited(static_cast<std::size_t>(w * h), 0);
    const auto toIdx = [w](int x, int y) noexcept -> std::size_t {
        return static_cast<std::size_t>(y * w + x);
    };

    std::vector<IntTile> q;
    q.reserve(256);
    q.push_back({targetX, targetY});
    visited[toIdx(targetX, targetY)] = 1;

    static constexpr int dirs[8][2] = {
        { 1,  0}, {-1,  0}, { 0,  1}, { 0, -1},
        { 1,  1}, { 1, -1}, {-1,  1}, {-1, -1},
    };

    std::size_t head = 0;
    while (head < q.size() && static_cast<int>(out.size()) < count)
    {
        const IntTile p = q[head++];

        // All nodes in the queue are passable.
        out.push_back(p);

        for (const auto& d : dirs)
        {
            const int dx = d[0];
            const int dy = d[1];
            if (!nav.can_step(p.x, p.y, dx, dy))
                continue;

            const int nx = p.x + dx;
            const int ny = p.y + dy;

            const std::size_t i = toIdx(nx, ny);
            if (visited[i])
                continue;
            visited[i] = 1;

            q.push_back({nx, ny});
        }
    }

    return out;
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
    case proto::TileType::Door: return IM_COL32(145, 110, 55, 255);
    case proto::TileType::Tree: return IM_COL32(25, 115, 25, 255);
    case proto::TileType::Remove: return IM_COL32(160, 60, 60, 255);
    }
    return IM_COL32(255, 0, 255, 255);
}

[[nodiscard]] ImU32 tilePlanColor(proto::TileType t) noexcept
{
    // Same palette, but semi-transparent.
    const ImU32 c = tileFillColor(t);
    return (c & 0x00FFFFFFu) | 0x88000000u;
}

[[nodiscard]] ImU32 roomOverlayColor(int roomId, bool indoors) noexcept
{
    // Deterministic pseudo-random color from room id.
    std::uint32_t h = static_cast<std::uint32_t>(roomId) * 2654435761u;

    const int r = 60 + static_cast<int>((h >> 0) & 0x7F);
    const int g = 60 + static_cast<int>((h >> 8) & 0x7F);
    const int b = 60 + static_cast<int>((h >> 16) & 0x7F);
    const int a = indoors ? 60 : 25;

    return IM_COL32(r, g, b, a);
}

[[nodiscard]] ImU32 roomOverlayColorSelected(int roomId, bool indoors) noexcept
{
    // Same deterministic palette as roomOverlayColor(), but with a stronger alpha
    // so the selected room stands out.
    std::uint32_t h = static_cast<std::uint32_t>(roomId) * 2654435761u;

    const int r = 60 + static_cast<int>((h >> 0) & 0x7F);
    const int g = 60 + static_cast<int>((h >> 8) & 0x7F);
    const int b = 60 + static_cast<int>((h >> 16) & 0x7F);
    const int a = indoors ? 110 : 70;

    return IM_COL32(r, g, b, a);
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

    // Remember the world canvas size for the minimap viewport indicator.
    lastWorldCanvasW = canvas_sz.x;
    lastWorldCanvasH = canvas_sz.y;
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
    cx.tilePx = 24.f * std::max(DebugCameraController::kMinZoom, cam.zoom);

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
            newZoom = clampf(newZoom, DebugCameraController::kMinZoom, DebugCameraController::kMaxZoom);

            // Apply zoom.
            camera.SetZoom(newZoom);

            // Adjust pan so that the world under the cursor stays under the cursor.
            const DebugCameraState& cam2 = camera.State();
            CanvasXform cx2 = cx;
            cx2.tilePx = 24.f * std::max(DebugCameraController::kMinZoom, cam2.zoom);
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

            // Farm growth overlay (subtle progress bar)
            if (c.built == proto::TileType::Farm && cx.tilePx >= 10.f)
            {
                const float g   = clampf(c.farmGrowth, 0.f, 1.f);
                const float pad = std::max(1.0f, cx.tilePx * 0.08f);
                const float barH = std::max(2.0f, cx.tilePx * 0.18f);

                const ImVec2 bg0 = {p0.x + pad, p1.y - pad - barH};
                const ImVec2 bg1 = {p1.x - pad, p1.y - pad};
                const float  fillW = std::max(0.0f, (bg1.x - bg0.x) * g);
                const ImVec2 fg1 = {bg0.x + fillW, bg1.y};

                dl->AddRectFilled(bg0, bg1, IM_COL32(0, 0, 0, 80));
                dl->AddRectFilled(bg0, fg1, IM_COL32(230, 230, 140, 170));

                if (g >= 0.999f)
                    dl->AddRect(p0, p1, IM_COL32(255, 245, 170, 150), 0.f, 0, 2.f);
            }

            // Rooms overlay
if (showRoomsOverlay && cx.tilePx >= 8.f)
{
    const int rid = world.roomIdAt(x, y);
    const proto::World::RoomInfo* ri = world.roomInfoById(rid);
    if (ri)
    {
        if (!roomsOverlayIndoorsOnly || ri->indoors)
        {
            const bool selectedRoom = (selectedRoomId >= 0) && (ri->id == selectedRoomId);
            const ImU32 col = selectedRoom
                ? roomOverlayColorSelected(ri->id, ri->indoors)
                : roomOverlayColor(ri->id, ri->indoors);

            dl->AddRectFilled(p0, p1, col);
        }
    }
}

            // Planned overlay
            if (c.planned != proto::TileType::Empty && c.planned != c.built) {
                if (c.planned == proto::TileType::Remove)
                {
                    // Demolish plan: red overlay + cross.
                    // If the target is a Tree, tint it more "choppy" than "destructive".
                    const bool choppingTree = (c.built == proto::TileType::Tree);

                    const ImU32 fill = choppingTree ? IM_COL32(120, 95, 50, 70) : IM_COL32(220, 80, 80, 70);
                    const ImU32 edge = choppingTree ? IM_COL32(140, 110, 60, 170) : IM_COL32(220, 80, 80, 170);
                    const ImU32 line = choppingTree ? IM_COL32(255, 255, 255, 160) : IM_COL32(255, 255, 255, 140);

                    dl->AddRectFilled(p0, p1, fill);
                    dl->AddRect(p0, p1, edge);
                    dl->AddLine(p0, p1, line, 1.5f);
                    dl->AddLine({p0.x, p1.y}, {p1.x, p0.y}, line, 1.5f);
                }
                else
                {
                    dl->AddRectFilled(p0, p1, tilePlanColor(c.planned));
                    dl->AddRect(p0, p1, IM_COL32(220, 220, 255, 140));
                }

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

    // Room labels
if (showRoomIds && cx.tilePx >= 18.f)
{
    for (int rid = 0; rid < world.roomCount(); ++rid)
    {
        const proto::World::RoomInfo* ri = world.roomInfoById(rid);
        if (!ri)
            continue;

        if (showRoomIdsIndoorsOnly && !ri->indoors)
            continue;

        const float wx = (static_cast<float>(ri->minX + ri->maxX) + 1.0f) * 0.5f;
        const float wy = (static_cast<float>(ri->minY + ri->maxY) + 1.0f) * 0.5f;
        const ImVec2 pos = worldToScreen(cam3, cx, {wx, wy});

        char buf[16] = {};
        (void)std::snprintf(buf, sizeof(buf), "R%d", ri->id);

        const ImU32 col = (selectedRoomId >= 0 && ri->id == selectedRoomId)
            ? IM_COL32(255, 245, 170, 230)
            : IM_COL32(255, 255, 255, 200);

        dl->AddText({pos.x - 8.f, pos.y - 6.f}, col, buf);
    }
}

// Selected room outline (tile-edge border)
if (showSelectedRoomOutline && selectedRoomId >= 0 && cx.tilePx >= 6.f)
{
    const ImU32 col = IM_COL32(255, 245, 170, 220);
    const float thick = (cx.tilePx >= 18.f) ? 3.f : 2.f;

    for (int y = minY; y <= maxY; ++y)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            if (!world.inBounds(x, y))
                continue;

            if (world.roomIdAt(x, y) != selectedRoomId)
                continue;

            const ImVec2 tileCenter = worldToScreen(cam3, cx, {x + 0.5f, y + 0.5f});
            const ImVec2 half       = {cx.tilePx * 0.5f, cx.tilePx * 0.5f};
            const ImVec2 p0         = {tileCenter.x - half.x, tileCenter.y - half.y};
            const ImVec2 p1         = {tileCenter.x + half.x, tileCenter.y + half.y};

            // Left edge
            if (!world.inBounds(x - 1, y) || world.roomIdAt(x - 1, y) != selectedRoomId)
                dl->AddLine({p0.x, p0.y}, {p0.x, p1.y}, col, thick);

            // Right edge
            if (!world.inBounds(x + 1, y) || world.roomIdAt(x + 1, y) != selectedRoomId)
                dl->AddLine({p1.x, p0.y}, {p1.x, p1.y}, col, thick);

            // Top edge
            if (!world.inBounds(x, y - 1) || world.roomIdAt(x, y - 1) != selectedRoomId)
                dl->AddLine({p0.x, p0.y}, {p1.x, p0.y}, col, thick);

            // Bottom edge
            if (!world.inBounds(x, y + 1) || world.roomIdAt(x, y + 1) != selectedRoomId)
                dl->AddLine({p0.x, p1.y}, {p1.x, p1.y}, col, thick);
        }
    }
}

    // Draw colonists
    for (const proto::Colonist& c : world.colonists()) {
        const ImVec2 pos = worldToScreen(cam3, cx, {c.x, c.y});
        const float  r   = std::max(2.f, cx.tilePx * 0.18f);

        ImU32 bodyCol = IM_COL32(240, 240, 90, 255);
        if (!c.hasJob)
            bodyCol = IM_COL32(220, 220, 220, 255);
        else if (c.jobKind == proto::Colonist::JobKind::Eat)
            bodyCol = IM_COL32(120, 240, 120, 255);
        else if (c.jobKind == proto::Colonist::JobKind::Harvest)
            bodyCol = IM_COL32(90, 200, 240, 255);
        else if (c.jobKind == proto::Colonist::JobKind::HaulWood)
            bodyCol = IM_COL32(210, 170, 100, 255);
        else if (c.jobKind == proto::Colonist::JobKind::ManualMove)
            bodyCol = IM_COL32(200, 120, 240, 255);

        dl->AddCircleFilled(pos, r, bodyCol);

        // Draft / selection outlines.
        if (c.drafted)
            dl->AddCircle(pos, r + 2.f, IM_COL32(220, 80, 80, 220), 0, 2.0f);

        if (isColonistInSelection(c.id))
        {
            const float extra = (c.id == selectedColonistId) ? 4.f : 3.f;
            const float thick = (c.id == selectedColonistId) ? 3.0f : 2.0f;
            dl->AddCircle(pos, r + extra, IM_COL32(255, 240, 120, 240), 0, thick);
        }

        // Role label (v7+)
        if (cx.tilePx >= 18.f)
        {
            const char* rn = RoleDefOf(c.role.role).name;
            const char ab = (rn && rn[0]) ? rn[0] : '?';
            const unsigned lvl = static_cast<unsigned>(std::max<std::uint16_t>(1, c.role.level));
            char buf[16] = {};
            (void)std::snprintf(buf, sizeof(buf), "%c%u", ab, lvl);
            dl->AddText({pos.x - r, pos.y + r + 1.0f}, IM_COL32(255, 255, 255, 200), buf);
        }

        // Hunger bar (v3+ save format)
        const float maxFood = static_cast<float>(std::max(0.0, world.colonistMaxPersonalFood));
        if (maxFood > 0.0f)
        {
            const float frac = clampf(c.personalFood / maxFood, 0.0f, 1.0f);

            const float barW = std::max(10.f, r * 2.f);
            const float barH = std::max(2.f, cx.tilePx * 0.04f);
            const ImVec2 b0 = {pos.x - barW * 0.5f, pos.y - r - barH - 2.0f};
            const ImVec2 b1 = {b0.x + barW, b0.y + barH};

            const int rc = static_cast<int>(255.f * (1.0f - frac));
            const int gc = static_cast<int>(255.f * frac);
            const ImU32 fillCol = IM_COL32(rc, gc, 60, 220);

            dl->AddRectFilled(b0, b1, IM_COL32(0, 0, 0, 160));
            dl->AddRectFilled(b0, {b0.x + barW * frac, b1.y}, fillCol);
            if (cx.tilePx >= 12.f)
                dl->AddRect(b0, b1, IM_COL32(0, 0, 0, 120));
        }

        if (c.hasJob) {
            const ImVec2 tgt = worldToScreen(cam3, cx, {c.targetX + 0.5f, c.targetY + 0.5f});
            const ImU32 lineCol = (c.jobKind == proto::Colonist::JobKind::Eat)
                ? IM_COL32(120, 240, 120, 80)
                : (c.jobKind == proto::Colonist::JobKind::Harvest)
                    ? IM_COL32(90, 200, 240, 80)
                    : (c.jobKind == proto::Colonist::JobKind::ManualMove)
                        ? IM_COL32(200, 120, 240, 80)
                    : IM_COL32(240, 240, 90, 80);
            const ImU32 pathCol = (c.jobKind == proto::Colonist::JobKind::Eat)
                ? IM_COL32(120, 240, 120, 110)
                : (c.jobKind == proto::Colonist::JobKind::Harvest)
                    ? IM_COL32(90, 200, 240, 110)
                    : (c.jobKind == proto::Colonist::JobKind::ManualMove)
                        ? IM_COL32(200, 120, 240, 110)
                    : IM_COL32(240, 240, 90, 110);

            dl->AddLine(pos, tgt, lineCol, 1.0f);

            // Optional: draw the path the colonist is following.
            if (showJobPaths && !c.path.empty())
            {
                ImVec2 prev = pos;
                for (std::size_t i = c.pathIndex; i < c.path.size(); ++i)
                {
                    const auto& p = c.path[i];
                    const ImVec2 pt = worldToScreen(cam3, cx, {p.x + 0.5f, p.y + 0.5f});
                    dl->AddLine(prev, pt, pathCol, 1.0f);
                    prev = pt;
                }
            }
        }

// Manual order queue overlay for the selected colonist.
if (c.id == selectedColonistId && !c.manualQueue.empty())
{
    const proto::Colonist::ManualOrder& o0 = c.manualQueue.front();
    const bool frontActive =
        c.hasJob &&
        ((o0.kind == proto::Colonist::ManualOrder::Kind::Move && c.jobKind == proto::Colonist::JobKind::ManualMove) ||
         (o0.kind == proto::Colonist::ManualOrder::Kind::Build && c.jobKind == proto::Colonist::JobKind::BuildPlan) ||
         (o0.kind == proto::Colonist::ManualOrder::Kind::Harvest && c.jobKind == proto::Colonist::JobKind::Harvest)) &&
        c.targetX == o0.x && c.targetY == o0.y;

    const float qR = std::max(2.0f, cx.tilePx * 0.12f);

    // Draw connecting lines between queued order targets (skipping the in-progress front order, if any).
    ImVec2 prev = pos;
    if (frontActive)
        prev = worldToScreen(cam3, cx, {o0.x + 0.5f, o0.y + 0.5f});

    const std::size_t startIdx = frontActive ? 1 : 0;
    for (std::size_t qi = startIdx; qi < c.manualQueue.size(); ++qi)
    {
        const auto& o = c.manualQueue[qi];
        ImVec2 pt = worldToScreen(cam3, cx, {o.x + 0.5f, o.y + 0.5f});
        dl->AddLine(prev, pt, IM_COL32(255, 255, 255, 80), 1.0f);
        prev = pt;
    }

    // Draw the order markers.
    for (std::size_t qi = 0; qi < c.manualQueue.size(); ++qi)
    {
        const auto& o = c.manualQueue[qi];
        ImVec2 pt = worldToScreen(cam3, cx, {o.x + 0.5f, o.y + 0.5f});

        ImU32 col = IM_COL32(220, 220, 220, 180);
        switch (o.kind)
        {
        case proto::Colonist::ManualOrder::Kind::Move:
            col = IM_COL32(200, 120, 240, 180);
            break;
        case proto::Colonist::ManualOrder::Kind::Build:
            col = IM_COL32(240, 240, 90, 180);
            break;
        case proto::Colonist::ManualOrder::Kind::Harvest:
            col = IM_COL32(90, 200, 240, 180);
            break;
        default:
            break;
        }

        dl->AddCircleFilled(pt, qR, col);
        if (qi == 0 && frontActive)
            dl->AddCircle(pt, qR + 1.5f, IM_COL32(255, 255, 255, 180), 0, 2.0f);

        // Numeric labels when zoomed in.
        if (cx.tilePx >= 18.0f)
        {
            const std::string label = std::to_string(qi + 1);
            dl->AddText({pt.x - qR * 0.6f + 1.0f, pt.y - qR * 0.8f + 1.0f}, IM_COL32(0, 0, 0, 220),
                        label.c_str());
            dl->AddText({pt.x - qR * 0.6f, pt.y - qR * 0.8f}, IM_COL32(255, 255, 255, 220), label.c_str());
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

        const std::uint8_t priority = (plan == proto::TileType::Empty)
            ? static_cast<std::uint8_t>(0)
            : static_cast<std::uint8_t>(std::max(0, std::min(3, planBrushPriority)));

        // We keep attempted as "in-bounds tiles in the rect" for consistent status messages.
        int attempted = 0;

        // If atomic placement is enabled, precompute the full batch and verify we can afford it.
        struct RectOp
        {
            int x = 0;
            int y = 0;
            PlanSnapshot before{};
            int deltaWood = 0;
        };

        std::vector<RectOp> ops;
        ops.reserve(static_cast<std::size_t>(std::max(0, (rx1 - rx0 + 1))) *
                    static_cast<std::size_t>(std::max(0, (ry1 - ry0 + 1))));

        int totalDeltaWood  = 0;
        int totalCostWood   = 0;
        int totalRefundWood = 0;

        if (atomicPlanPlacement)
        {
            for (int yy = ry0; yy <= ry1; ++yy)
            {
                for (int xx = rx0; xx <= rx1; ++xx)
                {
                    if (!world.inBounds(xx, yy))
                        continue;

                    ++attempted;

                    const proto::Cell& beforeC = world.cell(xx, yy);
                    if (!proto::PlanWouldChange(beforeC, plan, priority))
                        continue;

                    RectOp op;
                    op.x = xx;
                    op.y = yy;
                    op.before = snapshotFromCell(beforeC);
                    op.deltaWood = proto::PlanDeltaWoodCost(beforeC, plan);
                    ops.push_back(op);

                    totalDeltaWood += op.deltaWood;
                    if (op.deltaWood > 0)
                        totalCostWood += op.deltaWood;
                    else
                        totalRefundWood += -op.deltaWood;
                }
            }

            if (attempted == 0)
                return;

            // Not enough wood to apply the entire rectangle.
            if (totalDeltaWood > world.inventory().wood)
            {
                const int need = totalDeltaWood - world.inventory().wood;
                setStatus("Not enough wood for atomic placement: need " + std::to_string(need) +
                          " more (delta " + std::to_string(totalDeltaWood) +
                          ", cost " + std::to_string(totalCostWood) +
                          ", refund " + std::to_string(totalRefundWood) + ")");
                return;
            }
        }

        // One undoable command.
        if (planHistory.HasActiveCommand())
            (void)planHistory.CommitCommand(world.inventory().wood);
        planHistory.BeginCommand(world.inventory().wood);

        int changed = 0;
        bool notEnough = false;

        if (atomicPlanPlacement)
        {
            // Apply refunds first to maximize the chance of success when swapping plans.
            auto applyOne = [&](const RectOp& op) {
                const auto r = world.placePlan(op.x, op.y, plan, priority);
                if (r == proto::PlacePlanResult::Ok)
                {
                    ++changed;
                    const PlanSnapshot after = snapshotFromCell(world.cell(op.x, op.y));
                    planHistory.RecordChange(op.x, op.y, op.before, after);
                }
                else if (r == proto::PlacePlanResult::NotEnoughWood)
                {
                    // Should never happen due to the pre-check, but keep a safety flag.
                    notEnough = true;
                }
            };

            // deltaWood < 0 => refund
            for (const RectOp& op : ops)
                if (op.deltaWood < 0)
                    applyOne(op);

            // deltaWood == 0 => priority-only or free plan swap
            for (const RectOp& op : ops)
                if (op.deltaWood == 0)
                    applyOne(op);

            // deltaWood > 0 => cost
            for (const RectOp& op : ops)
                if (op.deltaWood > 0)
                    applyOne(op);

            // In atomic mode, attempted count was already computed in the prepass.
        }
        else
        {
            for (int yy = ry0; yy <= ry1; ++yy)
            {
                for (int xx = rx0; xx <= rx1; ++xx)
                {
                    if (!world.inBounds(xx, yy))
                        continue;
                    ++attempted;

                    const proto::Cell& beforeC = world.cell(xx, yy);
                    const PlanSnapshot before = snapshotFromCell(beforeC);

                    const auto r = world.placePlan(xx, yy, plan, priority);
                    if (r == proto::PlacePlanResult::Ok)
                    {
                        ++changed;
                        const PlanSnapshot after = snapshotFromCell(world.cell(xx, yy));
                        planHistory.RecordChange(xx, yy, before, after);
                    }
                    else if (r == proto::PlacePlanResult::NotEnoughWood)
                    {
                        notEnough = true;
                        // Remaining tiles will also fail for positive-cost placements.
                        if (reportNotEnoughWood)
                            goto done;
                    }
                }
            }
        }

    done:
        if (attempted == 0)
        {
            planHistory.CancelCommand();
            return;
        }

        const bool committed = planHistory.CommitCommand(world.inventory().wood);
        if (committed)
            world.CancelAllJobsAndClearReservations();

        if (notEnough && reportNotEnoughWood)
        {
            setStatus("Not enough wood (" + std::to_string(changed) + "/" + std::to_string(attempted) + ")");
        }
        else
        {
            if (plan == proto::TileType::Remove)
                setStatus("Marked " + std::to_string(changed) + " tiles for demolition (P" + std::to_string(static_cast<int>(priority) + 1) + ")");
            else if (plan != proto::TileType::Empty)
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



    auto blueprintTopLeftFromHover = [&](int hoverX, int hoverY, int& outX, int& outY) {
        outX = hoverX;
        outY = hoverY;

        if (!blueprint.Empty() && blueprintAnchor == BlueprintAnchor::Center) {
            outX = hoverX - blueprint.w / 2;
            outY = hoverY - blueprint.h / 2;
        }
    };

    auto drawBlueprintPreviewAt = [&](int topLeftX, int topLeftY) {
        if (blueprint.Empty())
            return;

        constexpr std::size_t kMaxPreviewCells = 20000u;
        if (blueprint.packed.size() > kMaxPreviewCells) {
            const ImVec2 bp0 = worldToScreen(cam3, cx, {(float)topLeftX, (float)topLeftY});
            const ImVec2 bp1 = worldToScreen(cam3, cx, {(float)(topLeftX + blueprint.w), (float)(topLeftY + blueprint.h)});

            dl->AddRectFilled(bp0, bp1, IM_COL32(120, 150, 220, 25));
            dl->AddRect(bp0, bp1, IM_COL32(200, 220, 255, 140), 0.0f, 0, 2.0f);

            char buf[64];
            std::snprintf(buf, sizeof(buf), "BP %dx%d", blueprint.w, blueprint.h);
            dl->AddText({bp0.x + 4.f, bp0.y + 4.f}, IM_COL32(235, 240, 255, 200), buf);
            return;
        }

        const float half = cx.tilePx * 0.5f;
        const int bw     = blueprint.w;
        const int bh     = blueprint.h;

        for (int by = 0; by < bh; ++by) {
            for (int bx = 0; bx < bw; ++bx) {
                const std::size_t idx = static_cast<std::size_t>(by * bw + bx);
                if (idx >= blueprint.packed.size())
                    continue;

                const std::uint8_t cellPacked = blueprint.packed[idx];
                const proto::TileType plan    = colony::game::editor::BlueprintUnpackTile(cellPacked);

                if (plan == proto::TileType::Empty && !blueprintPasteIncludeEmpty)
                    continue;

                const int wx = topLeftX + bx;
                const int wy = topLeftY + by;
                if (!world.inBounds(wx, wy))
                    continue;

                const ImVec2 c = worldToScreen(cam3, cx, {(float)wx + 0.5f, (float)wy + 0.5f});
                const ImVec2 p0 = {c.x - half, c.y - half};
                const ImVec2 p1 = {c.x + half, c.y + half};

                ImU32 fill = tilePlanColor(plan);
                if (plan == proto::TileType::Empty)
                    fill = IM_COL32(220, 80, 80, 45);

                dl->AddRectFilled(p0, p1, fill);
                dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 40));

                if (showPlanPriorities && plan != proto::TileType::Empty && cx.tilePx >= 18.f) {
                    const std::uint8_t pr = colony::game::editor::BlueprintUnpackPriority(cellPacked);
                    char pbuf[8];
                    std::snprintf(pbuf, sizeof(pbuf), "P%u", (unsigned)pr);
                    dl->AddText({p0.x + 2.f, p0.y + 1.f}, IM_COL32(255, 255, 255, 220), pbuf);
                }
            }
        }
    };

    struct BlueprintStampPreview
    {
        int attempted   = 0;
        int wouldChange = 0;
        int deltaWood   = 0;
        int costWood    = 0;
        int refundWood  = 0;
        bool truncated  = false;
    };

    auto previewBlueprintStampAt = [&](int topLeftX, int topLeftY) -> BlueprintStampPreview {
        BlueprintStampPreview out;
        if (blueprint.Empty())
            return out;

        constexpr std::size_t kMaxPreviewCells = 20000u;
        if (blueprint.packed.size() > kMaxPreviewCells)
        {
            out.truncated = true;
            return out;
        }

        const int bw = blueprint.w;
        const int bh = blueprint.h;

        for (int by = 0; by < bh; ++by)
        {
            for (int bx = 0; bx < bw; ++bx)
            {
                const std::size_t idx = static_cast<std::size_t>(by * bw + bx);
                if (idx >= blueprint.packed.size())
                    continue;

                const std::uint32_t packed = blueprint.packed[idx];
                proto::TileType plan = colony::game::editor::BlueprintUnpackTile(packed);
                std::uint8_t pr = colony::game::editor::BlueprintUnpackPriority(packed);

                if (plan == proto::TileType::Empty)
                {
                    if (!blueprintPasteIncludeEmpty)
                        continue;
                    pr = 0;
                }

                const int wx = topLeftX + bx;
                const int wy = topLeftY + by;
                if (!world.inBounds(wx, wy))
                    continue;

                ++out.attempted;

                const proto::Cell& c = world.cell(wx, wy);
                if (!proto::PlanWouldChange(c, plan, pr))
                    continue;

                ++out.wouldChange;

                const int d = proto::PlanDeltaWoodCost(c, plan);
                out.deltaWood += d;
                if (d > 0)
                    out.costWood += d;
                else
                    out.refundWood += -d;
            }
        }

        return out;
    };

    auto applyBlueprintAt = [&](int topLeftX, int topLeftY) {
        if (blueprint.Empty())
            return;

        struct StampOp
        {
            int x = 0;
            int y = 0;
            proto::TileType plan = proto::TileType::Empty;
            std::uint8_t priority = 0;
            PlanSnapshot before{};
            int deltaWood = 0;
        };

        // Optional prepass for atomic placement.
        std::vector<StampOp> ops;
        int attempted = 0;

        int totalDeltaWood  = 0;
        int totalCostWood   = 0;
        int totalRefundWood = 0;

        if (atomicPlanPlacement)
        {
            ops.reserve(blueprint.packed.size());

            const int bw = blueprint.w;
            const int bh = blueprint.h;

            for (int by = 0; by < bh; ++by)
            {
                for (int bx = 0; bx < bw; ++bx)
                {
                    const std::size_t idx = static_cast<std::size_t>(by * bw + bx);
                    if (idx >= blueprint.packed.size())
                        continue;

                    const std::uint32_t packed = blueprint.packed[idx];
                    proto::TileType plan = colony::game::editor::BlueprintUnpackTile(packed);
                    std::uint8_t pr = colony::game::editor::BlueprintUnpackPriority(packed);

                    // Empty blueprint cells optionally erase plans.
                    if (plan == proto::TileType::Empty)
                    {
                        if (!blueprintPasteIncludeEmpty)
                            continue;
                        pr = 0;
                    }

                    const int wx = topLeftX + bx;
                    const int wy = topLeftY + by;
                    if (!world.inBounds(wx, wy))
                        continue;

                    ++attempted;

                    const proto::Cell& beforeC = world.cell(wx, wy);
                    if (!proto::PlanWouldChange(beforeC, plan, pr))
                        continue;

                    StampOp op;
                    op.x = wx;
                    op.y = wy;
                    op.plan = plan;
                    op.priority = pr;
                    op.before = snapshotFromCell(beforeC);
                    op.deltaWood = proto::PlanDeltaWoodCost(beforeC, plan);

                    ops.push_back(op);

                    totalDeltaWood += op.deltaWood;
                    if (op.deltaWood > 0)
                        totalCostWood += op.deltaWood;
                    else
                        totalRefundWood += -op.deltaWood;
                }
            }

            if (attempted == 0)
            {
                setStatus("Blueprint paste: nothing to apply.", 3.0f);
                return;
            }

            if (totalDeltaWood > world.inventory().wood)
            {
                const int need = totalDeltaWood - world.inventory().wood;
                setStatus("Blueprint paste blocked (atomic): need " + std::to_string(need) +
                          " more wood (delta " + std::to_string(totalDeltaWood) +
                          ", cost " + std::to_string(totalCostWood) +
                          ", refund " + std::to_string(totalRefundWood) + ")", 4.0f);
                return;
            }
        }

        // One undoable command.
        if (planHistory.HasActiveCommand())
            (void)planHistory.CommitCommand(world.inventory().wood);
        planHistory.BeginCommand(world.inventory().wood);

        int changed = 0;
        bool notEnough = false;

        if (atomicPlanPlacement)
        {
            auto applyOne = [&](const StampOp& op) {
                const auto r = world.placePlan(op.x, op.y, op.plan, op.priority);
                if (r == proto::PlacePlanResult::Ok)
                {
                    ++changed;
                    const PlanSnapshot after = snapshotFromCell(world.cell(op.x, op.y));
                    planHistory.RecordChange(op.x, op.y, op.before, after);
                }
                else if (r == proto::PlacePlanResult::NotEnoughWood)
                {
                    // Should never happen due to the pre-check.
                    notEnough = true;
                }
            };

            // Refund-first ordering for deterministic + robust batch placement.
            for (const StampOp& op : ops)
                if (op.deltaWood < 0)
                    applyOne(op);
            for (const StampOp& op : ops)
                if (op.deltaWood == 0)
                    applyOne(op);
            for (const StampOp& op : ops)
                if (op.deltaWood > 0)
                    applyOne(op);
        }
        else
        {
            const int bw = blueprint.w;
            const int bh = blueprint.h;

            for (int by = 0; by < bh; ++by) {
                for (int bx = 0; bx < bw; ++bx) {
                    const std::size_t idx = static_cast<std::size_t>(by * bw + bx);
                    if (idx >= blueprint.packed.size())
                        continue;

                    const auto packed = blueprint.packed[idx];
                    const proto::TileType plan = colony::game::editor::BlueprintUnpackTile(packed);

                    // Empty blueprint cells optionally erase plans.
                    if (plan == proto::TileType::Empty && !blueprintPasteIncludeEmpty)
                        continue;

                    const int wx = topLeftX + bx;
                    const int wy = topLeftY + by;
                    if (!world.inBounds(wx, wy))
                        continue;

                    ++attempted;

                    const proto::Cell& beforeC = world.cell(wx, wy);
                    const PlanSnapshot before = snapshotFromCell(beforeC);

                    const std::uint8_t priority = (plan == proto::TileType::Empty)
                        ? static_cast<std::uint8_t>(0)
                        : colony::game::editor::BlueprintUnpackPriority(packed);

                    const auto r = world.placePlan(wx, wy, plan, priority);
                    if (r == proto::PlacePlanResult::Ok)
                    {
                        ++changed;
                        const PlanSnapshot after = snapshotFromCell(world.cell(wx, wy));
                        planHistory.RecordChange(wx, wy, before, after);
                    }
                    else if (r == proto::PlacePlanResult::NotEnoughWood)
                    {
                        notEnough = true;
                    }
                }
            }
        }

        if (attempted == 0)
        {
            planHistory.CancelCommand();
            setStatus("Blueprint paste: nothing to apply.", 3.0f);
            return;
        }

        const bool committed = planHistory.CommitCommand(world.inventory().wood);
        if (committed)
            world.CancelAllJobsAndClearReservations();

        if (notEnough)
        {
            setStatus("Blueprint paste: " + std::to_string(changed) + "/" + std::to_string(attempted) + " (not enough wood)", 4.0f);
        }
        else
        {
            setStatus("Blueprint paste: " + std::to_string(changed) + "/" + std::to_string(attempted), 3.0f);
        }
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
            const bool ctrlHeld  = ImGui::GetIO().KeyCtrl;
            const bool altHeld   = ImGui::GetIO().KeyAlt;

            // If we're in Inspect, allow clicking a colonist (not just tiles).
            int hoveredColonistId = -1;
            if (tool == Tool::Inspect)
            {
                const float hitR = std::max(6.f, cx.tilePx * 0.22f);
                const float hitR2 = hitR * hitR;
                for (const auto& c : world.colonists())
                {
                    const ImVec2 cpos = worldToScreen(cam3, cx, {c.x, c.y});
                    const float dx = mouse.x - cpos.x;
                    const float dy = mouse.y - cpos.y;
                    if ((dx * dx + dy * dy) <= hitR2)
                    {
                        hoveredColonistId = c.id;
                        break;
                    }
                }
            }

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
                else if (tool == Tool::Blueprint)
                {
                    if (blueprint.Empty())
                    {
                        dl->AddRectFilled(p0, p1, IM_COL32(220, 80, 80, 35));
                        dl->AddRect(p0, p1, IM_COL32(220, 80, 80, 160));
                    }
                    else
                    {
                        int bx = 0;
                        int by = 0;
                        blueprintTopLeftFromHover(tx, ty, bx, by);
                        drawBlueprintPreviewAt(bx, by);
                    }
                }
                else
                {
                    const proto::TileType previewPlan = toolTile();
                    if (previewPlan == proto::TileType::Empty)
                    {
                        dl->AddRectFilled(p0, p1, IM_COL32(220, 80, 80, 35));
                        dl->AddRect(p0, p1, IM_COL32(220, 80, 80, 160));
                    }
                    else
                    {
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
            ImGui::TextDisabled("%s", cell.builtFromPlan ? "Player-built" : "Seeded");
            if (cell.planned != proto::TileType::Empty && cell.planned != cell.built)
            {
                ImGui::Text("Plan:  %s", proto::TileTypeName(cell.planned));
                ImGui::Text("Priority: P%d", static_cast<int>(cell.planPriority) + 1);
                if (cell.reservedBy >= 0)
                    ImGui::Text("Reserved: %d", cell.reservedBy);
            }

            if (tool == Tool::Blueprint && !blueprint.Empty())
            {
                int bx = 0;
                int by = 0;
                blueprintTopLeftFromHover(tx, ty, bx, by);

                const BlueprintStampPreview prev = previewBlueprintStampAt(bx, by);

                ImGui::Separator();
                ImGui::Text("Blueprint stamp:");
                ImGui::Text("Size: %dx%d", blueprint.w, blueprint.h);
                ImGui::Text("Anchor: %s", (blueprintAnchor == colony::game::editor::BlueprintAnchor::TopLeft) ? "Top-left" : "Center");
                ImGui::Text("Top-left: (%d, %d)", bx, by);

                if (prev.truncated)
                {
                    ImGui::TextDisabled("Preview truncated (blueprint too large).");
                }
                else
                {
                    ImGui::Text("Would change: %d/%d cells", prev.wouldChange, prev.attempted);
                    ImGui::Text("Wood delta: %+d (cost %d, refund %d)", prev.deltaWood, prev.costWood, prev.refundWood);

                    if (atomicPlanPlacement && prev.deltaWood > world.inventory().wood)
                        ImGui::TextDisabled("Atomic placement: need %d more wood", prev.deltaWood - world.inventory().wood);
                }
            }

            if (hoveredColonistId >= 0)
            {
                for (const auto& c : world.colonists())
                {
                    if (c.id != hoveredColonistId)
                        continue;

                    ImGui::Separator();
                    ImGui::Text("Colonist: C%02d", c.id);
                    ImGui::Text("Drafted: %s", c.drafted ? "Yes" : "No");

                    const char* job = "Idle";
                    if (c.hasJob)
                    {
                        switch (c.jobKind)
                        {
                        case proto::Colonist::JobKind::Eat: job = "Eating"; break;
                        case proto::Colonist::JobKind::Harvest: job = "Harvest"; break;
                        case proto::Colonist::JobKind::BuildPlan: job = "Building"; break;
                        case proto::Colonist::JobKind::ManualMove: job = "Move"; break;
                        default: job = "Working"; break;
                        }
                    }
                    ImGui::Text("Job: %s", job);
                    break;
                }
            }
            ImGui::EndTooltip();



            if (!spaceHeld)
            {
                // --- Selection rectangle mode (Inspect + Shift + Left-drag) ---
                if (selectRectActive)
                {
                    // Track end point while the cursor is in-bounds.
                    selectRectEndX = tx;
                    selectRectEndY = ty;

                    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    {
                        selectRectActive = false;
                        selectRectHas    = true;

                        const int rx0 = std::min(selectRectStartX, selectRectEndX);
                        const int ry0 = std::min(selectRectStartY, selectRectEndY);
                        const int rx1 = std::max(selectRectStartX, selectRectEndX);
                        const int ry1 = std::max(selectRectStartY, selectRectEndY);

                        const int selW = rx1 - rx0 + 1;
                        const int selH = ry1 - ry0 + 1;
                        setStatus("Selected region " + std::to_string(selW) + "x" + std::to_string(selH));
                    }
                }
                // --- Rectangle paint mode (Shift + drag) ---
                else if (rectPaintActive)
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
                else
                {
                    // Start selection rectangle (Inspect + Shift + Left click).
                    if (shiftHeld && tool == Tool::Inspect && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        selectRectActive = true;
                        selectRectHas    = true;
                        selectRectStartX = tx;
                        selectRectStartY = ty;
                        selectRectEndX   = tx;
                        selectRectEndY   = ty;

                        selectedX = tx;
                        selectedY = ty;

                        lastPaintX = std::numeric_limits<int>::min();
                        lastPaintY = std::numeric_limits<int>::min();
                    }

                    // Start rectangle paint / erase (Shift + click).
                    if (shiftHeld)
                    {
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && tool != Tool::Inspect && tool != Tool::Blueprint)
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
                        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && tool != Tool::Inspect)
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
else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && tool == Tool::Inspect)
{
    // Inspect tool: Shift+Right-click queues manual orders.
    //
    // - Move orders apply to all selected colonists.
    // - Build/Harvest orders apply to the primary selection only.
    if (selectedColonistIds.empty())
    {
        setStatus("No colonist selected (left-click a colonist to select).", 2.5f);
    }
    else
    {
        proto::Cell& clickedCell = world.cell(tx, ty);

        // Build: primary selection only.
        if (clickedCell.planned != proto::TileType::Empty && clickedCell.planned != clickedCell.built)
        {
            const int cid = selectedColonistId;
            const proto::OrderResult r = world.OrderColonistBuild(cid, tx, ty, /*queue=*/true);
            if (r == proto::OrderResult::Ok)
                setStatus("Queued C" + std::to_string(cid) + " to build at " +
                              std::to_string(tx) + "," + std::to_string(ty),
                          2.5f);
            else
                setStatus(std::string("Queue order failed: ") + proto::OrderResultName(r), 2.5f);
        }
        // Harvest: primary selection only (farms are exclusive).
        else if (clickedCell.built == proto::TileType::Farm)
        {
            const int cid = selectedColonistId;
            const proto::OrderResult r = world.OrderColonistHarvest(cid, tx, ty, /*queue=*/true);
            if (r == proto::OrderResult::Ok)
                setStatus("Queued C" + std::to_string(cid) + " to harvest at " +
                              std::to_string(tx) + "," + std::to_string(ty),
                          2.5f);
            else
                setStatus(std::string("Queue order failed: ") + proto::OrderResultName(r), 2.5f);
        }
        // Move: apply to all selected.
        else
        {
            if (!world.nav().passable(tx, ty))
            {
                setStatus(std::string("Queue order failed: ") +
                              proto::OrderResultName(proto::OrderResult::TargetBlocked),
                          2.5f);
            }
            else
            {
                std::vector<int> orderIds = selectedColonistIds;
                if (selectedColonistId >= 0 && orderIds.size() > 1)
                {
                    auto it = std::find(orderIds.begin(), orderIds.end(), selectedColonistId);
                    if (it != orderIds.end() && it != orderIds.begin())
                    {
                        const int primary = *it;
                        orderIds.erase(it);
                        orderIds.insert(orderIds.begin(), primary);
                    }
                }

                const std::vector<IntTile> dests =
                    gatherMoveDestinations(world, tx, ty, static_cast<int>(orderIds.size()));

                int ok = 0;
                int fail = 0;
                proto::OrderResult lastFail = proto::OrderResult::Ok;

                for (std::size_t i = 0; i < orderIds.size(); ++i)
                {
                    const int cid = orderIds[i];
                    int dx = tx;
                    int dy = ty;
                    if (i < dests.size())
                    {
                        dx = dests[i].x;
                        dy = dests[i].y;
                    }

                    const proto::OrderResult r = world.OrderColonistMove(cid, dx, dy, /*queue=*/true);
                    if (r == proto::OrderResult::Ok)
                        ++ok;
                    else
                    {
                        ++fail;
                        lastFail = r;
                    }
                }

                if (ok > 0)
                {
                    std::string msg = "Queued " + std::to_string(ok) + " colonist";
                    if (ok != 1)
                        msg += "s";
                    msg += " to move";
                    if (fail > 0)
                        msg += " (" + std::to_string(fail) + " failed)";
                    setStatus(msg, 2.5f);
                }
                else
                {
                    setStatus(std::string("Queue order failed: ") + proto::OrderResultName(lastFail), 2.5f);
                }
            }
        }
    }
}
                    }

                    // Blueprint tool: click to stamp the loaded blueprint.
                    if (tool == Tool::Blueprint && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        if (blueprint.Empty())
                        {
                            setStatus("No blueprint loaded (see Colony panel: Blueprints).", 3.0f);
                        }
                        else
                        {
                            int bx = 0;
                            int by = 0;
                            blueprintTopLeftFromHover(tx, ty, bx, by);
                            applyBlueprintAt(bx, by);
                        }
                    }

                    // Single-tile paint mode (when Shift isn't held).
                    if (!shiftHeld)
                    {
                        auto tryApplySingle = [&](int x, int y, proto::TileType plan, bool reportNotEnoughWood) {
                            if (x == lastPaintX && y == lastPaintY)
                                return;

                            if (!world.inBounds(x, y))
                                return;

                            const proto::Cell& beforeC = world.cell(x, y);

                            // If the tool plan matches built, treat as "clear plan".
                            if (plan == beforeC.built)
                            {
                                plan = proto::TileType::Empty;
                            }

                            // Early out if no-op.
                            if (beforeC.planned == plan)
                            {
                                lastPaintX = x;
                                lastPaintY = y;
                                return;
                            }

                            const int woodBefore = world.inventory().wood;
                            const PlanSnapshot before = snapshotFromCell(beforeC);

                            const auto result = world.placePlan(x, y, plan, static_cast<std::uint8_t>(planBrushPriority));
                            if (result == proto::PlacePlanResult::NotEnoughWood && reportNotEnoughWood)
                            {
                                // Remaining tiles will also fail for positive-cost placements.
                                setStatus("Not enough wood");
                                lastPaintX = x;
                                lastPaintY = y;
                                return;
                            }

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

                        auto tryApplyPrioritySingle = [&](int x, int y) {
                            if (x == lastPaintX && y == lastPaintY)
                                return;

                            if (!world.inBounds(x, y))
                                return;

                            const proto::Cell& beforeC = world.cell(x, y);
                            if (beforeC.planned == proto::TileType::Empty || beforeC.planned == beforeC.built)
                            {
                                lastPaintX = x;
                                lastPaintY = y;
                                return;
                            }

                            const int newPriority = std::clamp(beforeC.planPriority + planBrushPriority - 1, 0, 3);
                            if (newPriority == beforeC.planPriority)
                            {
                                lastPaintX = x;
                                lastPaintY = y;
                                return;
                            }

                            const int woodBefore = world.inventory().wood;
                            const PlanSnapshot before = snapshotFromCell(beforeC);

                            const auto result = world.placePlan(x, y, beforeC.planned, static_cast<std::uint8_t>(newPriority));
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

                        // Left paint: place current tool (except Inspect / Blueprint)
                        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && tool != Tool::Inspect && tool != Tool::Blueprint)
                        {
                            if (tool == Tool::Priority)
                                tryApplyPrioritySingle(tx, ty);
                            else
                                tryApplySingle(tx, ty, toolTile(), /*reportNotEnoughWood=*/ true);
                        }

                        // Inspect tool: right-click issues direct manual orders.
                        //
                        // - Move orders apply to all selected colonists.
                        // - Build/Harvest orders apply to the primary selection only
                        //   (because those targets are exclusive/reserved).
                        if (tool == Tool::Inspect && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                        {
                            if (selectedColonistIds.empty())
                            {
                                setStatus("No colonist selected (left-click a colonist to select).", 3.0f);
                            }
                            else if (cell.planned != proto::TileType::Empty && cell.planned != cell.built)
                            {
                                const int cid = selectedColonistId;
                                const proto::OrderResult r = world.OrderColonistBuild(cid, tx, ty, /*queue=*/false);
                                if (r == proto::OrderResult::Ok)
                                    setStatus("Ordered C" + std::to_string(cid) + " to build at " +
                                              std::to_string(tx) + "," + std::to_string(ty),
                                              3.0f);
                                else
                                    setStatus(std::string("Order failed: ") + proto::OrderResultName(r), 3.0f);
                            }
                            else if (cell.built == proto::TileType::Farm && cell.farmGrowth >= 1.0f)
                            {
                                const int cid = selectedColonistId;
                                const proto::OrderResult r = world.OrderColonistHarvest(cid, tx, ty, /*queue=*/false);
                                if (r == proto::OrderResult::Ok)
                                    setStatus("Ordered C" + std::to_string(cid) + " to harvest at " +
                                              std::to_string(tx) + "," + std::to_string(ty),
                                              3.0f);
                                else
                                    setStatus(std::string("Order failed: ") + proto::OrderResultName(r), 3.0f);
                            }
                            else
                            {
                                if (!world.nav().passable(tx, ty))
                                {
                                    setStatus(std::string("Order failed: ") +
                                                  proto::OrderResultName(proto::OrderResult::TargetBlocked),
                                              3.0f);
                                }
                                else
                                {
                                    std::vector<int> orderIds = selectedColonistIds;
                                    if (selectedColonistId >= 0 && orderIds.size() > 1)
                                    {
                                        auto it = std::find(orderIds.begin(), orderIds.end(), selectedColonistId);
                                        if (it != orderIds.end() && it != orderIds.begin())
                                        {
                                            const int primary = *it;
                                            orderIds.erase(it);
                                            orderIds.insert(orderIds.begin(), primary);
                                        }
                                    }

                                    const std::vector<IntTile> dests =
                                        gatherMoveDestinations(world, tx, ty, static_cast<int>(orderIds.size()));

                                    int ok = 0;
                                    int fail = 0;
                                    proto::OrderResult lastFail = proto::OrderResult::Ok;

                                    for (std::size_t i = 0; i < orderIds.size(); ++i)
                                    {
                                        const int cid = orderIds[i];
                                        int dx = tx;
                                        int dy = ty;
                                        if (i < dests.size())
                                        {
                                            dx = dests[i].x;
                                            dy = dests[i].y;
                                        }

                                        const proto::OrderResult r = world.OrderColonistMove(cid, dx, dy, /*queue=*/false);
                                        if (r == proto::OrderResult::Ok)
                                            ++ok;
                                        else
                                        {
                                            ++fail;
                                            lastFail = r;
                                        }
                                    }

                                    if (ok > 0)
                                    {
                                        std::string msg = "Ordered " + std::to_string(ok) + " colonist";
                                        if (ok != 1)
                                            msg += "s";
                                        msg += " to move";
                                        if (fail > 0)
                                            msg += " (" + std::to_string(fail) + " failed)";
                                        setStatus(msg, 3.0f);
                                    }
                                    else
                                    {
                                        setStatus(std::string("Order failed: ") + proto::OrderResultName(lastFail), 3.0f);
                                    }
                                }
                            }
                        }

                        // Right paint: erase plan (disabled in Inspect to keep right-click for orders).
                        if (ImGui::IsMouseDown(ImGuiMouseButton_Right) && tool != Tool::Inspect)
                        {
                            tryApplySingle(tx, ty, proto::TileType::Empty, /*reportNotEnoughWood=*/ false);
                        }

                        // Inspect selection
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && tool == Tool::Inspect)
                        {
                            if (hoveredColonistId >= 0)
                            {
                                // Ctrl+click toggles multi-selection.
                                if (ctrlHeld)
                                    toggleColonistSelection(hoveredColonistId, /*makePrimaryIfAdding=*/ true);
                                else
                                    selectColonistExclusive(hoveredColonistId);

                                // Snap tile selection to the primary colonist.
                                if (selectedColonistId >= 0)
                                {
                                    for (const auto& c : world.colonists())
                                    {
                                        if (c.id == selectedColonistId)
                                        {
                                            selectedX = static_cast<int>(std::floor(c.x));
                                            selectedY = static_cast<int>(std::floor(c.y));
                                            break;
                                        }
                                    }
                                }

                                setStatus("Selected " + std::to_string(selectedColonistIds.size()) + " colonist(s)",
                                          2.0f);
                            }
                            else
                            {
                                // Ctrl+clicking a tile does NOT clear the current colonist selection,
                                // allowing quick inspection while keeping a group selected.
                                if (!ctrlHeld)
                                    clearColonistSelection();

                                selectedX = tx;
                                selectedY = ty;

                                int pickedRoomId = -1;
                                if (altHeld)
                                {
                                    pickedRoomId = world.roomIdAt(tx, ty);
                                    selectedRoomId = pickedRoomId;
                                }

                                std::string msg;
                                if (!selectedColonistIds.empty())
                                {
                                    msg = std::string("Selected ") + std::to_string(tx) + "," + std::to_string(ty) +
                                          " (+" + std::to_string(selectedColonistIds.size()) + " colonist(s))";
                                }
                                else
                                {
                                    msg = std::string("Selected ") + std::to_string(tx) + "," + std::to_string(ty);
                                }

                                if (altHeld)
                                {
                                    if (pickedRoomId >= 0)
                                        msg += " [R" + std::to_string(pickedRoomId) + "]";
                                    else
                                        msg += " [no room]";
                                }

                                setStatus(msg, 2.0f);
                            }
                        }
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

    // Finalize selection rectangle even if the mouse was released outside the canvas.
    if (selectRectActive && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        selectRectActive = false;
        selectRectHas    = true;

        const int rx0 = std::min(selectRectStartX, selectRectEndX);
        const int ry0 = std::min(selectRectStartY, selectRectEndY);
        const int rx1 = std::max(selectRectStartX, selectRectEndX);
        const int ry1 = std::max(selectRectStartY, selectRectEndY);

        const int w = rx1 - rx0 + 1;
        const int h = ry1 - ry0 + 1;
        setStatus("Selected region " + std::to_string(w) + "x" + std::to_string(h));
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



    // Draw selection rectangle overlay (Inspect + Shift + Left-drag).
    if (selectRectHas || selectRectActive)
    {
        const int rx0 = std::min(selectRectStartX, selectRectEndX);
        const int ry0 = std::min(selectRectStartY, selectRectEndY);
        const int rx1 = std::max(selectRectStartX, selectRectEndX);
        const int ry1 = std::max(selectRectStartY, selectRectEndY);

        const ImVec2 sp0 = worldToScreen(cam3, cx, {(float)rx0, (float)ry0});
        const ImVec2 sp1 = worldToScreen(cam3, cx, {(float)(rx1 + 1), (float)(ry1 + 1)});

        const ImU32 fill   = selectRectActive ? IM_COL32(255, 220, 90, 35) : IM_COL32(255, 220, 90, 20);
        const ImU32 border = selectRectActive ? IM_COL32(255, 240, 140, 220) : IM_COL32(255, 240, 140, 140);

        dl->AddRectFilled(sp0, sp1, fill);
        dl->AddRect(sp0, sp1, border, 0.0f, 0, 2.0f);

        char buf[64];
        std::snprintf(buf, sizeof(buf), "SEL %dx%d", (rx1 - rx0 + 1), (ry1 - ry0 + 1));
        dl->AddText({sp0.x + 4.f, sp0.y + 3.f}, border, buf);
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

    // Toast notifications overlay (top-left, below status).
    if (alertsShowToasts)
    {
        const auto& toasts = notify.toasts();
        if (!toasts.empty())
        {
            float y = canvas_p0.y + 52.f;

            // Render newest -> oldest, capped to avoid filling the screen.
            const int maxShow = 4;
            int shown = 0;

            for (int i = static_cast<int>(toasts.size()) - 1; i >= 0 && shown < maxShow; --i)
            {
                const util::ToastEntry& t = toasts[static_cast<std::size_t>(i)];

                float a = 1.0f;
                if (t.ttlSeconds < 0.5f)
                    a = clampf(t.ttlSeconds / 0.5f, 0.f, 1.f);

                ImU32 textCol = IM_COL32(255, 255, 255, static_cast<int>(220 * a));
                if (t.entry.severity == util::NotifySeverity::Warning)
                    textCol = IM_COL32(255, 210, 120, static_cast<int>(230 * a));
                else if (t.entry.severity == util::NotifySeverity::Error)
                    textCol = IM_COL32(255, 140, 140, static_cast<int>(240 * a));

                const ImU32 bgCol = IM_COL32(0, 0, 0, static_cast<int>(150 * a));

                char line[512] = {};
                (void)std::snprintf(line,
                                    sizeof(line),
                                    "[%s] %s",
                                    util::NotifySeverityName(t.entry.severity),
                                    t.entry.text.c_str());

                const ImVec2 sz = ImGui::CalcTextSize(line);
                const ImVec2 pos = {canvas_p0.x + 8.f, y};

                dl->AddRectFilled({pos.x - 4.f, pos.y - 2.f},
                                  {pos.x + sz.x + 4.f, pos.y + sz.y + 2.f},
                                  bgCol,
                                  4.0f);
                dl->AddText(pos, textCol, line);

                y += sz.y + 6.f;
                ++shown;
            }
        }
    }

    ImGui::End();
}

#endif // COLONY_WITH_IMGUI

} // namespace colony::game
