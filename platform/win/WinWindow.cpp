// platform/win/WinWindow.cpp
// Win32 window creation & message handling for Colony-Game (Windows-only)

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

// We only ever store a pointer to AppWindow and hand it back; no header needed.
class AppWindow;

// Helper to read the AppWindow* we stored in GWLP_USERDATA.
// This is kept even if we don't use it yet, so you can easily hook engine logic
// into the Win32 message pump later.
static AppWindow* GetAppWindowFromHwnd(HWND hwnd)
{
    return reinterpret_cast<AppWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

// Main Win32 window procedure.
// NOTE: Not 'static' anymore – this avoids C4211 when there's an extern
// declaration elsewhere (e.g., in a header). :contentReference[oaicite:2]{index=2}
LRESULT CALLBACK ColonyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    AppWindow* appWindow = GetAppWindowFromHwnd(hwnd);
    (void)appWindow; // currently unused; reserved for future hooks
    (void)wParam;    // avoid C4100 (unreferenced parameter) with /WX

    switch (msg)
    {
    case WM_CREATE:
    {
        // Store the AppWindow* (if any) that was passed via lpCreateParams.
        auto* cs        = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* passedPtr = cs
            ? reinterpret_cast<AppWindow*>(cs->lpCreateParams)
            : nullptr;

        if (passedPtr)
        {
            ::SetWindowLongPtrW(
                hwnd,
                GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(passedPtr)
            );
        }
        return 0;
    }

    case WM_DPICHANGED:
    {
        // lParam points to a suggested RECT in the new DPI.
        // This is straight from MS docs: resize the window to the recommended rect. :contentReference[oaicite:3]{index=3}
        const RECT* const prcNewWindow = reinterpret_cast<const RECT*>(lParam);
        if (prcNewWindow)
        {
            ::SetWindowPos(
                hwnd,
                nullptr,
                prcNewWindow->left,
                prcNewWindow->top,
                prcNewWindow->right  - prcNewWindow->left,
                prcNewWindow->bottom - prcNewWindow->top,
                SWP_NOZORDER | SWP_NOACTIVATE
            );
        }

        // Hook point: if you later want to scale render targets, fonts, etc.,
        // this is the right place to notify the engine (using 'appWindow').
        return 0;
    }

    case WM_SIZE:
        // Hook point: notify engine about resize here if / when you add an API,
        // e.g. AppWindow::OnResize or a global callback.
        //
        // For now we just swallow the message so nothing crashes or warns.
        return 0;

    case WM_ACTIVATE:
        // Hook point: focus / pause / resume logic can go here later.
        return 0;

    case WM_CLOSE:
        // Standard close behaviour: destroy the window, then WM_DESTROY posts quit.
        ::DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;

    default:
        return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

// Optional helper used by the launcher / app bootstrap code.
// This keeps the previous signature (HINSTANCE, nCmdShow, AppWindow&, HWND&)
// but does NOT require any AppWindow methods – we only pass the pointer
// through to WM_CREATE and store it for future use.
bool CreateMainWindow(HINSTANCE   hInstance,
                      int         nCmdShow,
                      AppWindow&  appWindow,
                      HWND&       outHwnd)
{
    const wchar_t* const kClassName = L"ColonyGameMainWindow";

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = &ColonyWndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = hInstance;
    wc.hIcon         = ::LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;   // you’ll usually clear in your renderer instead
    wc.lpszMenuName  = nullptr;
    wc.lpszClassName = kClassName;
    wc.hIconSm       = wc.hIcon;

    if (!::RegisterClassExW(&wc))
        return false;

    // Start with a reasonable default client size; the engine can manage
    // logical resolution vs physical later.
    RECT rc{ 0, 0, 1280, 720 };
    ::AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = ::CreateWindowExW(
        0,
        kClassName,
        L"Colony Game",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right  - rc.left,
        rc.bottom - rc.top,
        nullptr,               // parent
        nullptr,               // menu
        hInstance,
        &appWindow             // lpCreateParams -> WM_CREATE -> GWLP_USERDATA
    );

    if (!hwnd)
        return false;

    outHwnd = hwnd;

    ::ShowWindow(hwnd, nCmdShow);
    ::UpdateWindow(hwnd);

    return true;
}
