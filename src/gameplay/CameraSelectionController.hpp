#pragma once
// File: src/gameplay/CameraSelectionController.hpp
//
// Provides camera pan/zoom, drag-select (marquee), and right-click move.
// - Header-only; depends on DirectXMath + Dear ImGui.
// - Pan: Middle mouse drag or WASD (Shift to speed up).
// - Zoom: Mouse wheel (zooms toward cursor), configurable limits.
// - Selection: Left-drag for marquee; click selects a single closest unit.
// - Move: Right-click on ground to issue move order via callback.
// - Renders marquee & destination marker with ImGui draw lists.
//
// Integration:
//   1) Create instance, set callbacks (selectables / move / selection-changed).
//   2) Call Update(dt, width, height) each frame.
//   3) GetViewMatrix()/GetProjMatrix() and feed your renderer.

#include <imgui.h>
#include <DirectXMath.h>
#include <vector>
#include <functional>
#include <cstdint>
#include <algorithm>
#include <limits>
#include <cmath>

namespace colony { namespace game {

using namespace DirectX;

// -----------------------------
// Data model & callbacks
// -----------------------------

struct Selectable {
    uint32_t            id = 0;
    XMFLOAT3            worldPos = {0,0,0}; // world-space position (y can be terrain height)
};

using GatherSelectablesFn = std::function<void(std::vector<Selectable>& out)>; // fill 'out'
using IssueMoveOrderFn    = std::function<void(const std::vector<uint32_t>& ids, const XMFLOAT3& dest)>;
using OnSelectionChangedFn= std::function<void(const std::vector<uint32_t>& ids)>;
using GroundHeightFn      = std::function<float(float x, float z)>;

// -----------------------------
// Controller
// -----------------------------

class CameraSelectionController {
public:
    struct Config {
        // Camera
        float pitchRadians = 60.0f * (3.1415926535f / 180.0f); // fixed tilt
        float minDistance  = 6.0f;
        float maxDistance  = 250.0f;
        float distance     = 40.0f;           // start zoom
        float fovYRadians  = 45.0f * (3.1415926535f / 180.0f);
        float nearZ        = 0.1f;
        float farZ         = 2000.0f;

        // Input
        float wasdSpeed    = 25.0f;           // units/sec at target distance 40
        float wasdSpeedBoost= 2.25f;          // when holding Shift
        float wheelZoomFactor = 1.12f;        // multiplicative per wheel notch
        float wheelZoomToCursor = 1.0f;       // keep cursor world point stable (1 = on)

        // Selection
        float clickPickPixels = 14.0f;        // radius to pick a single unit on click
        float marqueeMinDrag  = 4.0f;         // px before it becomes a drag-select
    };

    CameraSelectionController() = default;

    // ----- Setup -----
    void SetCallbacks(GatherSelectablesFn gather,
                      IssueMoveOrderFn issueMove,
                      OnSelectionChangedFn onSelChanged = {}) {
        m_gather = std::move(gather);
        m_issueMove = std::move(issueMove);
        m_onSelectionChanged = std::move(onSelChanged);
    }

    void SetGroundHeightFn(GroundHeightFn fn) { m_groundY = std::move(fn); }

    // ----- Per-frame -----
    void Update(float dt, int viewportWidth, int viewportHeight) {
        m_vpW = std::max(1, viewportWidth);
        m_vpH = std::max(1, viewportHeight);

        ImGuiIO& io = ImGui::GetIO();

        // Build matrices for this frame
        BuildView();
        BuildProj();

        // Respect ImGui wanting the mouse for UI
        const bool mouseBlocked = io.WantCaptureMouse;

        // Wheel zoom toward cursor
        if (!mouseBlocked && std::fabs(io.MouseWheel) > 0.0f) {
            OnWheelZoom(io.MouseWheel, io.MousePos);
        }

        // Middle-mouse panning (drag ground point under cursor)
        if (!mouseBlocked) {
            HandlePan(io);
        }

        // WASD panning (XZ plane)
        HandleWASD(io, dt);

        // Selection (left mouse)
        if (!mouseBlocked) {
            HandleSelection(io);
        }

        // Move orders (right mouse click)
        if (!mouseBlocked) {
            HandleMove(io);
        }

        // Draw marquee & move marker on the foreground overlay
        DrawOverlay();

        // Rebuild matrices if target or distance changed
        if (m_dirtyCamera) {
            BuildView();
            BuildProj();
            m_dirtyCamera = false;
        }
    }

