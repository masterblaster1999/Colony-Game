// src/game/input/CameraInput.cpp
// -------------------------------------------------------------------------------------------------
// 2D Camera + Input Controller (Win32, single translation unit)
// - Raw mouse panning (RMB/MMB drag) via WM_INPUT (falls back to WM_MOUSEMOVE if raw not available)
// - Wheel zoom (cursor-anchored)
// - Edge-scroll panning
// - Simple action map: pause, timescale, regen-map (one-shot)
// Requirements: Windows, C++17. No external deps.
// Integration:
//   1) Create a global/static CameraInput instance after you create the window.
//   2) In your Win32 WndProc, forward all messages to controller.HandleMessage(...).
//   3) Each frame, call controller.Tick(dtSeconds, hwnd, viewportWidth, viewportHeight).
//   4) Use controller.Center(), controller.Zoom() for your view; or WorldToScreen/ScreenToWorld.
//   5) Read controller.EffectiveTimeScale(), controller.Paused(). If controller.ConsumeRegenRequest()
//      returns true, rebuild your world and clear the flag.
//
// Example hook-up (pseudo):
//   CameraInput g_cam;
//   // after window creation:
//   g_cam.InitializeRawInput(hwnd);
//   ...
//   LRESULT CALLBACK WndProc(...) {
//     if (g_cam.HandleMessage(hwnd, msg, wParam, lParam)) return 0; // consumed
//     switch (msg) { case WM_DESTROY: PostQuitMessage(0); return 0; }
//     return DefWindowProc(hwnd, msg, wParam, lParam);
//   }
//   // in game loop:
//   g_cam.Tick(dt, hwnd, backbufferWidth, backbufferHeight);
//   float ts = g_cam.EffectiveTimeScale();
//   if (g_cam.ConsumeRegenRequest()) { RegenerateWorld(); }
//   // build your 2D view matrix as: screen = (world - g_cam.Center()) * g_cam.Zoom() + 0.5*viewport
//
// -------------------------------------------------------------------------------------------------
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <stdint.h>
#include <cmath>
#include <algorithm>

namespace colony {

// ------------------------------- math helpers --------------------------------
struct Vec2 {
    float x = 0.f, y = 0.f;
    Vec2() = default;
    Vec2(float X, float Y) : x(X), y(Y) {}
    Vec2 operator+(const Vec2& v) const { return {x + v.x, y + v.y}; }
    Vec2 operator-(const Vec2& v) const { return {x - v.x, y - v.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2& operator+=(const Vec2& v){ x += v.x; y += v.y; return *this; }
    Vec2& operator-=(const Vec2& v){ x -= v.x; y -= v.y; return *this; }
    Vec2& operator*=(float s){ x *= s; y *= s; return *this; }
};

template <class T>
static inline T clamp(T v, T lo, T hi) {
    return std::max(lo, std::min(v, hi));
}

// ------------------------------- configuration --------------------------------
// Feel free to tweak these at runtime or wire them to a settings menu.
struct CameraInputConfig {
    // Edge scrolling
    int   edgePixels          = 12;     // distance from each window edge in which edge-scroll engages
    float edgePanSpeedUnits   = 800.f;  // world units per second at zoom=1

    // Drag panning
    float dragPanSpeedUnits   = 1.f;    // multiplier for raw-mouse delta (world units per raw pixel at zoom=1)

    // Zoom
    float zoomMin             = 0.25f;  // 0.25x
    float zoomMax             = 8.0f;   // 8x
    float zoomStepPerNotch    = 1.15f;  // wheel factor per detent (1.15 => +15% / notch)

    // Timescale
    float timeScaleMin        = 0.25f;
    float timeScaleMax        = 16.0f;

    // Input behavior
    bool  useRawInput         = true;   // use WM_INPUT for high-DPI mice while dragging
};

// ------------------------------- controller --------------------------------
class CameraInput {
public:
    explicit CameraInput(const CameraInputConfig& cfg = CameraInputConfig())
        : m_cfg(cfg) {}

    // Call once after window creation
    void InitializeRawInput(HWND hwnd) {
        if (!m_cfg.useRawInput) return;
        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x01; // Generic desktop controls
        rid.usUsage     = 0x02; // Mouse
        // INPUTSINK lets us receive WM_INPUT even when not focused (we gate usage to 'isDragging').
        // We do NOT use RIDEV_NOLEGACY to keep WM_MOUSEMOVE available as a fallback.
        rid.dwFlags     = RIDEV_INPUTSINK;
        rid.hwndTarget  = hwnd;
        RegisterRawInputDevices(&rid, 1, sizeof(rid));
    }

    // Return true if the message was consumed.
    bool HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
            case WM_ACTIVATEAPP:
                m_appActive = (wParam == TRUE);
                break;

            case WM_SIZE:
                // Handled in Tick via params, but we can cache if you prefer.
                break;

            case WM_INPUT: {
                if (!m_cfg.useRawInput || !m_isDragging) break;
                UINT size = 0;
                if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0) break;
                m_rawBuffer.resize(size);
                if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, m_rawBuffer.data(), &size, sizeof(RAWINPUTHEADER)) != size) break;

