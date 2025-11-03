// Centralized Windows header policy (defines WIN32_LEAN_AND_MEAN, NOMINMAX, STRICT
// and includes <windows.h> / <windowsx.h> once for the whole project).
// See platform/win/WinCommon.h for details.
#include "WinCommon.h"

#include "WinApp.h"

#include <shellscalingapi.h>  // SetProcessDpiAwareness / PROCESS_DPI_AWARENESS
#include <dwmapi.h>           // optional, not strictly required
#include <cassert>
#include <vector>
#include <string>

#pragma comment(lib, "Shcore.lib")  // DPI APIs
#pragma comment(lib, "Shell32.lib") // Drag&Drop APIs (DragAcceptFiles etc.)
// Dwmapi.lib would be needed if you use DWM calls (not required here)

// Helper: dynamically query newer DPI APIs
namespace {
    using SetProcessDpiAwarenessContext_t = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
    using GetDpiForWindow_t               = UINT (WINAPI*)(HWND);
    using AdjustWindowRectExForDpi_t      = BOOL (WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);

    SetProcessDpiAwarenessContext_t pSetProcessDpiAwarenessContext = nullptr;
    GetDpiForWindow_t               pGetDpiForWindow               = nullptr;
    AdjustWindowRectExForDpi_t      pAdjustWindowRectExForDpi      = nullptr;

    void LoadDpiProcs()
    {
        HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
        if (user32) {
            pSetProcessDpiAwarenessContext =
                reinterpret_cast<SetProcessDpiAwarenessContext_t>(::GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
            pGetDpiForWindow =
                reinterpret_cast<GetDpiForWindow_t>(::GetProcAddress(user32, "GetDpiForWindow"));
            pAdjustWindowRectExForDpi =
                reinterpret_cast<AdjustWindowRectExForDpi_t>(::GetProcAddress(user32, "AdjustWindowRectExForDpi"));
        }
    }

    UINT GetWindowDpi(HWND hwnd)
    {
        if (pGetDpiForWindow && hwnd) return pGetDpiForWindow(hwnd);
        HDC hdc = ::GetDC(hwnd ? hwnd : nullptr);
        UINT dpi = (UINT)::GetDeviceCaps(hdc, LOGPIXELSX);
        ::ReleaseDC(hwnd, hdc);
        return dpi ? dpi : 96;
    }

    void DebugAllocConsoleIfRequested(bool enabled)
    {
    #if defined(_DEBUG)
        if (enabled) {
            ::AllocConsole();
            FILE* out = nullptr;
            freopen_s(&out, "CONOUT$", "w", stdout);
            freopen_s(&out, "CONOUT$", "w", stderr);
            freopen_s(&out, "CONIN$",  "r", stdin);
            ::SetConsoleTitleW(L"ColonyGame Debug Console");
        }
    #else
        (void)enabled;
    #endif
    }
}

namespace winplat {

WinApp::~WinApp()
{
    destroyWindowInternal();
}

bool WinApp::registerClass()
{
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc   = &WinApp::WndProcThunk;
    wc.hInstance     = m_hinstance;
    wc.hCursor       = ::LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon         = ::LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName = m_className.c_str();
    return ::RegisterClassExW(&wc) != 0;
}

void WinApp::applyDPIAwareness()
{
    LoadDpiProcs();

    if (!m_desc.highDPIAware) {
        return;
    }

    // Try PerMonitorV2 first (Windows 10+)
    if (pSetProcessDpiAwarenessContext) {
        if (pSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
            return;
        }
        // fall through otherwise
    }

    // Fallback to system-level awareness (Win8.1+)
    HRESULT hr = ::SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    if (SUCCEEDED(hr)) return;

    // Last resort (Vista+): system DPI aware
    ::SetProcessDPIAware();
}

void WinApp::updateDPIMetrics(HWND forWindow)
{
    m_dpi = GetWindowDpi(forWindow);
    m_dpiScale = static_cast<float>(m_dpi) / 96.0f;
}

bool WinApp::create(const WinCreateDesc& desc, const Callbacks& cbs)
{
    m_desc = desc;
    m_cbs  = cbs;

    m_hinstance = ::GetModuleHandleW(nullptr);
    m_title     = desc.title;

    applyDPIAwareness();
    DebugAllocConsoleIfRequested(desc.debugConsole);

    if (!registerClass()) {
        ::MessageBoxW(nullptr, L"Failed to register window class.", L"Error", MB_ICONERROR);
        return false;
    }

    if (!createWindowInternal()) {
        ::MessageBoxW(nullptr, L"Failed to create window.", L"Error", MB_ICONERROR);
        return false;
    }

    if (m_desc.enableFileDrop) {
        enableFileDrop(true);
    }

    updateDPIMetrics(m_hwnd);
    if (m_cbs.onResize) {
        m_cbs.onResize(*this, m_clientW, m_clientH, m_dpiScale);
    }

    if (m_cbs.onInit) {
        if (!m_cbs.onInit(*this)) {
            destroyWindowInternal();
            return false;
        }
    }

    return true;
}

bool WinApp::createWindowInternal()
{
    // Desired client size -> window rect (respect DPI if API exists)
    RECT rect{0, 0, m_desc.clientSize.width, m_desc.clientSize.height};
    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!m_desc.resizable) {
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    }

