// src/app/ColonySlice.cpp
//
// Minimal Win32 + D3D11 app shell for ColonySlice:
// - WinMain and window creation
// - High-resolution timing & title bar FPS / ms stats
// - D3D11 device + flip-model swap chain + sRGB render target
// - Basic input state (keyboard/mouse), simple toggles
//
// Build: link against d3d11.lib, dxgi.lib, user32.lib, gdi32.lib, ole32.lib
// (CMake example target should add these libs.)
//
// This file intentionally avoids engine/editor dependencies so you can
// compile & run a blank slice that clears the screen and shows stats.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#ifdef _DEBUG
#include <d3d11sdklayers.h>
#endif
#include <wrl/client.h>
#include <string>
#include <cstdio>
#include <cstdint>
#include <array>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;

static const wchar_t* kAppTitle = L"ColonySlice";

// -------------------------------------------------------------------------------------------------
// Simple high-resolution timer (QueryPerformanceCounter).
// -------------------------------------------------------------------------------------------------
struct HiTimer {
    LARGE_INTEGER freq{};
    LARGE_INTEGER last{};
    double dtSec{0.0};

    void init() {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&last);
    }
    void tick() {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        dtSec = double(now.QuadPart - last.QuadPart) / double(freq.QuadPart);
        last = now;
        // Clamp dt to avoid giant steps if the debugger halted us.
        if (dtSec > 0.25) dtSec = 0.25;
    }
};

// -------------------------------------------------------------------------------------------------
// Input (very small state).
// -------------------------------------------------------------------------------------------------
struct InputState {
    std::array<bool, 256> key{};
    std::array<bool, 256> prev{};
    int mouseX{0}, mouseY{0};
    int mouseDX{0}, mouseDY{0};
    bool mouseL{false}, mouseR{false};

    void beginFrame() { mouseDX = mouseDY = 0; }
    void endFrame()   { prev = key; }
    bool pressedOnce(int vk) const { return key[vk] && !prev[vk]; }
} gInput;

// -------------------------------------------------------------------------------------------------
// D3D objects & helpers.
// -------------------------------------------------------------------------------------------------
struct D3DContext {
    ComPtr<ID3D11Device>            device;
    ComPtr<ID3D11DeviceContext>     ctx;
    ComPtr<IDXGISwapChain1>         swap;
    ComPtr<ID3D11RenderTargetView>  rtv;
#ifdef _DEBUG
    ComPtr<ID3D11Debug>             debug;
#endif
    D3D11_VIEWPORT                  vp{};
    UINT                            fbWidth{1280};
    UINT                            fbHeight{720};
} gGfx;

static HWND gHwnd = nullptr;
static bool gRunning = true;
static bool gPaused  = false;
static bool gVSync   = true;

// Forward decls.
static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
static bool InitWindow(HINSTANCE, int, UINT, UINT);
static bool InitD3D(HWND, UINT, UINT);
static bool CreateBackbuffer(UINT w, UINT h);
static void DestroyBackbuffer();
static void OnResize(UINT w, UINT h);
static void UpdateAndRender(double dt);
static void UpdateTitleBar(double dt);

// -------------------------------------------------------------------------------------------------
// Minimal error popup with HRESULT.
// -------------------------------------------------------------------------------------------------
static void FatalHR(const wchar_t* where, HRESULT hr) {
    wchar_t buf[512];
    swprintf_s(buf, L"%s failed (0x%08X)", where, hr);
    MessageBoxW(gHwnd ? gHwnd : GetDesktopWindow(), buf, kAppTitle, MB_ICONERROR | MB_OK);
    ExitProcess(uint32_t(hr));
}

// -------------------------------------------------------------------------------------------------
int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR /*lpCmdLine*/, int nCmdShow) {
    // Keep it simple: set System-DPI awareness via API (manifest recommended for production).
    // Must be called before creating any HWNDs.
    // https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setprocessdpiaware
    SetProcessDPIAware();

    const UINT initW = 1280, initH = 720;
    if (!InitWindow(hInst, nCmdShow, initW, initH)) return -1;
    if (!InitD3D(gHwnd, initW, initH))              return -2;

    HiTimer timer;
    timer.init();

    // Main loop (PeekMessage-based "game loop"): process pending messages,
    // otherwise run our tick (update + render).
    // See: https://learn.microsoft.com/windows/win32/winmsg/using-messages-and-message-queues
    MSG msg = {};
    while (gRunning) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { gRunning = false; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!gRunning) break;

        // Frame begin
        gInput.beginFrame();
        timer.tick();

        if (!gPaused) {
            UpdateAndRender(timer.dtSec);
        } else {
            // Even when paused, clear so the window stays responsive.
            UpdateAndRender(0.0);
        }

        UpdateTitleBar(timer.dtSec);
        gInput.endFrame();
    }

#ifdef _DEBUG
    if (gGfx.debug) {
        gGfx.debug->ReportLiveDeviceObjects(D3D11_RLDO_SUMMARY);
    }
#endif

    return 0;
}