                RAWINPUT* ri = reinterpret_cast<RAWINPUT*>(m_rawBuffer.data());
                if (ri->header.dwType == RIM_TYPEMOUSE) {
                    const RAWMOUSE& rm = ri->data.mouse;
                    if ((rm.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
                        // Relative motion; apply immediately. Convert from device delta to world units.
                        // We divide by zoom so pan speed feels consistent regardless of zoom level.
                        float dx = static_cast<float>(rm.lLastX);
                        float dy = static_cast<float>(rm.lLastY);
                        PanByPixels(dx, dy);
                        return true; // consumed
                    }
                }
            } break;

            case WM_MOUSEMOVE: {
                // Fallback panning when not using raw input (or raw not active).
                if (m_isDragging && (!m_cfg.useRawInput)) {
                    const POINTS pt = MAKEPOINTS(lParam);
                    Vec2 now(static_cast<float>(pt.x), static_cast<float>(pt.y));
                    Vec2 delta = now - m_lastMouseClient;
                    m_lastMouseClient = now;
                    PanByPixels(delta.x, delta.y);
                    return true;
                }
            } break;

            case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN: {
                SetCapture(hwnd);
                m_isDragging = true;
                POINT p; GetCursorPos(&p); ScreenToClient(hwnd, &p);
                m_lastMouseClient = Vec2(static_cast<float>(p.x), static_cast<float>(p.y));
                return true;
            }

            case WM_RBUTTONUP:
            case WM_MBUTTONUP: {
                m_isDragging = false;
                ReleaseCapture();
                return true;
            }

            case WM_MOUSEWHEEL: {
                // Cursor-anchored zoom: keep the world point under the cursor fixed on screen.
                const int zDelta = GET_WHEEL_DELTA_WPARAM(wParam); // multiples of WHEEL_DELTA (120)
                const int notches = zDelta / WHEEL_DELTA;
                if (notches != 0) {
                    POINT screen = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                    POINT client = screen;
                    ScreenToClient(hwnd, &client);
                    OnWheelZoom(client.x, client.y, notches);
                }
                return true;
            }

            case WM_KEYDOWN:
            case WM_SYSKEYDOWN: {
                // Only treat initial press (ignore auto-repeat)
                const bool wasDown = (lParam & (1 << 30)) != 0;
                if (!wasDown) {
                    if (TranslateActionDown(static_cast<UINT>(wParam))) {
                        return true;
                    }
                }
            } break;

            case WM_KEYUP:
            case WM_SYSKEYUP: {
                // Not strictly needed, but here if you later want "while held" actions.
            } break;
        }

        return false;
    }

    // Per-frame update. Call once per frame.
    void Tick(float dtSeconds, HWND hwnd, int viewportWidth, int viewportHeight) {
        m_viewW = viewportWidth;
        m_viewH = viewportHeight;

        // Edge scroll (only when app active and not dragging)
        if (m_appActive && !m_isDragging) {
            POINT p; GetCursorPos(&p);
            if (WindowFromPoint(p) == hwnd) {
                POINT c = p; ScreenToClient(hwnd, &c);
                Vec2 dir(0, 0);
                if (c.x >= 0 && c.x < m_viewW && c.y >= 0 && c.y < m_viewH) {
                    if (c.x < m_cfg.edgePixels) dir.x -= 1.f;
                    if (c.x > (m_viewW - m_cfg.edgePixels)) dir.x += 1.f;
                    if (c.y < m_cfg.edgePixels) dir.y -= 1.f;
                    if (c.y > (m_viewH - m_cfg.edgePixels)) dir.y += 1.f;
                }
                if (dir.x != 0.f || dir.y != 0.f) {
                    // Move in world units; divide by zoom so speed is consistent visually.
                    const float speed = m_cfg.edgePanSpeedUnits / std::max(0.0001f, m_zoom);
                    m_center += dir * (speed * dtSeconds);
                }
            }
        }

        // Clamp zoom (center clamping is up to your game world bounds)
        m_zoom = clamp(m_zoom, m_cfg.zoomMin, m_cfg.zoomMax);
    }

    // --------------------------- camera & transforms ---------------------------
    const Vec2& Center() const noexcept { return m_center; }
    float       Zoom()   const noexcept { return m_zoom; }

    // World -> Screen (pixels, origin at top-left of client area)
    Vec2 WorldToScreen(const Vec2& world) const {
        Vec2 half(static_cast<float>(m_viewW) * 0.5f, static_cast<float>(m_viewH) * 0.5f);
        return (world - m_center) * m_zoom + half;
    }

    // Screen (pixels) -> World
    Vec2 ScreenToWorld(const Vec2& screen) const {
        Vec2 half(static_cast<float>(m_viewW) * 0.5f, static_cast<float>(m_viewH) * 0.5f);
        return (screen - half) * (1.0f / m_zoom) + m_center;
    }

    // ----------------------------- sim controls --------------------------------
    bool  Paused() const noexcept { return m_paused; }              // raw paused flag
    float TimeScale() const noexcept { return m_timeScale; }        // requested timescale (ignores pause)
    float EffectiveTimeScale() const noexcept { return m_paused ? 0.0f : m_timeScale; }

    // Returns true once when the user requests a map regeneration. Resets the flag.
    bool ConsumeRegenRequest() {
        const bool was = m_regenRequested;
        m_regenRequested = false;
        return was;
    }

    // Optional: programmatic controls
    void SetCenter(const Vec2& c) { m_center = c; }
    void SetZoom(float z)         { m_zoom   = clamp(z, m_cfg.zoomMin, m_cfg.zoomMax); }
    void SetPaused(bool p)        { m_paused = p; }
    void SetTimeScale(float s)    { m_timeScale = clamp(s, m_cfg.timeScaleMin, m_cfg.timeScaleMax); }