    // ----- Camera access -----
    XMMATRIX GetViewMatrix() const { return m_view; }
    XMMATRIX GetProjMatrix() const { return m_proj; }
    XMFLOAT3 GetCameraPosition() const { return m_eye; }

    // ----- Selection access -----
    const std::vector<uint32_t>& GetSelection() const { return m_selectedIds; }
    void ClearSelection() {
        m_selectedIds.clear();
        if (m_onSelectionChanged) m_onSelectionChanged(m_selectedIds);
    }

    // ----- Config & state -----
    Config&       GetConfig()       { return m_cfg; }
    const Config& GetConfig() const { return m_cfg; }

    // Set / get where the camera looks
    void SetTarget(const XMFLOAT3& tgt) { m_target = tgt; m_dirtyCamera = true; }
    XMFLOAT3 GetTarget() const { return m_target; }

private:
    // -----------------------------
    // Camera math
    // -----------------------------
    void BuildView() {
        // fixed yaw: look "towards +Z" with a constant tilt around X (pitch)
        const float cp = std::cos(m_cfg.pitchRadians);
        const float sp = std::sin(m_cfg.pitchRadians);

        // Forward (from eye to target) in LH, we want +Z forward with a downward tilt
        XMFLOAT3 forward = { 0.0f, -sp, cp };
        XMVECTOR f = XMLoadFloat3(&forward);

        // Eye = target - forward * distance
        XMVECTOR tgt = XMLoadFloat3(&m_target);
        XMVECTOR eye = XMVectorSubtract(tgt, XMVectorScale(f, m_cfg.distance));
        XMStoreFloat3(&m_eye, eye);

        const XMVECTOR up = XMVectorSet(0, 1, 0, 0);
        m_view = XMMatrixLookAtLH(eye, tgt, up);

        // Precompute for panning projection
        m_cachedForwardXZ = XMVector3Normalize(XMVectorSet(0, 0, 1, 0)); // +Z
        m_cachedRight = XMVector3Normalize(XMVector3Cross(up, m_cachedForwardXZ));
    }

    void BuildProj() {
        const float aspect = (float)m_vpW / (float)m_vpH;
        m_proj = XMMatrixPerspectiveFovLH(m_cfg.fovYRadians, aspect, m_cfg.nearZ, m_cfg.farZ);
    }

    // -----------------------------
    // Input helpers
    // -----------------------------
    void HandlePan(ImGuiIO& io) {
        // Middle mouse drag pans the target, locking the ground point under the cursor.
        if (io.MouseClicked[ImGuiMouseButton_Middle]) {
            m_panActive = true;
            m_panAnchorWorld = ScreenToGround(io.MousePos);
            if (!m_panAnchorWorldValid) m_panActive = false;
        }
        if (m_panActive && io.MouseDown[ImGuiMouseButton_Middle]) {
            const XMFLOAT3 cur = ScreenToGround(io.MousePos);
            if (m_panAnchorWorldValid) {
                XMVECTOR a = XMLoadFloat3(&m_panAnchorWorld);
                XMVECTOR c = XMLoadFloat3(&cur);
                XMVECTOR d = XMVectorSubtract(a, c); // move target by delta so the point stays under cursor
                XMVECTOR tgt = XMVectorAdd(XMLoadFloat3(&m_target), d);
                XMStoreFloat3(&m_target, tgt);
                m_dirtyCamera = true;
            }
        }
        if (m_panActive && io.MouseReleased[ImGuiMouseButton_Middle]) {
            m_panActive = false;
        }
    }

    void HandleWASD(ImGuiIO& io, float dt) {
        float speed = m_cfg.wasdSpeed * (m_cfg.distance / 40.0f);
        if (io.KeyShift) speed *= m_cfg.wasdSpeedBoost;

        float dx = 0.0f;
        float dz = 0.0f;
        if (io.KeysDown[ImGuiKey_A] || io.KeysDown[ImGuiKey_LeftArrow])  dx -= 1.0f;
        if (io.KeysDown[ImGuiKey_D] || io.KeysDown[ImGuiKey_RightArrow]) dx += 1.0f;
        if (io.KeysDown[ImGuiKey_W] || io.KeysDown[ImGuiKey_UpArrow])    dz += 1.0f;
        if (io.KeysDown[ImGuiKey_S] || io.KeysDown[ImGuiKey_DownArrow])  dz -= 1.0f;

        if (dx != 0.0f || dz != 0.0f) {
            XMVECTOR move = XMVectorZero();
            if (dx != 0.0f) move = XMVectorAdd(move, XMVectorScale(m_cachedRight, dx));
            if (dz != 0.0f) move = XMVectorAdd(move, XMVectorScale(m_cachedForwardXZ, dz));
            move = XMVector3Normalize(move);
            move = XMVectorScale(move, speed * dt);
            XMVECTOR tgt = XMVectorAdd(XMLoadFloat3(&m_target), move);
            XMStoreFloat3(&m_target, tgt);
            m_dirtyCamera = true;
        }
    }