    DWORD exStyle = WS_EX_APPWINDOW;

    if (pAdjustWindowRectExForDpi) {
        UINT dpi = GetWindowDpi(nullptr);
        pAdjustWindowRectExForDpi(&rect, style, FALSE, exStyle, dpi);
    } else {
        ::AdjustWindowRectEx(&rect, style, FALSE, exStyle);
    }

    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;

    m_hwnd = ::CreateWindowExW(
        exStyle,
        m_className.c_str(),
        m_title.c_str(),
        style,
        CW_USEDEFAULT, CW_USEDEFAULT, w, h,
        nullptr, nullptr, m_hinstance, this);

    if (!m_hwnd) return false;

    ::ShowWindow(m_hwnd, SW_SHOW);
    ::UpdateWindow(m_hwnd);

    // Cache client size
    RECT cr{};
    ::GetClientRect(m_hwnd, &cr);
    m_clientW = static_cast<int>(cr.right - cr.left);
    m_clientH = static_cast<int>(cr.bottom - cr.top);
    return true;
}

void WinApp::destroyWindowInternal()
{
    if (m_hwnd) {
        if (m_cbs.onShutdown) {
            m_cbs.onShutdown(*this);
        }
        ::DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (!m_className.empty()) {
        ::UnregisterClassW(m_className.c_str(), m_hinstance);
    }
}

void WinApp::requestQuit(int exitCode)
{
    ::PostQuitMessage(exitCode);
}

void WinApp::resizeClientInternal(int w, int h)
{
    m_clientW = w;
    m_clientH = h;
    if (m_cbs.onResize) {
        m_cbs.onResize(*this, w, h, m_dpiScale);
    }
}

void WinApp::setTitleInternal(const std::wstring& title)
{
    ::SetWindowTextW(m_hwnd, title.c_str());
}

void WinApp::enableFileDrop(bool enable)
{
    ::DragAcceptFiles(m_hwnd, enable ? TRUE : FALSE);
}

int WinApp::run()
{
    m_running = true;
    m_prevTick = clock::now();

    MSG msg{};
    while (m_running) {
        // Non‑blocking pump
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                m_running = false;
                return static_cast<int>(msg.wParam);
            }
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }

        // Compute dt
        auto now = clock::now();
        float dt = std::chrono::duration<float>(now - m_prevTick).count();
        m_prevTick = now;

        // Reset input deltas each frame
        m_inputDelta = {};

        if (m_cbs.onUpdate) m_cbs.onUpdate(*this, dt);
        if (m_cbs.onRender) m_cbs.onRender(*this);
    }

    return 0;
}

