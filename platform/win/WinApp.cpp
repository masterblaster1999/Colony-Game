#include "WinApp.h"
#include <shellscalingapi.h>   // AdjustWindowRectExForDpi, GetDpiForSystem
#include <shellapi.h>          // DragAcceptFiles, DragQueryFile, DragFinish
#include <cstdio>
#include <vector>
#include <string>
#include <memory>

#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "Shell32.lib")

WinApp* WinApp::s_self = nullptr;

// Prefer process DPI awareness in the manifest; API is a safety fallback. (MS recommends manifest.)
void WinApp::EnablePerMonitorV2DpiFallback(bool enable) {
    if (!enable) return;
    HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    using SetCtx = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
    if (auto fn = reinterpret_cast<SetCtx>(::GetProcAddress(user32, "SetProcessDpiAwarenessContext"))) {
        fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
    // Prefer manifest for DPI awareness; API fallback remains for older installs.
}

// If you also added fine-grained raw-input callbacks (onMouseRawDelta/onMouseWheel/onKeyRaw)
// in WinApp::Callbacks, define this macro (e.g., in your project settings) to enable the
// extra fan-out below.
// #define WINAPP_HAS_FINE_RAW_INPUT_CALLBACKS 1

#ifdef WINAPP_HAS_FINE_RAW_INPUT_CALLBACKS
// Map generic VKs to left/right variants when possible (for raw keyboard).
static inline unsigned short MapLeftRightVK(unsigned short vkey, unsigned short make, USHORT flags)
{
    const bool e0 = (flags & RI_KEY_E0) != 0;
    switch (vkey) {
        case VK_SHIFT:
            // MapVirtualKeyW with VSC->VK_EX distinguishes L/R Shift
            return static_cast<unsigned short>(::MapVirtualKeyW(make, MAPVK_VSC_TO_VK_EX));
        case VK_CONTROL:
            return e0 ? VK_RCONTROL : VK_LCONTROL;
        case VK_MENU: // ALT
            return e0 ? VK_RMENU : VK_LMENU;
        default:
            return vkey;
    }
}
#endif // WINAPP_HAS_FINE_RAW_INPUT_CALLBACKS

static void ComputeWindowRectForClient(SIZE desiredClient, DWORD style, DWORD exStyle, RECT& outRect) {
    RECT r{ 0, 0, desiredClient.cx, desiredClient.cy };
    HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    auto pGetDpiForSystem = reinterpret_cast<UINT (WINAPI*)()>(
        ::GetProcAddress(user32, "GetDpiForSystem"));
    auto pAdjustWindowRectExForDpi = reinterpret_cast<BOOL (WINAPI*)(LPRECT,DWORD,BOOL,DWORD,UINT)>(
        ::GetProcAddress(user32, "AdjustWindowRectExForDpi"));

    if (pAdjustWindowRectExForDpi) {
        const UINT dpi = pGetDpiForSystem ? pGetDpiForSystem() : 96u;
        pAdjustWindowRectExForDpi(&r, style, FALSE, exStyle, dpi);   // DPI-aware sizing
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
    rids[1].usUsagePage = 0x01;  // HID_USAGE_PAGE_GENERIC
    rids[1].usUsage     = 0x02;  // MOUSE
    rids[1].dwFlags     = (noLegacy ? RIDEV_NOLEGACY : 0) | RIDEV_DEVNOTIFY;
    rids[1].hwndTarget  = m_hwnd;

    ::RegisterRawInputDevices(rids, 2, sizeof(RAWINPUTDEVICE));      // enables WM_INPUT
}

void WinApp::EnableFileDrops(bool accept) {
    ::DragAcceptFiles(m_hwnd, accept ? TRUE : FALSE);                 // enables WM_DROPFILES
}

bool WinApp::Create(const WinCreateDesc& desc, const Callbacks& cbs) {
    s_self = this;
    m_cbs  = cbs;

    m_hInst = desc.hInstance ? desc.hInstance : ::GetModuleHandleW(nullptr);

    // High-DPI: manifest is preferred; this API is a fallback to PMv2 when needed.
    EnablePerMonitorV2DpiFallback(desc.highDPIAware);

    // Window class
    WNDCLASSW wc{};
    wc.hInstance     = m_hInst;
    wc.lpszClassName = L"ColonyGameWindowClass";
    wc.lpfnWndProc   = &WinApp::WndProc;
    wc.hCursor       = ::LoadCursor(nullptr, IDC_ARROW);
    if (!::RegisterClassW(&wc)) return false;

    // Style controls resize capability
    DWORD style  = desc.style;
    DWORD exStyle= desc.exStyle;
    if (!desc.resizable) {
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    } else {
        style |=  (WS_THICKFRAME | WS_MAXIMIZEBOX);
    }

    // Desired client size -> DPI-correct window rect
    SIZE desiredClient = desc.clientSize;
    if (desiredClient.cx <= 0 || desiredClient.cy <= 0) {
        desiredClient.cx = desc.width;
        desiredClient.cy = desc.height;
    }
    RECT wr{};
    ComputeWindowRectForClient(desiredClient, style, exStyle, wr);     // AdjustWindowRectExForDpi

    // Create window
    m_hwnd = ::CreateWindowExW(
        exStyle, wc.lpszClassName, desc.title, style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr, m_hInst, nullptr);
    if (!m_hwnd) return false;

    // Optional debug console
    if (desc.debugConsole) {
        if (!::AttachConsole(ATTACH_PARENT_PROCESS)) {
            ::AllocConsole();
        }
        FILE* f{};
        _wfreopen_s(&f, L"CONOUT$", L"w", stdout);
        _wfreopen_s(&f, L"CONOUT$", L"w", stderr);
        _wfreopen_s(&f, L"CONIN$",  L"r", stdin);
    }

    ::ShowWindow(m_hwnd, SW_SHOW);
    ::UpdateWindow(m_hwnd);

    // Input & DnD
    EnableFileDrops(true);                                             // DragAcceptFiles
    RegisterRawInput(desc.rawInputNoLegacy);                           // RegisterRawInputDevices

    if (m_cbs.onInit) m_cbs.onInit(*this);
    return true;
}

// Legacy wrappers
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
    LARGE_INTEGER freq{}, last{};
    ::QueryPerformanceFrequency(&freq);
    ::QueryPerformanceCounter(&last);

    MSG msg{};
    bool running = true;
    while (running) {
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = false; break; }
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        if (!running) break;

        LARGE_INTEGER now{};
        ::QueryPerformanceCounter(&now);
        const float dt = static_cast<float>(now.QuadPart - last.QuadPart) / static_cast<float>(freq.QuadPart);
        last = now;

        if (m_cbs.onUpdate) m_cbs.onUpdate(*this, dt);
        if (m_cbs.onRender) m_cbs.onRender(*this);
    }

    if (m_cbs.onShutdown) m_cbs.onShutdown(*this);
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK WinApp::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WinApp* self = s_self;
    switch (msg) {
    case WM_INPUT: {
        if (!self) return 0;

        // Retrieve the RAWINPUT packet
        UINT size = 0;
        ::GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
        if (!size) return 0;

        std::unique_ptr<BYTE[]> buf(new BYTE[size]);
        if (::GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buf.get(), &size, sizeof(RAWINPUTHEADER)) != size)
            return 0;

        RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(buf.get());

        // 1) Raw packet passthrough (if requested)
        if (self->m_cbs.onRawInput) {
            self->m_cbs.onRawInput(*raw);
        }

#ifdef WINAPP_HAS_FINE_RAW_INPUT_CALLBACKS
        // 2) Optional decoded fan-out to fine-grained callbacks
        if (raw->header.dwType == RIM_TYPEMOUSE) {
            const RAWMOUSE& m = raw->data.mouse;

            if (self->m_cbs.onMouseRawDelta) {
                self->m_cbs.onMouseRawDelta(*self, static_cast<int>(m.lLastX), static_cast<int>(m.lLastY));
            }
            if (self->m_cbs.onMouseWheel) {
                if (m.usButtonFlags & RI_MOUSE_WHEEL) {
                    const short delta = static_cast<short>(m.usButtonData);
                    self->m_cbs.onMouseWheel(*self, delta, /*horizontal*/false);
                }
                if (m.usButtonFlags & RI_MOUSE_HWHEEL) {
                    const short delta = static_cast<short>(m.usButtonData);
                    self->m_cbs.onMouseWheel(*self, delta, /*horizontal*/true);
                }
            }
        }
        else if (raw->header.dwType == RIM_TYPEKEYBOARD) {
            const RAWKEYBOARD& k = raw->data.keyboard;
            const bool keyUp   = (k.Flags & RI_KEY_BREAK) != 0;
            const bool keyDown = !keyUp;
            if (self->m_cbs.onKeyRaw) {
                unsigned short vkey = MapLeftRightVK(k.VKey, k.MakeCode, k.Flags);
                self->m_cbs.onKeyRaw(*self, vkey, keyDown);
            }
        }
#endif // WINAPP_HAS_FINE_RAW_INPUT_CALLBACKS

        return 0; // processed
    } // WM_INPUT

    case WM_SIZE: {
        if (self && self->m_cbs.onResize) {
            const int w = LOWORD(lParam);
            const int h = HIWORD(lParam);
            self->m_cbs.onResize(*self, w, h, 0.0f);
        }
        return 0;
    } // WM_SIZE

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
                if (!path.empty() && path.back() == L'\0') path.pop_back();
                files.push_back(std::move(path));
            }
            ::DragFinish(hDrop);
            self->m_cbs.onFileDrop(*self, files);
        }
        return 0;
    } // WM_DROPFILES

    case WM_DPICHANGED: {
        // Resize to Windows' suggested rectangle to avoid mixed‑DPI glitches.
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
    } // WM_DPICHANGED

    case WM_CLOSE:
        // Let the app decide shutdown order; post quit on destroy.
        ::DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
