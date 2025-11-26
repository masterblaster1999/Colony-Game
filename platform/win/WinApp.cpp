#include "WinApp.h"
#include <shellscalingapi.h>   // AdjustWindowRectExForDpi, GetDpiForSystem
#include <shellapi.h>          // DragAcceptFiles, DragQueryFile, DragFinish
#include <cstdio>
#include <vector>

#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "Shell32.lib")

WinApp* WinApp::s_self = nullptr;

// Prefer manifest (Per-Monitor v2). This is a runtime fallback for safety.
// AdjustWindowRectExForDpi computes window size from desired client size at a given DPI.  [MS Docs]
void WinApp::EnablePerMonitorV2DpiFallback() {
    HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    if (user32) {
        using SetCtx = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
        if (auto fn = reinterpret_cast<SetCtx>(::GetProcAddress(user32, "SetProcessDpiAwarenessContext"))) {
            fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }
}

static void ComputeWindowRectForClient(SIZE desiredClient, DWORD style, DWORD exStyle, RECT& outRect) {
    RECT r{ 0, 0, desiredClient.cx, desiredClient.cy };
    HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    auto pGetDpiForSystem = reinterpret_cast<UINT (WINAPI*)()>(
        ::GetProcAddress(user32, "GetDpiForSystem"));
    auto pAdjustWindowRectExForDpi = reinterpret_cast<BOOL (WINAPI*)(LPRECT,DWORD,BOOL,DWORD,UINT)>(
        ::GetProcAddress(user32, "AdjustWindowRectExForDpi"));

    if (pAdjustWindowRectExForDpi) {
        const UINT dpi = pGetDpiForSystem ? pGetDpiForSystem() : 96u;
        pAdjustWindowRectExForDpi(&r, style, FALSE, exStyle, dpi);  // DPI-aware sizing
    } else {
        ::AdjustWindowRectEx(&r, style, FALSE, exStyle);
    }
    outRect = r;
}

void WinApp::RegisterRawInput(bool noLegacy) {
    RAWINPUTDEVICE rids[2]{};

    // Keyboard
    rids[0].usUsagePage = 0x01;  // HID_USAGE_PAGE_GENERIC
    rids[0].usUsage     = 0x06;  // KEYBOARD
    rids[0].dwFlags     = (noLegacy ? RIDEV_NOLEGACY : 0) | RIDEV_DEVNOTIFY;
    rids[0].hwndTarget  = m_hwnd;

    // Mouse
    rids[1].usUsagePage = 0x01;  // GENERIC
    rids[1].usUsage     = 0x02;  // MOUSE
    rids[1].dwFlags     = (noLegacy ? RIDEV_NOLEGACY : 0) | RIDEV_DEVNOTIFY;
    rids[1].hwndTarget  = m_hwnd;

    ::RegisterRawInputDevices(rids, 2, sizeof(RAWINPUTDEVICE));   // Enables WM_INPUT
}

bool WinApp::Create(const WinCreateDesc& desc, const Callbacks& cbs) {
    m_hInst = desc.hInstance ? desc.hInstance : ::GetModuleHandleW(nullptr);
    m_cbs   = cbs;

    // Optional debug console: attach to parent or allocate a new one.
    if (desc.debugConsole) {
        if (!::AttachConsole(ATTACH_PARENT_PROCESS)) {
            ::AllocConsole();
        }
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        freopen_s(&f, "CONIN$",  "r", stdin);
    }

    if (desc.highDPIAware && desc.enableDpiFallback) {
        EnablePerMonitorV2DpiFallback();
    }

    // Window class
    WNDCLASSW wc{};
    wc.hInstance     = m_hInst;
    wc.lpszClassName = L"ColonyGameWindowClass";
    wc.lpfnWndProc   = &WinApp::WndProc;
    wc.hCursor       = ::LoadCursor(nullptr, IDC_ARROW);
    if (!::RegisterClassW(&wc)) return false;

    // Style (respect resizable=false)
    DWORD style  = desc.style;
    DWORD exStyle= desc.exStyle;
    if (!desc.resizable) {
        style &= ~WS_THICKFRAME;
        style &= ~WS_MAXIMIZEBOX;
    }

    // Desired client area
    SIZE desiredClient = desc.clientSize;
    if (desiredClient.cx <= 0 || desiredClient.cy <= 0) {
        desiredClient.cx = desc.width;
        desiredClient.cy = desc.height;
    }

    RECT wr{};
    ComputeWindowRectForClient(desiredClient, style, exStyle, wr);

    m_hwnd = ::CreateWindowExW(
        exStyle, wc.lpszClassName, desc.title, style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr, m_hInst, nullptr);
    if (!m_hwnd) return false;

    if (m_cbs.onFileDrop) {
        ::DragAcceptFiles(m_hwnd, TRUE); // enables WM_DROPFILES
    }

    ::ShowWindow(m_hwnd, SW_SHOW);
    ::UpdateWindow(m_hwnd);

    RegisterRawInput(desc.rawInputNoLegacy);
    return true;
}

// Legacy static wrappers
bool WinApp::create(const WinCreateDesc& d, const Callbacks& c) {
    if (!s_self) s_self = new WinApp();
    return s_self->Create(d, c);
}
int WinApp::run() {
    return s_self ? s_self->RunMessageLoop() : -1;
}
HWND WinApp::hwnd() {
    return s_self ? s_self->GetHwnd() : nullptr;
}

int WinApp::RunMessageLoop() {
    if (m_cbs.onInit) m_cbs.onInit(*this);

    LARGE_INTEGER freq{}, prev{}, cur{};
    ::QueryPerformanceFrequency(&freq);
    ::QueryPerformanceCounter(&prev);

    MSG msg{};
    bool running = true;
    while (running) {
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = false; break; }
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        if (!running) break;

        ::QueryPerformanceCounter(&cur);
        const float dt = float(double(cur.QuadPart - prev.QuadPart) / double(freq.QuadPart));
        prev = cur;

        if (m_cbs.onUpdate) m_cbs.onUpdate(*this, dt);
        if (m_cbs.onRender) m_cbs.onRender(*this);
    }

    if (m_cbs.onShutdown) m_cbs.onShutdown();
    return int(msg.wParam);
}