    void HandleSelection(ImGuiIO& io) {
        const ImVec2 mouse = io.MousePos;

        // Begin potential drag
        if (io.MouseClicked[ImGuiMouseButton_Left]) {
            m_dragStart = mouse;
            m_dragging = false;
            m_clickConsumed = false;
        }

        // Detect if it becomes a drag
        if (io.MouseDown[ImGuiMouseButton_Left] && !m_dragging) {
            const float dx = mouse.x - m_dragStart.x;
            const float dy = mouse.y - m_dragStart.y;
            if (std::fabs(dx) > m_cfg.marqueeMinDrag || std::fabs(dy) > m_cfg.marqueeMinDrag) {
                m_dragging = true;
            }
        }

        // While dragging, store current rectangle
        if (m_dragging && io.MouseDown[ImGuiMouseButton_Left]) {
            m_dragEnd = mouse;
        }

        // Release: perform selection
        if (io.MouseReleased[ImGuiMouseButton_Left]) {
            if (m_dragging) {
                m_dragging = false;
                DoMarqueeSelect(m_dragStart, m_dragEnd, io.KeyShift);
                m_clickConsumed = true;
            } else if (!m_clickConsumed) {
                // Single click: pick nearest selectable within pixel radius
                SinglePick(mouse, io.KeyShift);
            }
        }
    }

    void HandleMove(ImGuiIO& io) {
        if (io.MouseClicked[ImGuiMouseButton_Right]) {
            // No-op on press; wait for release to avoid accidental drags
        }
        if (io.MouseReleased[ImGuiMouseButton_Right]) {
            if (!m_selectedIds.empty() && m_issueMove) {
                const XMFLOAT3 dst = ScreenToGround(io.MousePos);
                if (m_groundHitValid) {
                    // Adjust y to actual ground height if provided
                    XMFLOAT3 dest = dst;
                    dest.y = m_groundY ? m_groundY(dest.x, dest.z) : dest.y;
                    m_issueMove(m_selectedIds, dest);
                    m_moveMarker.world = dest;
                    m_moveMarker.t = 0.6f; // seconds to display
                }
            }
        }

        // Decay the marker timer
        if (m_moveMarker.t > 0.0f) {
            // dt is not passed here, but we can approximate using ImGui's DeltaTime
            m_moveMarker.t -= ImGui::GetIO().DeltaTime;
            if (m_moveMarker.t < 0.0f) m_moveMarker.t = 0.0f;
        }
    }

    void OnWheelZoom(float wheel, ImVec2 mouse) {
        if (wheel == 0.0f) return;

        // Save world point under cursor (if any)
        XMFLOAT3 anchor = ScreenToGround(mouse);

        // Adjust distance multiplicatively
        const float factor = (wheel > 0.0f) ? (1.0f / m_cfg.wheelZoomFactor) : m_cfg.wheelZoomFactor;
        float newDist = std::clamp(m_cfg.distance * factor, m_cfg.minDistance, m_cfg.maxDistance);
        if (std::fabs(newDist - m_cfg.distance) > 1e-3f) {
            m_cfg.distance = newDist;
            m_dirtyCamera = true;
            BuildView(); // update for second ground sample

            // Keep the world point under cursor stable (if a valid hit)
            if (m_groundHitValid && m_cfg.wheelZoomToCursor > 0.0f) {
                XMFLOAT3 after = ScreenToGround(mouse);
                if (m_groundHitValid) {
                    XMVECTOR a = XMLoadFloat3(&anchor);
                    XMVECTOR b = XMLoadFloat3(&after);
                    XMVECTOR diff = XMVectorSubtract(a, b);
                    diff = XMVectorScale(diff, m_cfg.wheelZoomToCursor);
                    XMVECTOR tgt = XMVectorAdd(XMLoadFloat3(&m_target), diff);
                    XMStoreFloat3(&m_target, tgt);
                }
            }
        }
    }