private:
    // Apply pixel delta to camera center (dragging); invert Y for typical screen coords (top-left origin).
    void PanByPixels(float dxPixels, float dyPixels) {
        const float mul = (m_cfg.dragPanSpeedUnits / std::max(0.0001f, m_zoom));
        // Move the camera opposite to mouse drag to create "grab & move" feel.
        m_center.x -= dxPixels * mul;
        m_center.y -= dyPixels * mul;
    }

    // Cursor-anchored zoom (client-space coords)
    void OnWheelZoom(int clientX, int clientY, int notches) {
        // World point under the cursor before zoom:
        Vec2 cursorScreen(static_cast<float>(clientX), static_cast<float>(clientY));
        Vec2 worldBefore = ScreenToWorld(cursorScreen);

        // Update zoom:
        const float factor = std::pow(m_cfg.zoomStepPerNotch, static_cast<float>(notches));
        m_zoom = clamp(m_zoom * factor, m_cfg.zoomMin, m_cfg.zoomMax);

        // Recompute center so the same world point stays under the cursor.
        Vec2 half(static_cast<float>(m_viewW) * 0.5f, static_cast<float>(m_viewH) * 0.5f);
        // screen = (world - center) * zoom + half  => center = world - (screen - half)/zoom
        m_center = worldBefore - (cursorScreen - half) * (1.0f / m_zoom);
    }

    // Translate a key press to an action and execute it. Returns true if consumed.
    bool TranslateActionDown(UINT vk) {
        switch (vk) {
            // Pause toggles
            case VK_SPACE:
            case 'P':
                m_paused = !m_paused;
                return true;

            // Fixed speeds
            case '1': SetTimeScale(1.0f);  return true;
            case '2': SetTimeScale(2.0f);  return true;
            case '3': SetTimeScale(4.0f);  return true;

            // Step down/up
            case VK_OEM_MINUS: // '-' on main keyboard
            case VK_SUBTRACT:  // '-' on numpad
                SetTimeScale(m_timeScale * 0.5f);
                return true;

            case VK_OEM_PLUS:  // '=' / '+' on main keyboard
            case VK_ADD:       // '+' on numpad
                SetTimeScale(m_timeScale * 2.0f);
                return true;

            // Regen map (one-shot)
            case 'R':
            case VK_F5:
                m_regenRequested = true;
                return true;

            default: break;
        }
        return false;
    }

private:
    CameraInputConfig m_cfg;

    // Camera state (2D)
    Vec2  m_center { 0.f, 0.f }; // world-space point centered on screen
    float m_zoom   = 1.0f;       // scale: screen = (world - center)*zoom + halfViewport

    // Viewport
    int   m_viewW  = 1;
    int   m_viewH  = 1;

    // Input state
    bool  m_appActive     = true;
    bool  m_isDragging    = false;
    Vec2  m_lastMouseClient{0.f, 0.f};
    bool  m_regenRequested = false;

    // Simulation
    bool  m_paused    = false;
    float m_timeScale = 1.0f;

    // Raw input scratch
    struct ByteBuffer {
        BYTE* dataPtr = nullptr;
        size_t sz = 0;
        ~ByteBuffer(){ delete[] dataPtr; }
        void resize(size_t n) {
            if (n <= sz) return;
            delete[] dataPtr;
            dataPtr = new BYTE[n];
            sz = n;
        }
        BYTE* data(){ return dataPtr; }
    } m_rawBuffer;
};

} // namespace colony

// ----------------------------- OPTIONAL: sample shim --------------------------
// If you want to quick-test this file standalone, you could wrap a minimal WinMain
// and create a window that forwards to a static instance of colony::CameraInput.
// In the game repo, you won't need this: just integrate as per the header comments.