void WinApp::toggleBorderlessFullscreen()
{
    if (!m_hwnd) return;

    static WINDOWPLACEMENT prevPlacement{ sizeof(WINDOWPLACEMENT) };

    // Use *Ptr variants for 64-bit correctness.
    DWORD style = static_cast<DWORD>(::GetWindowLongPtrW(m_hwnd, GWL_STYLE));
    if (!m_fullscreenBorderless) {
        // Save placement
        ::GetWindowPlacement(m_hwnd, &prevPlacement);

        // Remove decorations
        ::SetWindowLongPtrW(m_hwnd, GWL_STYLE, static_cast<LONG_PTR>(style & ~(WS_OVERLAPPEDWINDOW)));

        // Size to monitor
        HMONITOR hMon = ::MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{ sizeof(MONITORINFO) };
        ::GetMonitorInfoW(hMon, &mi);

        ::SetWindowPos(m_hwnd, HWND_TOP,
                       mi.rcMonitor.left, mi.rcMonitor.top,
                       mi.rcMonitor.right - mi.rcMonitor.left,
                       mi.rcMonitor.bottom - mi.rcMonitor.top,
                       SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

        m_fullscreenBorderless = true;
    } else {
        // Restore decorations
        ::SetWindowLongPtrW(m_hwnd, GWL_STYLE, static_cast<LONG_PTR>(style | WS_OVERLAPPEDWINDOW));
        ::SetWindowPlacement(m_hwnd, &prevPlacement);
        ::SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                       SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        m_fullscreenBorderless = false;
    }
}

LRESULT CALLBACK WinApp::WndProcThunk(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WinApp* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<LPCREATESTRUCTW>(lParam);
        self = reinterpret_cast<WinApp*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hWnd;
    } else {
        self = reinterpret_cast<WinApp*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }
    if (self) {
        return self->wndProc(hWnd, msg, wParam, lParam);
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT WinApp::wndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Give the app a chance first
    if (m_cbs.onMessage) {
        if (m_cbs.onMessage(*this, msg, wParam, lParam)) {
            return 0;
        }
    }

    switch (msg) {
    case WM_CLOSE:
        ::PostQuitMessage(0);
        return 0;

    case WM_DESTROY:
        return 0;

    case WM_SIZE: {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        resizeClientInternal(w, h);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        auto mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = m_desc.minClientWidth;
        mmi->ptMinTrackSize.y = m_desc.minClientHeight;
        return 0;
    }

    case WM_DPICHANGED: {
        m_dpi = HIWORD(wParam);
        m_dpiScale = static_cast<float>(m_dpi) / 96.0f;
        RECT* suggested = reinterpret_cast<RECT*>(lParam);
        ::SetWindowPos(hWnd, nullptr,
                       suggested->left, suggested->top,
                       suggested->right - suggested->left,
                       suggested->bottom - suggested->top,
                       SWP_NOZORDER | SWP_NOACTIVATE);
        // Client rect will update via WM_SIZE
        return 0;
    }

    case WM_INPUT: { // optional raw mouse delta
        UINT size = 0;
        ::GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
        std::vector<uint8_t> buffer(size);
        if (size != 0 &&
            ::GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, buffer.data(), &size, sizeof(RAWINPUTHEADER)) == size) {
            RAWINPUT* ri = reinterpret_cast<RAWINPUT*>(buffer.data());
            if (ri->header.dwType == RIM_TYPEMOUSE) {
                m_inputDelta.mouseDX += ri->data.mouse.lLastX;
                m_inputDelta.mouseDY += ri->data.mouse.lLastY;
                if (ri->data.mouse.usButtonFlags & RI_MOUSE_WHEEL) {
                    m_inputDelta.wheel += static_cast<SHORT>(ri->data.mouse.usButtonData);
                }
            }
        }
        return 0;
    }

    case WM_MOUSEWHEEL:
        m_inputDelta.wheel += GET_WHEEL_DELTA_WPARAM(wParam);
        return 0;

    case WM_SYSKEYDOWN:
        if (wParam == VK_RETURN && (HIWORD(lParam) & KF_ALTDOWN)) {
            toggleBorderlessFullscreen();
            return 0;
        }
        break;

    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        UINT count = ::DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        std::vector<std::wstring> files;
        files.reserve(count);
        for (UINT i = 0; i < count; ++i) {
            UINT len = ::DragQueryFileW(drop, i, nullptr, 0) + 1;
            std::wstring path;
            path.resize(len);
            UINT written = ::DragQueryFileW(drop, i, path.data(), len);
            path.resize(written);
            files.emplace_back(std::move(path));
        }
        ::DragFinish(drop);
        if (!files.empty() && m_cbs.onFileDrop) {
            m_cbs.onFileDrop(*this, files);
        }
        return 0;
    }

    default:
        break;
    }

    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

} // namespace winplat