    // -----------------------------
    // Selection ops
    // -----------------------------
    void DoMarqueeSelect(ImVec2 a, ImVec2 b, bool additive) {
        if (!m_gather) return;

        // Build rectangle (min/max)
        ImVec2 min(std::min(a.x, b.x), std::min(a.y, b.y));
        ImVec2 max(std::max(a.x, b.x), std::max(a.y, b.y));

        std::vector<Selectable> tmp;
        m_gather(tmp);

        std::vector<uint32_t> picked;
        picked.reserve(tmp.size());

        for (const auto& s : tmp) {
            ImVec2 sp;
            if (ProjectToScreen(s.worldPos, sp)) {
                if (sp.x >= min.x && sp.x <= max.x && sp.y >= min.y && sp.y <= max.y)
                    picked.push_back(s.id);
            }
        }

        if (!additive) m_selectedIds.clear();
        // Append new unique ids
        for (uint32_t id : picked) {
            if (std::find(m_selectedIds.begin(), m_selectedIds.end(), id) == m_selectedIds.end())
                m_selectedIds.push_back(id);
        }
        if (m_onSelectionChanged) m_onSelectionChanged(m_selectedIds);
    }

    void SinglePick(ImVec2 mouse, bool additive) {
        if (!m_gather) return;

        std::vector<Selectable> tmp;
        m_gather(tmp);

        float bestD2 = std::numeric_limits<float>::max();
        uint32_t best = 0;
        bool found = false;

        for (const auto& s : tmp) {
            ImVec2 sp;
            if (!ProjectToScreen(s.worldPos, sp)) continue;
            const float dx = sp.x - mouse.x;
            const float dy = sp.y - mouse.y;
            const float d2 = dx*dx + dy*dy;
            if (d2 < bestD2 && d2 <= (m_cfg.clickPickPixels * m_cfg.clickPickPixels)) {
                bestD2 = d2;
                best = s.id;
                found = true;
            }
        }

        if (found) {
            if (!additive)
                m_selectedIds.clear();
            if (std::find(m_selectedIds.begin(), m_selectedIds.end(), best) == m_selectedIds.end())
                m_selectedIds.push_back(best);
            if (m_onSelectionChanged) m_onSelectionChanged(m_selectedIds);
        } else if (!additive) {
            // Click on empty ground clears selection
            if (!m_selectedIds.empty()) {
                m_selectedIds.clear();
                if (m_onSelectionChanged) m_onSelectionChanged(m_selectedIds);
            }
        }
    }

    // -----------------------------
    // Projection & picking
    // -----------------------------
    // Project world -> screen. Returns false if behind camera or off-screen, but we still return screen coords for usability.
    bool ProjectToScreen(const XMFLOAT3& world, ImVec2& out) const {
        XMVECTOR p = XMLoadFloat3(&world);
        XMVECTOR clip = XMVector3Transform(p, XMMatrixMultiply(m_view, m_proj));
        float w = XMVectorGetW(clip);
        if (std::fabs(w) < 1e-6f) return false;
        XMVECTOR ndc = XMVectorScale(clip, 1.0f / w);
        float x = XMVectorGetX(ndc);
        float y = XMVectorGetY(ndc);
        // NDC [-1,1] -> screen
        out.x = (x * 0.5f + 0.5f) * (float)m_vpW;
        out.y = (1.0f - (y * 0.5f + 0.5f)) * (float)m_vpH;
        return (w > 0.0f);
    }

    // Screen -> ground plane (y = terrain height if provided; intersection along current view ray).
    XMFLOAT3 ScreenToGround(ImVec2 screen) {
        m_groundHitValid = false;

        // Unproject to a view ray in world space
        XMVECTOR rayOrigin, rayDir;
        ScreenToRay(screen, rayOrigin, rayDir);

        // Intersect with ground: plane y = h (use current estimate near target)
        // We'll use the ground height at the target XZ as the plane to reduce popping on steep terrain.
        float h = m_groundY ? m_groundY(m_target.x, m_target.z) : 0.0f;

        // Solve: origin.y + t * dir.y = h  =>  t = (h - origin.y) / dir.y
        const float dirY = XMVectorGetY(rayDir);
        if (std::fabs(dirY) < 1e-6f) {
            m_panAnchorWorldValid = false;
            return XMFLOAT3{0, h, 0};
        }
        const float t = (h - XMVectorGetY(rayOrigin)) / dirY;
        if (t < 0.0f) {
            m_panAnchorWorldValid = false;
            return XMFLOAT3{0, h, 0};
        }
        XMVECTOR hit = XMVectorAdd(rayOrigin, XMVectorScale(rayDir, t));
        XMFLOAT3 out;
        XMStoreFloat3(&out, hit);
        out.y = h;
        m_groundHitValid = true;
        m_panAnchorWorldValid = true;
        return out;
    }

