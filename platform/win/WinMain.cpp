// Platform/Win/WinMain.cpp
//
// Minimal Win32 entry with:
//  - Safe Hi-DPI awareness init (prefers manifest; API as fallback)
//  - Window class/creation
//  - Message loop suitable for a game (PeekMessage pattern)
//  - Alt+Enter -> borderless fullscreen toggle
//  - WM_SIZE/WM_DPICHANGED handling
//
// If you integrate renderer/SwapchainWin32.{h,cpp}, define CG_HAS_SWAPCHAIN
// and provide a way to bind your swapchain instance (see the #if blocks).
//
// Build: Windows only.

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <shellscalingapi.h>   // Prefer manifest for DPI; API as fallback
#pragma comment(lib, "Shcore.lib")

// Optional: if your tree contains the DXGI swapchain helper from earlier steps,
// you can make this file call into it when resizing/toggling.
#if __has_include("renderer/SwapchainWin32.h")
#   include "renderer/SwapchainWin32.h"
#   define CG_HAS_SWAPCHAIN 1
// Provide (elsewhere) a binding point so the app can hand us the live swapchain.
// Example in your engine after creating it:
//   extern "C" void ColonyBindSwapchain(HWND, cg::SwapchainWin32*);
//   ColonyBindSwapchain(hwnd, &mySwap);
#endif

// ---------- DPI Awareness (API fallback) -------------------------------------
// Prefer manifest-based DPI awareness per Microsoft guidance; this runtime
// init is a safety net for dev builds or missing manifests. See docs.
static bool InitDpiAwareness()
{
    // Windows 10+ Per-Monitor V2
    if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        return true;

    // Windows 8.1+ (PROCESS_PER_MONITOR_DPI_AWARE). E_ACCESSDENIED if already set.
    HRESULT hr = SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    if (SUCCEEDED(hr) || hr == E_ACCESSDENIED)
        return true;

    // Vista+ legacy: system-DPI aware
    if (SetProcessDPIAware())
        return true;

    return false;
}

// ---------- App state carried with the window --------------------------------
struct AppState
{
    // Saved windowed placement for borderless toggle:
    RECT  windowedRect{};
    DWORD windowedStyle{0};
    bool  isBorderless{false};

#ifdef CG_HAS_SWAPCHAIN
    cg::SwapchainWin32* swap{nullptr}; // bound later by engine
#endif
};

// Store/retrieve AppState via GWLP_USERDATA
static inline AppState* GetState(HWND hWnd)
{
    return reinterpret_cast<AppState*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
}

