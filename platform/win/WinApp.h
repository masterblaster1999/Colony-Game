#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

// Small Win32 host: window + raw input + DPI fallback helper
class WinApp {
public:
    WinApp() = default;

    // Creates the top-level window. Returns false on failure.
    bool CreateWindowClassAndWindow(const std::wstring& title, int width, int height, bool borderless);

    // Registers Raw Input for keyboard+mouse. If noLegacy=true, legacy WM_* input is suppressed.
    void RegisterRawInput(bool noLegacy);

    // Standard message pump; return exit code
    int  RunMessageLoop();

    HWND Hwnd() const { return hwnd_; }

    // Runtime DPI fallback (manifest is primary; call this early in WinMain).
    static void EnablePerMonitorV2DpiAwareness();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static WinApp* self_;

    HWND hwnd_ = nullptr;
};