    void ScreenToRay(ImVec2 screen, XMVECTOR& outOrigin, XMVECTOR& outDir) const {
        // Convert screen -> NDC
        float x = (2.0f * screen.x) / (float)m_vpW - 1.0f;
        float y = 1.0f - (2.0f * screen.y) / (float)m_vpH;

        XMMATRIX invView = XMMatrixInverse(nullptr, m_view);
        XMMATRIX invProj = XMMatrixInverse(nullptr, m_proj);

        // NDC -> view space
        XMVECTOR nearPt = XMVectorSet(x, y, 0.0f, 1.0f);
        XMVECTOR farPt  = XMVectorSet(x, y, 1.0f, 1.0f);

        nearPt = XMVector3TransformCoord(nearPt, invProj);
        farPt  = XMVector3TransformCoord(farPt,  invProj);

        // View -> world
        nearPt = XMVector3TransformCoord(nearPt, invView);
        farPt  = XMVector3TransformCoord(farPt,  invView);

        outOrigin = nearPt;
        outDir    = XMVector3Normalize(XMVectorSubtract(farPt, nearPt));
    }

    // -----------------------------
    // Overlay rendering
    // -----------------------------
    void DrawOverlay() {
        ImDrawList* dl = ImGui::GetForegroundDrawList();

        // Marquee
        if (m_dragging) {
            ImVec2 a = m_dragStart;
            ImVec2 b = ImGui::GetIO().MousePos;
            ImU32 fill = IM_COL32(64, 160, 255, 40);
            ImU32 line = IM_COL32(64, 160, 255, 180);
            dl->AddRectFilled(a, b, fill, 2.0f);
            dl->AddRect(a, b, line, 2.0f, 0, 2.0f);
        }

        // Move marker (decays over time)
        if (m_moveMarker.t > 0.0f) {
            ImVec2 sp;
            if (ProjectToScreen(m_moveMarker.world, sp)) {
                const float alpha = std::clamp(m_moveMarker.t / 0.6f, 0.0f, 1.0f);
                ImU32 col = IM_COL32(255, 230, 80, (int)(alpha * 200.0f));
                float r = 18.0f + (1.0f - alpha) * 12.0f;
                dl->AddCircle(sp, r, col, 32, 2.0f);
                dl->AddCircle(sp, r * 0.6f, col, 32, 2.0f);
                dl->AddLine(ImVec2(sp.x - r, sp.y), ImVec2(sp.x + r, sp.y), col, 1.0f);
                dl->AddLine(ImVec2(sp.x, sp.y - r), ImVec2(sp.x, sp.y + r), col, 1.0f);
            }
        }
    }

private:
    // Config/state
    Config m_cfg{};

    // Camera
    XMFLOAT3 m_target{0, 0, 0};
    XMFLOAT3 m_eye{0, 0, -1};
    XMMATRIX m_view = XMMatrixIdentity();
    XMMATRIX m_proj = XMMatrixIdentity();
    XMVECTOR m_cachedForwardXZ{ XMVectorZero() };
    XMVECTOR m_cachedRight{ XMVectorZero() };
    bool     m_dirtyCamera = true;

    // Viewport
    int m_vpW = 1280;
    int m_vpH = 720;

    // Panning state
    bool     m_panActive = false;
    XMFLOAT3 m_panAnchorWorld{0,0,0};
    bool     m_panAnchorWorldValid = false;

    // Drag-select state
    bool   m_dragging = false;
    bool   m_clickConsumed = false;
    ImVec2 m_dragStart{0,0};
    ImVec2 m_dragEnd{0,0};

    // Selection
    std::vector<uint32_t> m_selectedIds;

    // Move marker
    struct { XMFLOAT3 world{0,0,0}; float t = 0.0f; } m_moveMarker;

    // Picking bookkeeping
    bool m_groundHitValid = false;

    // Callbacks
    GatherSelectablesFn  m_gather;
    IssueMoveOrderFn     m_issueMove;
    OnSelectionChangedFn m_onSelectionChanged;
    GroundHeightFn       m_groundY;

}; // class CameraSelectionController

}} // namespace colony::game
