#include "WinApp.h"

#include <shellapi.h>      // DragAcceptFiles, DragQueryFile
#include <profileapi.h>    // QueryPerformanceCounter/Frequency
#include <shellscalingapi.h>
#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "Shell32.lib")

WinApp* WinApp::s_self = nullptr;

// Prefer manifest (Per-Monitor V2). This is a best-effort runtime fallback.
void WinApp::EnablePerMonitorV2DpiFallback() {
    HMODULE user32 = ::LoadLibraryW(L"user32.dll");
    if (user32) {
        using SetCtx = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
        if (auto fn = reinterpret_cast<SetCtx>(::GetProcAddress(user32, "SetProcessDpiAwarenessContext"))) {
            fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
        ::FreeLibrary(user32);
    }
}

void WinApp::EnableDebugConsoleIfRequested(bool enable) {
    if (!enable) return;
    if (::AllocConsole()) { // allocates a console for a GUI app
        // Hook std handles; ignore failures if redirection not needed.
        FILE* f{};
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        freopen_s(&f, "CONIN$",  "r", stdin);
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
    rids[0].usUsagePage = 0x01;
    rids[0].usUsage     = 0x06;  // keyboard
    rids[0].dwFlags     = (noLegacy ? RIDEV_NOLEGACY : 0) | RIDEV_DEVNOTIFY;
    rids[0].hwndTarget  = m_hwnd;

    // Mouse
    rids[1].usUsagePage = 0x01;
    rids[1].usUsage     = 0x02;  // mouse
    rids[1].dwFlags     = (noLegacy ? RIDEV_NOLEGACY : 0) | RIDEV_DEVNOTIFY;
    rids[1].hwndTarget  = m_hwnd;

    ::RegisterRawInputDevices(rids, 2, sizeof(RAWINPUTDEVICE));
}

bool WinApp::Create(const WinCreateDesc& desc, const Callbacks& cbs) {
    s_self = this;
    m_hInst = desc.hInstance ? desc.hInstance : ::GetModuleHandleW(nullptr);
    m_cbs   = cbs;

    if (desc.highDPIAware && desc.enableDpiFallback) {
        EnablePerMonitorV2DpiFallback();
    }
    EnableDebugConsoleIfRequested(desc.debugConsole);

    // Window class
    WNDCLASSW wc{};
    wc.hInstance     = m_hInst;
    wc.lpszClassName = L"ColonyGameWindowClass";
    wc.lpfnWndProc   = &WinApp::WndProc;
    wc.hCursor       = ::LoadCursor(nullptr, IDC_ARROW);
    if (!::RegisterClassW(&wc)) return false;

    // Compute style from resizable flag
    DWORD style  = desc.style;
    DWORD exStyle= desc.exStyle;
    if (!desc.resizable) {
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX); // drop resize/mx box if requested
    }

    // Desired client size -> window rect
    SIZE desiredClient = desc.clientSize;
    if (desiredClient.cx <= 0 || desiredClient.cy <= 0) {
        desiredClient.cx = desc.width;
        desiredClient.cy = desc.height;
    }
    RECT wr{};
    ComputeWindowRectForClient(desiredClient, style, exStyle, wr);

    // Create window
    m_hwnd = ::CreateWindowExW(
        exStyle, wc.lpszClassName, desc.title, style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr, m_hInst, nullptr);
    if (!m_hwnd) return false;

    // Accept drag & drop of files
    ::DragAcceptFiles(m_hwnd, TRUE);

    ::ShowWindow(m_hwnd, SW_SHOW);
    ::UpdateWindow(m_hwnd);

    RegisterRawInput(desc.rawInputNoLegacy);

    if (m_cbs.onInit) m_cbs.onInit(*this);
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
    LARGE_INTEGER freq{}, t0{}, t1{};
    ::QueryPerformanceFrequency(&freq);
    ::QueryPerformanceCounter(&t0);

    MSG msg{};
    while (true) {
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                if (m_cbs.onShutdown) m_cbs.onShutdown(*this);
                return (int)msg.wParam;
            }
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }

        // Per-frame callbacks
        ::QueryPerformanceCounter(&t1);
        float dt = float(double(t1.QuadPart - t0.QuadPart) / double(freq.QuadPart));
        t0 = t1;

        RECT rc{};
        ::GetClientRect(m_hwnd, &rc);
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;

        if (m_cbs.onUpdate) m_cbs.onUpdate(*this, w, h, dt);
        if (m_cbs.onRender) m_cbs.onRender(*this, w, h, dt);
    }
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
    case WM_DROPFILES: {
        if (self && self->m_cbs.onFileDrop) {
            HDROP hDrop = (HDROP)wParam;
            UINT count = ::DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
            std::vector<std::wstring> files;
            files.reserve(count);
            for (UINT i = 0; i < count; ++i) {
                UINT len = ::DragQueryFileW(hDrop, i, nullptr, 0);
                std::wstring path(len, L'\0');
                ::DragQueryFileW(hDrop, i, path.data(), len + 1);
                // DragQueryFileW writes a null terminator; adjust size.
                if (!path.empty() && path.back() == L'\0') path.pop_back();
                files.push_back(std::move(path));
            }
            ::DragFinish(hDrop);
            self->m_cbs.onFileDrop(*self, files);
        }
        return 0;
    }
    case WM_SIZE: {
        if (self && self->m_cbs.onResize) {
            const int w = LOWORD(lParam);
            const int h = HIWORD(lParam);
            self->m_cbs.onResize(*self, w, h, 0.0f); // dt not applicable here
        }
        return 0;
    }
    case WM_DPICHANGED: {
        // Resize to suggested rect to avoid mixed-DPI glitches.
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
