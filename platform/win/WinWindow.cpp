// Platform/Win/WinWindow.cpp

#ifndef WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include "AppWindow.h"   // <- adjust to your actual header
#include "WinWindow.h"   // <- if you have a matching header

// Helper: get the AppWindow* from an HWND using GWLP_USERDATA
static AppWindow* GetAppWindowFromHwnd(HWND hwnd)
{
    return reinterpret_cast<AppWindow*>(::GetWindowLongPtr(hwnd, GWLP_USERDATA));
}

// Central Windows message handler.
// All your `case WM_...:` blocks live INSIDE this function.
static LRESULT CALLBACK ColonyWndProc(HWND hwnd,
                                      UINT   msg,
                                      WPARAM wParam,
                                      LPARAM lParam)
{
    AppWindow* appWindow = GetAppWindowFromHwnd(hwnd);

    switch (msg)
    {
    case WM_CREATE:
    {
        // When the window is created, we can grab the AppWindow* passed via lpCreateParams
        auto* cs        = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* passedPtr = reinterpret_cast<AppWindow*>(cs->lpCreateParams);
        if (passedPtr)
        {
            ::SetWindowLongPtr(hwnd,
                               GWLP_USERDATA,
                               reinterpret_cast<LONG_PTR>(passedPtr));
            appWindow = passedPtr;
        }
        return 0;
    }

    case WM_SIZE:
        if (appWindow)
        {
            const int width  = LOWORD(lParam);
            const int height = HIWORD(lParam);
            appWindow->OnResize(width, height);   // <- implement this in AppWindow
        }
        return 0;

    case WM_ACTIVATE:
        if (appWindow)
        {
            const bool active = (LOWORD(wParam) != WA_INACTIVE);
            appWindow->OnActivate(active);        // <- optional; implement if useful
        }
        return 0;

    case WM_DPICHANGED:
    {
        // lParam points to a suggested RECT in the new DPI
        const RECT* const prcNewWindow =
            reinterpret_cast<const RECT*>(lParam);

        ::SetWindowPos(hwnd, nullptr,
                       prcNewWindow->left,
                       prcNewWindow->top,
                       prcNewWindow->right  - prcNewWindow->left,
                       prcNewWindow->bottom - prcNewWindow->top,
                       SWP_NOZORDER | SWP_NOACTIVATE);

        // If your renderer scales with DPI, this is a good place to
        // notify it / recreate swapchain buffers, e.g.:
        //
        // if (appWindow)
        //     appWindow->OnDpiChanged(LOWORD(wParam), HIWORD(wParam));
        //
        // (Make OnDpiChanged a method on AppWindow if you want.)

        return 0;
    }

    case WM_CLOSE:
        if (appWindow)
            appWindow->OnCloseRequested();        // <- let game decide if quitting is OK
        ::DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;

    default:
        return ::DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

// Creates the native window and attaches it to an AppWindow instance.
// Call this from your WinMain / WinLauncher code.
bool CreateMainWindow(HINSTANCE   hInstance,
                      int         nCmdShow,
                      AppWindow&  appWindow,
                      HWND&       outHwnd)
{
    const wchar_t* kClassName = L"ColonyGameWindowClass";

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = &ColonyWndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = hInstance;
    wc.hIcon         = ::LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor       = ::LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // game will redraw everything
    wc.lpszMenuName  = nullptr;
    wc.lpszClassName = kClassName;
    wc.hIconSm       = wc.hIcon;

    if (!::RegisterClassExW(&wc))
        return false;

    // Pick a default size; the renderer can read back size and adjust.
    RECT rc{0, 0, 1280, 720};
    ::AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = ::CreateWindowExW(
        0,
        kClassName,
        L"Colony Game",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right  - rc.left,
        rc.bottom - rc.top,
        nullptr,
        nullptr,
        hInstance,
        &appWindow            // lpCreateParams -> retrieved in WM_CREATE
    );

    if (!hwnd)
        return false;

    outHwnd = hwnd;

    ::ShowWindow(hwnd, nCmdShow);
    ::UpdateWindow(hwnd);

    // Let AppWindow know which HWND it controls.
    appWindow.AttachToNativeWindow(hwnd); // implement this in AppWindow

    return true;
}