// Toggle borderless by changing styles/sizing to monitor bounds.
// If a SwapchainWin32 is bound later, you can delegate to swap->toggle_borderless().
static void ToggleBorderless(HWND hWnd, AppState& st)
{
#ifdef CG_HAS_SWAPCHAIN
    if (st.swap) {
        st.swap->toggle_borderless();
        st.isBorderless = st.swap->is_borderless();
        return;
    }
#endif

    st.isBorderless = !st.isBorderless;

    if (st.isBorderless)
    {
        // Save current rect/style
        GetWindowRect(hWnd, &st.windowedRect);
        st.windowedStyle = static_cast<DWORD>(GetWindowLongPtrW(hWnd, GWL_STYLE));

        // Full monitor rect
        HMONITOR hmon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{ sizeof(MONITORINFO) };
        GetMonitorInfoW(hmon, &mi);

        // WS_POPUP for borderless; remove WS_OVERLAPPEDWINDOW bits
        SetWindowLongPtrW(hWnd, GWL_STYLE, (st.windowedStyle & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
        SetWindowPos(
            hWnd, HWND_TOP,
            mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right  - mi.rcMonitor.left,
            mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        ShowWindow(hWnd, SW_SHOW);
    }
    else
    {
        // Restore
        SetWindowLongPtrW(hWnd, GWL_STYLE, st.windowedStyle);
        SetWindowPos(
            hWnd, nullptr,
            st.windowedRect.left, st.windowedRect.top,
            st.windowedRect.right  - st.windowedRect.left,
            st.windowedRect.bottom - st.windowedRect.top,
            SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        ShowWindow(hWnd, SW_SHOW);
    }
}

// Resize handler – calls into swapchain if present.
static void HandleResize(HWND hWnd, AppState& st, UINT w, UINT h)
{
    if (w == 0 || h == 0) return; // iconic/minimized or spurious

#ifdef CG_HAS_SWAPCHAIN
    if (st.swap) {
        st.swap->resize(w, h);
    }
#else
    (void)hWnd; (void)st; (void)w; (void)h;
#endif
}

// (Optional) public binding hook the engine can call after creating the swapchain.
#ifdef CG_HAS_SWAPCHAIN
extern "C" __declspec(dllexport)
void ColonyBindSwapchain(HWND hWnd, cg::SwapchainWin32* swap)
{
    if (auto* st = GetState(hWnd)) {
        st->swap = swap;
        st->isBorderless = false; // engine/swap will track true state
    }
}
#endif

// ---------- Window procedure --------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_NCCREATE:
    {
        // Receive AppState* via lpCreateParams (from CreateWindowExW).
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    case WM_SYSKEYDOWN:
        // ALT+Enter -> toggle borderless
        // WM_SYSKEYDOWN is posted when ALT is held and a key is pressed (here, Enter).
        if (wParam == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000))
        {
            if (auto* st = GetState(hWnd)) {
                ToggleBorderless(hWnd, *st);
            }
            return 0; // indicate handled (avoid system beep)
        }
        break;

    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
        {
            const UINT w = LOWORD(lParam);
            const UINT h = HIWORD(lParam);
            if (auto* st = GetState(hWnd)) {
                HandleResize(hWnd, *st, w, h);
            }
        }
        break;

    case WM_DPICHANGED:
        // Adjust window to recommended rect to keep correct scaling in PMv2 mode.
        if (RECT* const suggested = reinterpret_cast<RECT*>(lParam))
        {
            SetWindowPos(
                hWnd, nullptr,
                suggested->left, suggested->top,
                suggested->right  - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            return 0; // handled
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ---------- Entry point -------------------------------------------------------
int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow)
{
    (void)hInst;

    // Prefer manifest-based DPI awareness; this API call is a dev fallback.
    InitDpiAwareness();

    // Register a basic window class
    const wchar_t* kClassName = L"ColonyGameWindowClass";
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon         = nullptr;
    wc.hIconSm       = nullptr;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr, L"RegisterClassExW failed.", L"Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    // Desired client size; AdjustWindowRectEx to get correct outer size.
    RECT rc{ 0, 0, 1280, 720 };
    const DWORD style   = WS_OVERLAPPEDWINDOW;
    const DWORD exStyle = WS_EX_APPWINDOW;
    AdjustWindowRectEx(&rc, style, FALSE, exStyle);

    AppState state{}; // lives for the duration of the app

    HWND hWnd = CreateWindowExW(
        exStyle,
        kClassName,
        L"Colony Game",
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left,
        rc.bottom - rc.top,
        nullptr, nullptr, hInst,
        &state); // lpCreateParams -> WM_NCCREATE

    if (!hWnd) {
        MessageBoxW(nullptr, L"CreateWindowExW failed.", L"Error", MB_OK | MB_ICONERROR);
        return -2;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // Main loop: PeekMessage-based (lets you run a per-frame tick if needed).
    MSG msg{};
    bool running = true;
    while (running)
    {
        // Drain the message queue
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!running) break;

        // --- Your per-frame game/update/render goes here ----------------------
        // Example:
        //    EngineTick();
        //    RendererDraw();
        //    if (state.swap) state.swap->present();
        //
        // This loop intentionally does no Sleep to keep latency low; throttle in engine.
    }

    return static_cast<int>(msg.wParam);
}