// -------------------------------------------------------------------------------------------------
// Window creation (RegisterClassExW + CreateWindowExW).
// https://learn.microsoft.com/windows/win32/learnwin32/creating-a-window
// -------------------------------------------------------------------------------------------------
static bool InitWindow(HINSTANCE hInst, int nCmdShow, UINT w, UINT h) {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"ColonySliceWindowClass";

    if (!RegisterClassExW(&wc)) return false;

    DWORD style = WS_OVERLAPPEDWINDOW;
    RECT  rc{ 0, 0, LONG(w), LONG(h) };
    AdjustWindowRect(&rc, style, FALSE);

    gHwnd = CreateWindowExW(
        0, wc.lpszClassName, kAppTitle,
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInst, nullptr);

    if (!gHwnd) return false;

    ShowWindow(gHwnd, nCmdShow);
    UpdateWindow(gHwnd);
    return true;
}

// -------------------------------------------------------------------------------------------------
// D3D11 device + flip-model swap chain + sRGB RTV.
//  - Device first (D3D11CreateDevice), then factory->CreateSwapChainForHwnd.
//  - Backbuffer RTV created with *_SRGB format while swapchain is *_UNORM (DXGI allows this).
//     https://learn.microsoft.com/windows/win32/direct3ddxgi/converting-data-color-space
//     https://walbourn.github.io/care-and-feeding-of-modern-swapchains/
// -------------------------------------------------------------------------------------------------
static bool InitD3D(HWND hwnd, UINT w, UINT h) {
    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT; // good default; interop-friendly
#ifdef _DEBUG
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;  // Requires SDK layers installed.
#endif

    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL gotLevel{};

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags,
        levels, _countof(levels), D3D11_SDK_VERSION,
        gGfx.device.GetAddressOf(), &gotLevel, gGfx.ctx.GetAddressOf());
#ifdef _DEBUG
    // If debug layer is missing, retry without it.
    if (FAILED(hr) && (createFlags & D3D11_CREATE_DEVICE_DEBUG)) {
        createFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags,
                               levels, _countof(levels), D3D11_SDK_VERSION,
                               gGfx.device.GetAddressOf(), &gotLevel, gGfx.ctx.GetAddressOf());
    }
#endif
    if (FAILED(hr)) FatalHR(L"D3D11CreateDevice", hr);

#ifdef _DEBUG
    // Optional: hook debug interface if present.
    (void)gGfx.device.As(&gGfx.debug);
#endif

    // Get factory from device.
    ComPtr<IDXGIDevice> dxgiDevice;
    hr = gGfx.device.As(&dxgiDevice);
    if (FAILED(hr)) FatalHR(L"Query IDXGIDevice", hr);

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
    if (FAILED(hr)) FatalHR(L"IDXGIDevice::GetAdapter", hr);

    ComPtr<IDXGIFactory2> factory2;
    {
        ComPtr<IDXGIFactory1> factory1;
        hr = adapter->GetParent(IID_PPV_ARGS(&factory1));
        if (FAILED(hr)) FatalHR(L"IDXGIAdapter::GetParent", hr);
        hr = factory1.As(&factory2); // try upgrade to 1.2+
        if (FAILED(hr)) FatalHR(L"Query IDXGIFactory2", hr);
    }

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width       = w;
    sd.Height      = h;
    sd.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;   // flip-model + sRGB RTV on top
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;                            // double-buffer
    sd.SampleDesc  = { 1, 0 };
    sd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Scaling     = DXGI_SCALING_STRETCH;
    sd.AlphaMode   = DXGI_ALPHA_MODE_IGNORE;
    sd.Flags       = 0; // consider DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING for vsync-off on Win10+ (optional)

    ComPtr<IDXGISwapChain1> swap;
    hr = factory2->CreateSwapChainForHwnd(
        gGfx.device.Get(), hwnd, &sd, nullptr, nullptr, swap.GetAddressOf());
    if (FAILED(hr)) FatalHR(L"CreateSwapChainForHwnd", hr);

    gGfx.swap = swap;

    // Disable Alt+Enter default fullscreen toggle (we'll manage windowed mode ourselves).
    factory2->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    gGfx.fbWidth = w;
    gGfx.fbHeight = h;

    if (!CreateBackbuffer(w, h)) return false;
    return true;
}

static bool CreateBackbuffer(UINT w, UINT h) {
    DestroyBackbuffer();

    ComPtr<ID3D11Texture2D> backbuf;
    HRESULT hr = gGfx.swap->GetBuffer(0, IID_PPV_ARGS(&backbuf));
    if (FAILED(hr)) FatalHR(L"IDXGISwapChain::GetBuffer", hr);

    // Create SRGB RTV over an UNORM swap-chain buffer (DXGI special-case).
    // https://learn.microsoft.com/windows/win32/direct3ddxgi/converting-data-color-space
    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

    hr = gGfx.device->CreateRenderTargetView(backbuf.Get(), &rtvDesc, gGfx.rtv.GetAddressOf());
    if (FAILED(hr)) FatalHR(L"CreateRenderTargetView (sRGB RTV)", hr);

    gGfx.vp = {};
    gGfx.vp.TopLeftX = 0.0f;
    gGfx.vp.TopLeftY = 0.0f;
    gGfx.vp.Width    = static_cast<float>(w);
    gGfx.vp.Height   = static_cast<float>(h);
    gGfx.vp.MinDepth = 0.0f;
    gGfx.vp.MaxDepth = 1.0f;

    return true;
}

