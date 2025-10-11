// platform/win/Window.cpp
#include "Window.h"
#include <cassert>

using namespace Colony::Win;

namespace {
    // Guard: call once
    void SetProcessDpiAwarenessPerMonitorV2IfAvailable()
    {
        // Prefer manifest in production; programmatic call is a pragmatic fallback.
        // See SetProcessDpiAwarenessContext docs. If unavailable, this is a no-op.
        // https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setprocessdpiawarenesscontext
        auto user32 = ::GetModuleHandleW(L"user32.dll");
        if (!user32) return;

        using SetProcDpiAwareCtxFn = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto pSet = reinterpret_cast<SetProcDpiAwareCtxFn>(
            ::GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (!pSet) return;

        // Ignore result if called too late; best-effort.
        pSet(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }

    RECT AdjustClientToWindow(RECT client, DWORD style, DWORD exStyle, UINT dpi, bool useForDpi)
    {
        if (useForDpi)
        {
            // AdjustWindowRectExForDpi if available (Win10+)
            // https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-adjustwindowrectexfordpi
            auto user32 = ::GetModuleHandleW(L"user32.dll");
            using AdjustForDpiFn = BOOL (WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
            if (auto p = reinterpret_cast<AdjustForDpiFn>(::GetProcAddress(user32, "AdjustWindowRectExForDpi")))
            {
                RECT r = client;
                if (p(&r, style, FALSE, exStyle, dpi))
                    return r;
            }
        }
        RECT r = client;
        ::AdjustWindowRectEx(&r, style, FALSE, exStyle);
        return r;
    }
}

Window::~Window()
{
    if (m_hwnd) ::DestroyWindow(m_hwnd);
    if (!m_className.empty())
        ::UnregisterClassW(m_className.c_str(), m_hinst);
}

void Window::EnablePerMonitorDpiAwareV2()
{
    SetProcessDpiAwarenessPerMonitorV2IfAvailable(); // See note above.
}

bool Window::Create(const WindowDesc& desc)
{
    m_desc = desc;

    if (m_desc.highDPI)
        EnablePerMonitorDpiAwareV2();

    // Register window class
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &StaticWndProc;
    wc.hInstance     = m_hinst;
    wc.hCursor       = ::LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = m_className.c_str();

    if (!::RegisterClassExW(&wc))
        return false;

    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!m_desc.resizable)
        style &= ~(WS_MAXIMIZEBOX | WS_THICKFRAME);
    DWORD exStyle = WS_EX_APPWINDOW;

    // Initial DPI
    UpdateDpiFromWindow(); // pre-create best guess (96)

    RECT client{ 0, 0, m_desc.width, m_desc.height };
    RECT winRect = AdjustClientToWindow(client, style, exStyle, m_dpi, /*useForDpi*/true);

    m_hwnd = ::CreateWindowExW(
        exStyle,
        m_className.c_str(),
        m_desc.title.c_str(),
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        winRect.right - winRect.left,
        winRect.bottom - winRect.top,
        nullptr, nullptr, m_hinst, this);

    if (!m_hwnd)
        return false;

    UpdateDpiFromWindow(); // get actual DPI for created window
    return true;
}

void Window::Show(int nCmdShow)
{
    ::ShowWindow(m_hwnd, nCmdShow);
    ::UpdateWindow(m_hwnd);
}

bool Window::PumpMessages()
{
    MSG msg{};
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT) return false;
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    return true;
}

void Window::AddMsgListener(const MsgCallback& cb)
{
    m_msgListeners.push_back(cb);
}

void Window::ClearMsgListeners()
{
    m_msgListeners.clear();
}

LRESULT CALLBACK Window::StaticWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Window* self = nullptr;
    if (msg == WM_NCCREATE)
    {
        auto cs  = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<Window*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hWnd;
    }
    else
    {
        self = reinterpret_cast<Window*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    if (self)
        return self->WndProc(msg, wParam, lParam);

    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

void Window::OnResize(int w, int h)
{
    if (m_eventCallback)
    {
        WindowEvent ev; ev.type = WindowEvent::Type::Resize; ev.width = w; ev.height = h; ev.dpi = m_dpi;
        m_eventCallback(ev);
    }
}

void Window::OnDpiChanged(UINT newDpi, const RECT* suggested)
{
    m_dpi = newDpi;
    if (suggested)
    {
        // Per WM_DPICHANGED recommendation: resize/move to suggested rect.
        // https://learn.microsoft.com/windows/win32/hidpi/wm-dpichanged
        ::SetWindowPos(m_hwnd, nullptr,
            suggested->left, suggested->top,
            suggested->right - suggested->left,
            suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }

    if (m_eventCallback)
    {
        WindowEvent ev; ev.type = WindowEvent::Type::DpiChanged; ev.dpi = m_dpi;
        m_eventCallback(ev);
    }
}

void Window::UpdateDpiFromWindow()
{
    // GetDpiForWindow (Win10 1607+). Fallback to 96 if missing.
    // https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getdpiforwindow
    auto user32 = ::GetModuleHandleW(L"user32.dll");
    using GetDpiForWindowFn = UINT (WINAPI*)(HWND);
    if (auto p = reinterpret_cast<GetDpiForWindowFn>(::GetProcAddress(user32, "GetDpiForWindow")))
    {
        m_dpi = p(m_hwnd ? m_hwnd : GetDesktopWindow());
    }
    else
    {
        m_dpi = 96;
    }
}

LRESULT Window::WndProc(UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Let listeners peek messages (e.g., Input::HandleMessage)
    bool handledByListener = false;
    for (auto& cb : m_msgListeners)
    {
        if (cb(m_hwnd, msg, wParam, lParam))
            handledByListener = true;
    }

    switch (msg)
    {
    case WM_SIZE:
        OnResize(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_DPICHANGED:
        // https://learn.microsoft.com/windows/win32/hidpi/wm-dpichanged
        OnDpiChanged(HIWORD(wParam), reinterpret_cast<RECT*>(lParam));
        return 0;

    case WM_SETFOCUS:
        if (m_eventCallback) { WindowEvent ev; ev.type = WindowEvent::Type::FocusGained; m_eventCallback(ev); }
        break;

    case WM_KILLFOCUS:
        if (m_eventCallback) { WindowEvent ev; ev.type = WindowEvent::Type::FocusLost; m_eventCallback(ev); }
        break;

    case WM_CLOSE:
        if (m_eventCallback) { WindowEvent ev; ev.type = WindowEvent::Type::Close; m_eventCallback(ev); }
        ::DestroyWindow(m_hwnd);
        return 0;

    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;

    case WM_INPUT:
        // Even if a listener handled it, Win32 requires DefWindowProc for cleanup. (Important!)
        // https://learn.microsoft.com/windows/win32/inputdev/wm-input
        break;
    }

    if (handledByListener && msg != WM_INPUT)
        return 0;

    // For WM_INPUT: always call DefWindowProc (see docs)
    return ::DefWindowProcW(m_hwnd, msg, wParam, lParam);
}