LRESULT CALLBACK WinApp::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WinApp* self = s_self;
    switch (msg) {
    case WM_INPUT: {
        if (self && self->m_cbs.onRawInput) {
            UINT size = 0;
            ::GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
            std::vector<BYTE> buf(size);
            if (::GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buf.data(), &size, sizeof(RAWINPUTHEADER)) == size) {
                self->m_cbs.onRawInput(*reinterpret_cast<RAWINPUT*>(buf.data()));
            }
        }
        return 0;
    }

    case WM_SIZE:
        if (self && self->m_cbs.onResize) {
            self->m_cbs.onResize(LOWORD(lParam), HIWORD(lParam));
        }
        return 0;

    case WM_DPICHANGED: {
        // Use Windows' suggested rectangle on DPI change (mixed-DPI monitors). [MS Docs]
        const RECT* suggested = reinterpret_cast<RECT*>(lParam);
        ::SetWindowPos(hWnd, nullptr,
            suggested->left, suggested->top,
            suggested->right - suggested->left,
            suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        if (self && self->m_cbs.onDpiChanged) {
            const UINT dpi = HIWORD(wParam);
            self->m_cbs.onDpiChanged(dpi, dpi);
        }
        return 0;
    }

    case WM_DROPFILES: {
        if (self && self->m_cbs.onFileDrop) {
            HDROP hDrop = reinterpret_cast<HDROP>(wParam);
            const UINT count = ::DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
            std::vector<std::wstring> files;
            files.reserve(count);
            for (UINT i = 0; i < count; ++i) {
                const UINT len = ::DragQueryFileW(hDrop, i, nullptr, 0);
                std::wstring path(len, L'\0');
                ::DragQueryFileW(hDrop, i, path.data(), len + 1);
                files.push_back(std::move(path));
            }
            ::DragFinish(hDrop);
            self->m_cbs.onFileDrop(*self, files); // WM_DROPFILES/DragQueryFileW/DragFinish
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        if (self && self->m_cbs.onClose) self->m_cbs.onClose();
        ::DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