static void DestroyBackbuffer() {
    if (gGfx.ctx) gGfx.ctx->OMSetRenderTargets(0, nullptr, nullptr);
    gGfx.rtv.Reset();
}

static void OnResize(UINT w, UINT h) {
    if (!gGfx.swap) return;
    if (w == 0 || h == 0) return; // minimized

    DestroyBackbuffer();
    gGfx.fbWidth = w;
    gGfx.fbHeight = h;

    HRESULT hr = gGfx.swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) FatalHR(L"IDXGISwapChain::ResizeBuffers", hr);

    CreateBackbuffer(w, h);
}

// -------------------------------------------------------------------------------------------------
// Update + Render. Replace with your world/camera logic later.
// -------------------------------------------------------------------------------------------------
static void UpdateAndRender(double dt) {
    // Simple toggles.
    if (gInput.pressedOnce('V')) gVSync = !gVSync;
    if (gInput.pressedOnce('P')) gPaused = !gPaused;
    // 'R' is intended to pulse a "regenerate world" once you hook up worldgen.
    bool pulseRegenerate = gInput.pressedOnce('R');
    (void)pulseRegenerate; // not used yet

    // Placeholder camera-ish input (RMB look deltas are captured in WM_MOUSEMOVE).
    const float moveSpeed = 5.0f;
    float dx = 0.0f, dy = 0.0f;
    if (gInput.key['A']) dx -= moveSpeed * float(dt);
    if (gInput.key['D']) dx += moveSpeed * float(dt);
    if (gInput.key['W']) dy += moveSpeed * float(dt);
    if (gInput.key['S']) dy -= moveSpeed * float(dt);
    (void)dx; (void)dy;

    // Clear & present.
    const float clearSRGB[4] = { 0.075f, 0.075f, 0.10f, 1.0f }; // dark background
    gGfx.ctx->OMSetRenderTargets(1, gGfx.rtv.GetAddressOf(), nullptr);
    gGfx.ctx->RSSetViewports(1, &gGfx.vp);
    gGfx.ctx->ClearRenderTargetView(gGfx.rtv.Get(), clearSRGB);

    // Draw calls for your terrain/mesh will go here.

    gGfx.swap->Present(gVSync ? 1 : 0, 0);
}

// -------------------------------------------------------------------------------------------------
// Title-bar stats (updated ~4x a second).
// -------------------------------------------------------------------------------------------------
static void UpdateTitleBar(double dt) {
    static double accum = 0.0;
    static uint32_t frames = 0;

    accum += dt;
    frames++;

    if (accum >= 0.25) {
        double fps = frames / accum;
        double ms  = (accum / frames) * 1000.0;

        wchar_t title[256];
        swprintf_s(title, L"%s  |  %ux%u  |  %.1f FPS (%.2f ms)  |  VSync: %s%s",
                   kAppTitle, gGfx.fbWidth, gGfx.fbHeight,
                   fps, ms, gVSync ? L"On" : L"Off",
                   gPaused ? L"  |  Paused" : L"");

        SetWindowTextW(gHwnd, title);
        accum = 0.0;
        frames = 0;
    }
}

// -------------------------------------------------------------------------------------------------
// Window procedure: input + resize + destroy.
// -------------------------------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_SIZE: {
            UINT w = LOWORD(lParam);
            UINT h = HIWORD(lParam);
            OnResize(w, h);
            return 0;
        }

        case WM_ACTIVATE:
            // If deactivated, you might want to pause or release mouse capture.
            return 0;

        // Keyboard
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (!(lParam & (1 << 30))) { // ignore auto-repeat for "pressedOnce" edge
                if (wParam < 256) gInput.key[static_cast<uint8_t>(wParam)] = true;
            }
            if (wParam == VK_ESCAPE) {
                // Escape closes the app for now (replace with pause/menu later).
                PostMessageW(hWnd, WM_CLOSE, 0, 0);
            }
            return 0;

        case WM_KEYUP:
        case WM_SYSKEYUP:
            if (wParam < 256) gInput.key[static_cast<uint8_t>(wParam)] = false;
            return 0;

        // Mouse
        case WM_LBUTTONDOWN: SetCapture(hWnd); gInput.mouseL = true; return 0;
        case WM_LBUTTONUP:   ReleaseCapture(); gInput.mouseL = false; return 0;
        case WM_RBUTTONDOWN: SetCapture(hWnd); gInput.mouseR = true; return 0;
        case WM_RBUTTONUP:   ReleaseCapture(); gInput.mouseR = false; return 0;

        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            gInput.mouseDX += (x - gInput.mouseX);
            gInput.mouseDY += (y - gInput.mouseY);
            gInput.mouseX = x; gInput.mouseY = y;
            return 0;
        }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
