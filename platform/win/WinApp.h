#pragma once

#ifndef UNICODE
#  error "UNICODE must be defined for WinApp (wide-char APIs)."
#endif

// Use the centralized, guarded Windows include to avoid C4005 macro redefinitions
// and min/max collisions. This header defines WIN32_LEAN_AND_MEAN and NOMINMAX
// only if they aren't already defined, then includes <windows.h>.
#include "platform/win/WinCommon.h"

#include <shellapi.h> // file drop
#include <string>
#include <functional>
#include <vector>
#include <chrono>

namespace winplat {

struct WinSize {
    int width  = 1280;
    int height = 720;
};

struct WinCreateDesc {
    std::wstring title               = L"Colony Game";
    WinSize      clientSize          = {1280, 720};
    bool         resizable           = true;
    bool         enableFileDrop      = true;
    bool         debugConsole        = true;   // Alloc console in Debug builds
    bool         highDPIAware        = true;   // Per‑Monitor‑V2 awareness
    int          minClientWidth      = 640;
    int          minClientHeight     = 360;
};

struct InputDelta {
    LONG mouseDX = 0;
    LONG mouseDY = 0;
    SHORT wheel  = 0; // in WHEEL_DELTA units
};

class WinApp {
public:
    struct Callbacks {
        // Return false to abort launch if init fails.
        std::function<bool(WinApp&)> onInit;

        // Update tick (dt in seconds).
        std::function<void(WinApp&, float dt)> onUpdate;

        // Optional render callback (if you separate update & render).
        std::function<void(WinApp&)> onRender;

        // Resize notify (client size in pixels, and DPI scale).
        std::function<void(WinApp&, int w, int h, float dpiScale)> onResize;

        // Optional: raw Windows message hook (return true if handled).
        std::function<bool(WinApp&, UINT, WPARAM, LPARAM)> onMessage;

        // Optional: file-drop callback.
        std::function<void(WinApp&, const std::vector<std::wstring>&)> onFileDrop;

        // Shutdown hook (always called once before window is destroyed).
        std::function<void(WinApp&)> onShutdown;
    };

public:
    WinApp() = default;
    ~WinApp();

    // Create window + install DPI awareness. Returns false on failure.
    bool create(const WinCreateDesc& desc, const Callbacks& cbs);

    // Main loop. Returns process exit code.
    int run();

    // Request quit from outside (thread‑safe PostQuitMessage).
    void requestQuit(int exitCode = 0);

    // Toggle borderless “fullscreen” on the current monitor.
    void toggleBorderlessFullscreen();

    // Accessors
    HWND        hwnd()        const { return m_hwnd; }
    HINSTANCE   hinstance()   const { return m_hinstance; }
    float       dpiScale()    const { return m_dpiScale; }
    UINT        dpi()         const { return m_dpi; }
    WinSize     clientSize()  const { return { m_clientW, m_clientH }; }
    bool        isRunning()   const { return m_running; }
    const InputDelta& inputDelta() const { return m_inputDelta; }

private:
    // Internal helpers
    bool registerClass();
    bool createWindowInternal();
    void destroyWindowInternal();
    void pumpOnce();
    void applyDPIAwareness();
    void updateDPIMetrics(HWND forWindow);
    void resizeClientInternal(int w, int h);
    void setTitleInternal(const std::wstring& title);
    void enableFileDrop(bool enable);

    // Message handling
    static LRESULT CALLBACK WndProcThunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT wndProc(HWND, UINT, WPARAM, LPARAM);

private:
    HINSTANCE   m_hinstance = nullptr;
    HWND        m_hwnd      = nullptr;
    std::wstring m_className = L"ColonyGameWinClass";
    std::wstring m_title;

    WinCreateDesc m_desc;
    Callbacks     m_cbs;

    bool        m_running  = false;
    bool        m_fullscreenBorderless = false;

    // DPI
    UINT        m_dpi      = 96;
    float       m_dpiScale = 1.0f;

    // Size
    int         m_clientW  = 1280;
    int         m_clientH  = 720;

    // Input
    InputDelta  m_inputDelta = {};

    // Timing
    using clock = std::chrono::steady_clock;
    clock::time_point m_prevTick{};
};

} // namespace winplat
