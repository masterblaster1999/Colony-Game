// platform/win/Window.h
#pragma once
#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <functional>
#include <vector>
#include <cstdint>

namespace Colony::Win
{
    struct WindowDesc
    {
        std::wstring title = L"Colony";
        int width  = 1600;
        int height = 900;
        bool resizable = true;
        bool highDPI = true; // Request Per‑Monitor‑V2 if available.
    };

    struct WindowEvent
    {
        enum class Type { None, Close, Resize, DpiChanged, FocusGained, FocusLost } type = Type::None;
        int  width = 0, height = 0; // for Resize
        UINT dpi = 96;              // for DpiChanged
    };

    // Return true if handled. Still, WM_INPUT must call DefWindowProc (see docs).
    using MsgCallback = std::function<bool(HWND, UINT, WPARAM, LPARAM)>;
    using EventCallback = std::function<void(const WindowEvent&)>;

    class Window
    {
    public:
        Window() = default;
        ~Window();

        // Optionally call before Create() to set DPI awareness programmatically.
        // (Preferred way is via app.manifest, but this is a pragmatic fallback.)
        static void EnablePerMonitorDpiAwareV2(); // no-op if unavailable

        bool Create(const WindowDesc& desc);
        void Show(int nCmdShow = SW_SHOW);
        // Pump all queued messages. Returns false if WM_QUIT received.
        bool PumpMessages();

        // Add/remove generic message listeners (e.g., Input::HandleMessage).
        void AddMsgListener(const MsgCallback& cb);
        void ClearMsgListeners();

        void SetEventCallback(const EventCallback& cb) { m_eventCallback = cb; }

        HWND      Handle() const { return m_hwnd; }
        HINSTANCE HInst()  const { return m_hinst; }
        UINT      CurrentDpi() const { return m_dpi; }
        float     DpiScale()  const { return m_dpi / 96.0f; }

    private:
        static LRESULT CALLBACK StaticWndProc(HWND, UINT, WPARAM, LPARAM);
        LRESULT                 WndProc(UINT, WPARAM, LPARAM);

        void    OnResize(int w, int h);
        void    OnDpiChanged(UINT newDpi, const RECT* suggested);
        void    UpdateDpiFromWindow();

        HINSTANCE m_hinst = GetModuleHandleW(nullptr);
        HWND      m_hwnd  = nullptr;
        UINT      m_dpi   = 96;
        std::wstring m_className = L"ColonyGameWindowClass";
        std::vector<MsgCallback> m_msgListeners;
        EventCallback m_eventCallback {};
        WindowDesc m_desc {};
    };
}
