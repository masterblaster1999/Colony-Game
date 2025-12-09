#ifdef _WIN32
#include "SwapchainWin32.h"

#include <cassert>
#include <algorithm>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")

using namespace cg;

static void throw_if_failed(HRESULT hr, const char* what)
{
    if (FAILED(hr)) {
        throw std::runtime_error(what);
    }
}

RECT SwapchainWin32::window_client_rect_(HWND hwnd)
{
    RECT rc{};
    GetClientRect(hwnd, &rc);
    return rc;
}

bool SwapchainWin32::initialize(ID3D12CommandQueue* queue, const SwapchainDesc& d)
{
    assert(queue && d.hwnd);
    m_queue         = queue;
    m_hwnd          = d.hwnd;
    m_format        = d.format;
    m_bufferCount   = std::max<UINT>(2, d.bufferCount);
    m_vsync         = d.startVsyncOn;

    // Determine initial size
    if (d.width == 0 || d.height == 0)
    {
        RECT rc = window_client_rect_(m_hwnd);
        m_width  = std::max<UINT>(1, rc.right - rc.left);
        m_height = std::max<UINT>(1, rc.bottom - rc.top);
    }
    else {
        m_width  = d.width;
        m_height = d.height;
    }

    // Save current windowed style/rect for later restoration
    m_windowedStyle = static_cast<DWORD>(GetWindowLongPtr(m_hwnd, GWL_STYLE));
    GetWindowRect(m_hwnd, &m_windowedRect);

    create_factory_();
    create_swapchain_();

    return true;
}

void SwapchainWin32::create_factory_()
{
    UINT flags = 0;
#if defined(_DEBUG)
    // If you have the D3D12 debug layer enabled, DXGI debug can be handy too
    flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
    ComPtr<IDXGIFactory4> factory4;
    throw_if_failed(CreateDXGIFactory2(flags, IID_PPV_ARGS(factory4.ReleaseAndGetAddressOf())),
                    "CreateDXGIFactory2 failed");
    m_factory = factory4;

    // Disable DXGI's built-in Alt+Enter so we can implement borderless ourselves
    m_factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);

    // Check for tearing support via IDXGIFactory5::CheckFeatureSupport
    BOOL allowTearing = FALSE;
    if (ComPtr<IDXGIFactory5> factory5; SUCCEEDED(m_factory.As(&factory5)))
    {
        if (SUCCEEDED(factory5->CheckFeatureSupport(
                DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                &allowTearing, sizeof(allowTearing))))
        {
            m_tearingSupported = (allowTearing == TRUE);
        }
    }
}

void SwapchainWin32::create_swapchain_()
{
    // Release previous swapchain if any (for recreate path)
    m_swapchain.Reset();

    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width       = m_width;
    scd.Height      = m_height;
    scd.Format      = m_format;
    scd.Stereo      = FALSE;
    scd.SampleDesc  = {1, 0}; // Flip model requires no MSAA
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = m_bufferCount;
    scd.Scaling     = DXGI_SCALING_NONE;
    scd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;  // Flip model (recommended)
    scd.AlphaMode   = DXGI_ALPHA_MODE_UNSPECIFIED;
    scd.Flags       = m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    ComPtr<IDXGISwapChain1> swapchain1;
    // NOTE: For D3D12, the first parameter is the *command queue*
    throw_if_failed(
        m_factory->CreateSwapChainForHwnd(
            m_queue.Get(), m_hwnd, &scd, nullptr, nullptr, swapchain1.ReleaseAndGetAddressOf()),
        "CreateSwapChainForHwnd failed");

    // We stay windowed (borderless when toggled); do not call SetFullscreenState(true).

    throw_if_failed(swapchain1.As(m_swapchain.ReleaseAndGetAddressOf()),
                    "Query IDXGISwapChain3 failed");

    m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();
}

void SwapchainWin32::resize(UINT width, UINT height)
{
    if (!m_swapchain) return;

    width  = std::max<UINT>(1, width);
    height = std::max<UINT>(1, height);

    if (width == m_width && height == m_height) return;

    UINT flags = m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    // Release any per-backbuffer resources in the engine **before** this call.
    HRESULT hr = m_swapchain->ResizeBuffers(m_bufferCount, width, height, m_format, flags);

    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        // Let the engine handle device loss as appropriate.
        throw std::runtime_error("Device lost during ResizeBuffers");
    } else {
        throw_if_failed(hr, "ResizeBuffers failed");
    }

    m_width      = width;
    m_height     = height;
    m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();
}

void SwapchainWin32::toggle_borderless()
{
    m_isBorderless = !m_isBorderless;

    if (m_isBorderless)
    {
        // Save windowed placement
        GetWindowRect(m_hwnd, &m_windowedRect);
        m_windowedStyle = static_cast<DWORD>(GetWindowLongPtr(m_hwnd, GWL_STYLE));

        // Switch to borderless (WS_POPUP) covering the monitor
        HMONITOR hmon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{ sizeof(MONITORINFO) };
        GetMonitorInfo(hmon, &mi);

        SetWindowLongPtr(m_hwnd, GWL_STYLE, (m_windowedStyle & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
        SetWindowPos(
            m_hwnd, HWND_TOP,
            mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right  - mi.rcMonitor.left,
            mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

        ShowWindow(m_hwnd, SW_SHOW);
    }
    else
    {
        // Restore windowed style/placement
        SetWindowLongPtr(m_hwnd, GWL_STYLE, m_windowedStyle);
        SetWindowPos(
            m_hwnd, nullptr,
            m_windowedRect.left, m_windowedRect.top,
            m_windowedRect.right  - m_windowedRect.left,
            m_windowedRect.bottom - m_windowedRect.top,
            SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

        ShowWindow(m_hwnd, SW_SHOW);
    }
}

void SwapchainWin32::present()
{
    if (!m_swapchain) return;

    // DXGI_PRESENT_ALLOW_TEARING can only be used with syncInterval==0 and when windowed.
    UINT syncInterval = m_vsync ? 1 : 0;

    BOOL isFullscreen = FALSE;
    m_swapchain->GetFullscreenState(&isFullscreen, nullptr);

    UINT flags = 0;
    if (!m_vsync && m_tearingSupported && !isFullscreen) {
        flags |= DXGI_PRESENT_ALLOW_TEARING;
    }

    HRESULT hr = m_swapchain->Present(syncInterval, flags);

    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            throw std::runtime_error("Device removed/reset during Present");
        }
        throw_if_failed(hr, "Present failed");
    }

    m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();
}

#endif // _WIN32
