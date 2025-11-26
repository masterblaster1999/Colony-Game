#include "WinApp.h"
#include <shellscalingapi.h>   // optional, for DPI helpers if you later use them
#pragma comment(lib, "Shcore.lib")

WinApp* WinApp::s_self = nullptr;

// Prefer manifest for PMv2; this is just a best-effort runtime fallback.
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

// Helper: compute window rectangle for a desired client rect at current system DPI.
static void ComputeWindowRectForClient(SIZE desiredClient, DWORD style, DWORD exStyle, RECT& outRect) {
    RECT r{ 0, 0, desiredClient.cx, desiredClient.cy };

    HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    auto pGetDpiForSystem = reinterpret_cast<UINT (WINAPI*)()>(
        ::GetProcAddress(user32, "GetDpiForSystem"));
    auto pAdjustWindowRectExForDpi = reinterpret_cast<BOOL (WINAPI*)(LPRECT,DWORD,BOOL,DWORD,UINT)>(
        ::GetProcAddress(user32, "AdjustWindowRectExForDpi"));

    if (pAdjustWindowRectExForDpi) {
        const UINT dpi = pGetDpiForSystem ? pGetDpiForSystem() : 96u;
        pAdjustWindowRectExForDpi(&r, style, FALSE, exStyle, dpi);     // DPI-aware version. :contentReference[oaicite:1]{index=1}
    } else {
        ::AdjustWindowRectEx(&r, style, FALSE, exStyle);               // Fallback for older OS. :contentReference[oaicite:2]{index=2}
    }
    outRect = r;
}

void WinApp::RegisterRawInput(bool noLegacy) {
    RAWINPUTDEVICE rids[2]{};

    // Keyboard
    rids[0].usUsagePage = 0x01;  // HID_USAGE_PAGE_GENERIC
    rids[0].usUsage     = 0x06;  // KEYBOARD
    rids[0].dwFlags     = (noLegacy ? RIDEV_NOLEGACY : 0);
    rids[0].hwndTarget  = m_hwnd;

    // Mouse
    rids[1].usUsagePage = 0x01;  // GENERIC
    rids[1].usUsage     = 0x02;  // MOUSE
    rids[1].dwFlags     = (noLegacy ? RIDEV_NOLEGACY : 0);
    rids[1].hwndTarget  = m_hwnd;

    ::RegisterRawInputDevices(rids, 2, sizeof(RAWINPUTDEVICE));       // enables WM_INPUT
}

bool WinApp::Create(const WinCreateDesc& desc, const Callbacks& cbs) {
    m_hInst = desc.hInstance ? desc.hInstance : ::GetModuleHandleW(nullptr);
    m_cbs   = cbs;

    if (desc.enableDpiFallback) EnablePerMonitorV2DpiFallback();

    WNDCLASSW wc{};
    wc.hInstance     = m_hInst;
    wc.lpszClassName = L"ColonyGameWindowClass";
    wc.lpfnWndProc   = &WinApp::WndProc;
    wc.hCursor       = ::LoadCursor(nullptr, IDC_ARROW);
    if (!::RegisterClassW(&wc)) return false;

    // Decide desired client size
    SIZE desiredClient = desc.clientSize;
    if (desiredClient.cx <= 0 || desiredClient.cy <= 0) {
        desiredClient.cx = desc.width;
        desiredClient.cy = desc.height;
    }

    // Compute the actual window rect for the desired client area at current DPI.
    RECT wr{};
    ComputeWindowRectForClient(desiredClient, desc.style, desc.exStyle, wr);

    m_hwnd = ::CreateWindowExW(
        desc.exStyle, wc.lpszClassName, desc.title, desc.style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr, m_hInst, nullptr);
    if (!m_hwnd) return false;

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
    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    return int(msg.wParam);
}

LRESULT CALLBACK WinApp::WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    WinApp* self = s_self;
    switch (m) {
    case WM_INPUT: {
        if (self && self->m_cbs.onRawInput) {
            UINT size = 0;
            ::GetRawInputData((HRAWINPUT)l, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
            std::unique_ptr<BYTE[]> buf(new BYTE[size]);
            if (::GetRawInputData((HRAWINPUT)l, RID_INPUT, buf.get(), &size, sizeof(RAWINPUTHEADER)) == size) {
                self->m_cbs.onRawInput(*reinterpret_cast<RAWINPUT*>(buf.get()));
            }
        }
        return 0;
    }
    case WM_SIZE:
        if (self && self->m_cbs.onResize) {
            self->m_cbs.onResize(LOWORD(l), HIWORD(l));
        }
        return 0;

    case WM_DPICHANGED: {
        // Use Windows' suggested rect to avoid scaling glitches on mixed-DPI setups. :contentReference[oaicite:3]{index=3}
        const RECT* suggested = reinterpret_cast<RECT*>(l);
        ::SetWindowPos(h, nullptr,
            suggested->left, suggested->top,
            suggested->right - suggested->left,
            suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        if (self && self->m_cbs.onDpiChanged) {
            const UINT dpi = HIWORD(w);
            self->m_cbs.onDpiChanged(dpi, dpi);
        }
        return 0;
    }

    case WM_CLOSE:
        if (self && self->m_cbs.onClose) self->m_cbs.onClose();
        ::DestroyWindow(h);
        return 0;

    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(h, m, w, l);
}
