// platform/win/WinApp.cpp
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <mmsystem.h>
#include <xinput.h>
#include <vector>
#include <string>
#include <cassert>
#include "WinApp.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "xinput9_1_0.lib")

// Static singleton
WinApp* WinApp::s_self = nullptr;

// ------------------------------ DPI awareness helpers ------------------------------
void WinApp::EnablePerMonitorV2DpiFallback(bool enable)
{
    if(!enable) return;

    // Prefer PMv2 process awareness if available (Win10+)
    HMODULE user = ::GetModuleHandleA("user32.dll");
    if (user) {
        using SetDpiCtx = BOOL (WINAPI*)(HANDLE);
        auto p = (SetDpiCtx)::GetProcAddress(user, "SetProcessDpiAwarenessContext");
        if (p) {
            if (p(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
        }
    }
    // Fallback to SHCore (Win8.1+)
    HMODULE shcore = ::LoadLibraryA("SHCore.dll");
    if (shcore) {
        using SetPDA = HRESULT (WINAPI*)(int);
        auto sp = (SetPDA)::GetProcAddress(shcore, "SetProcessDpiAwareness");
        if (sp) {
            const int PROCESS_PER_MONITOR_DPI_AWARE = 2;
            if (SUCCEEDED(sp(PROCESS_PER_MONITOR_DPI_AWARE))) {
                ::FreeLibrary(shcore);
                return;
            }
        }
        ::FreeLibrary(shcore);
    }
    // Legacy fallback
    ::SetProcessDPIAware();
}

// ------------------------------ Raw input -----------------------------------------
void WinApp::RegisterRawInput(bool noLegacy)
{
    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01;         // generic desktop controls
    rid.usUsage     = 0x02;         // mouse
    rid.dwFlags     = RIDEV_INPUTSINK | (noLegacy ? RIDEV_NOLEGACY : 0);
    rid.hwndTarget  = m_hwnd;
    ::RegisterRawInputDevices(&rid, 1, sizeof(rid));
    // Note: uninstall with RIDEV_REMOVE + hwndTarget==NULL if ever needed. :contentReference[oaicite:2]{index=2}
}

void WinApp::EnableFileDrops(bool accept)
{
    ::DragAcceptFiles(m_hwnd, accept ? TRUE : FALSE);
}

// ------------------------------ Win32 plumbing ------------------------------------
LRESULT CALLBACK WinApp::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    WinApp* self = s_self;
    switch (msg)
    {
    case WM_CREATE:
        return 0;

    case WM_DPICHANGED:
        if (self && self->m_cbs.onDpiChanged) {
            UINT x = LOWORD(wp), y = HIWORD(wp);
            self->m_cbs.onDpiChanged(x, y);
        }
        if (RECT* suggested = reinterpret_cast<RECT*>(lp)) {
            ::SetWindowPos(hwnd, nullptr,
                suggested->left, suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;

    case WM_SIZE:
        if (self && self->m_cbs.onResize) {
            RECT cr{}; ::GetClientRect(hwnd, &cr);
            int cw = cr.right - cr.left;
            int ch = cr.bottom - cr.top;

            // Use actual window DPI for scale factor. :contentReference[oaicite:3]{index=3}
            UINT dpi = 96;
            if (auto mod = ::GetModuleHandleA("user32.dll")) {
                using GetDpiForWindow_t = UINT (WINAPI*)(HWND);
                if (auto p = (GetDpiForWindow_t)::GetProcAddress(mod, "GetDpiForWindow"))
                    dpi = p(hwnd);
            }
            float scale = dpi / 96.0f;
            self->m_cbs.onResize(*self, cw, ch, scale);
        }
        return 0;

    case WM_INPUT:
        if (self && self->m_cbs.onRawInput) {
            UINT size = 0;
            if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) == 0 && size) {
                std::vector<BYTE> buf(size);
                if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, buf.data(), &size, sizeof(RAWINPUTHEADER)) == size) {
                    const RAWINPUT& ri = *reinterpret_cast<const RAWINPUT*>(buf.data());
                    self->m_cbs.onRawInput(ri);
                    if (ri.header.dwType == RIM_TYPEMOUSE && self->m_cbs.onMouseRawDelta) {
                        LONG dx = ri.data.mouse.lLastX;
                        LONG dy = ri.data.mouse.lLastY;
                        bool absolute = (ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0;
                        self->m_cbs.onMouseRawDelta(*self, dx, dy, absolute);
                    }
                }
            }
        }
        return 0;

    case WM_MOUSEWHEEL:
        if (self && self->m_cbs.onMouseWheel) {
            short delta = GET_WHEEL_DELTA_WPARAM(wp);
            self->m_cbs.onMouseWheel(*self, delta, false);
        }
        return 0;

    case WM_MOUSEHWHEEL:
        if (self && self->m_cbs.onMouseWheel) {
            short delta = GET_WHEEL_DELTA_WPARAM(wp);
            self->m_cbs.onMouseWheel(*self, delta, true);
        }
        return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (self && self->m_cbs.onKeyRaw) {
            bool down = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
            self->m_cbs.onKeyRaw(*self, (unsigned short)wp, down);
        }
        return 0;

    case WM_DROPFILES:
        if (self && self->m_cbs.onFileDrop) {
            HDROP hdrop = (HDROP)wp;
            UINT count = ::DragQueryFileW(hdrop, 0xFFFFFFFF, nullptr, 0);
            std::vector<std::wstring> names;
            names.reserve(count);
            wchar_t tmp[MAX_PATH];
            for (UINT i = 0; i < count; ++i) {
                UINT n = ::DragQueryFileW(hdrop, i, tmp, MAX_PATH);
                if (n) names.emplace_back(tmp, tmp + n);
            }
            ::DragFinish(hdrop);
            self->m_cbs.onFileDrop(*self, names);
        }
        return 0;

    case WM_CLOSE:
        ::DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

// ------------------------------ Instance API --------------------------------------
bool WinApp::Create(const WinCreateDesc& desc, const Callbacks& cbs)
{
    m_hInst = ::GetModuleHandleW(nullptr);

    if (desc.highDPIAware)
        EnablePerMonitorV2DpiFallback(true); // Prefer PMv2, then PMv1, else system DPI.

    // Register class (explicit assignments; no brace-list pitfalls). :contentReference[oaicite:4]{index=4}
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &WinApp::WndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = m_hInst;
    wc.hIcon         = ::LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName  = nullptr;
    wc.lpszClassName = L"ColonyWinApp";
    wc.hIconSm       = wc.hIcon;
    ATOM atom = ::RegisterClassExW(&wc);
    if (!atom) return false;

    // Styles from normalized desc
    DWORD style  = WS_OVERLAPPEDWINDOW;
    DWORD exStyle = 0;
    if (!desc.resizable) {
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    }

    // DPI-correct client sizing
    RECT wr{0,0, desc.clientSize.cx, desc.clientSize.cy}; // SIZE uses cx/cy
    HMODULE user = ::GetModuleHandleA("user32.dll");
    using AdjustForDpi_t = BOOL (WINAPI*)(LPRECT,DWORD,BOOL,DWORD,UINT);
    AdjustForDpi_t pAdjForDpi = user ? (AdjustForDpi_t)::GetProcAddress(user, "AdjustWindowRectExForDpi") : nullptr;
    if (pAdjForDpi) {
        UINT dpi = 96;
        using GetDpiForSystem_t = UINT (WINAPI*)(void);
        if (auto pGetDpiForSystem = user ? (GetDpiForSystem_t)::GetProcAddress(user, "GetDpiForSystem") : nullptr)
            dpi = pGetDpiForSystem();
        (void)pAdjForDpi(&wr, style, FALSE, exStyle, dpi);        // per-DPI adjustment (Win10+) :contentReference[oaicite:5]{index=5}
    } else {
        ::AdjustWindowRectEx(&wr, style, FALSE, exStyle);
    }

    m_hwnd = ::CreateWindowExW(
        exStyle, L"ColonyWinApp", desc.title, style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr, m_hInst, nullptr);
    if (!m_hwnd) return false;

    m_cbs = cbs;

    // Optional developer console
    if (desc.debugConsole) {
        if (::AllocConsole()) {
            FILE* f;
            freopen_s(&f, "CONOUT$", "w", stdout);
            freopen_s(&f, "CONOUT$", "w", stderr);
        }
    }

    // File drops enabled if callback present
    EnableFileDrops(m_cbs.onFileDrop != nullptr);

    // Raw input default: keep legacy messages too (noLegacy=false) for compatibility.
    RegisterRawInput(false);

    ::ShowWindow(m_hwnd, SW_SHOW);
    ::UpdateWindow(m_hwnd);

    if (m_cbs.onInit) m_cbs.onInit(*this);
    return true;
}

int WinApp::RunMessageLoop()
{
    LARGE_INTEGER fq; ::QueryPerformanceFrequency(&fq);
    auto qpc = [](){ LARGE_INTEGER li; ::QueryPerformanceCounter(&li); return li.QuadPart; };
    double inv = 1.0 / double(fq.QuadPart);
    long long t0 = qpc();

    MSG msg{};
    for (;;) {
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                if (m_cbs.onShutdown) m_cbs.onShutdown(*this);
                return int(msg.wParam);
            }
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }

        long long t1 = qpc();
        float dt = float(double(t1 - t0) * inv);
        t0 = t1;
        if (m_cbs.onUpdate) m_cbs.onUpdate(*this, dt);
        if (m_cbs.onRender) m_cbs.onRender(*this);
        // A very small sleep to avoid pegging single-core machines.
        ::Sleep(0);
    }
}

// ------------------------------ Static facade -------------------------------------
bool WinApp::create(const WinCreateDesc& desc, const Callbacks& cbs)
{
    if (!s_self) s_self = new WinApp();
    return s_self->Create(desc, cbs);
}
int  WinApp::run()
{
    return s_self ? s_self->RunMessageLoop() : -1;
}
HWND WinApp::hwnd()
{
    return s_self ? s_self->m_hwnd : nullptr;
}
