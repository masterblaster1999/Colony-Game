#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "d3d11_device.h"

#include <cassert>
#include <cstdio>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace render {

// Helper: small debug print without depending on external logger
static void dbg_print(const char* fmt, ...)
{
#if defined(_DEBUG)
    char buf[1024];
    va_list args; va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf);
#endif
}

// --- Private helpers ---------------------------------------------------------

bool D3D11Device::try_create_device(D3D_DRIVER_TYPE driverType, UINT flags)
{
    static const D3D_FEATURE_LEVEL kRequested[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_11_0;

    HRESULT hr = D3D11CreateDevice(
        /*pAdapter*/     nullptr,
        /*DriverType*/   driverType,
        /*Software*/     nullptr,
        /*Flags*/        flags,
        /*pFeatureLevels*/ kRequested,
        /*FeatureLevels*/  UINT(_countof(kRequested)),
        /*SDKVersion*/   D3D11_SDK_VERSION,
        /*ppDevice*/     device.GetAddressOf(),
        /*pFeatureLevel*/&obtained,
        /*ppImmediateContext*/ context.GetAddressOf());

    if (FAILED(hr)) return false;

    m_device = device;
    m_context = context;
    m_featureLevel = obtained;
    return true;
}

bool D3D11Device::query_tearing_support()
{
    m_tearingSupported = false;

    ComPtr<IDXGIFactory4> factory4;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory4))))
        return false;

    ComPtr<IDXGIFactory5> factory5;
    if (FAILED(factory4.As(&factory5)))
        return false;

    BOOL allowTearing = FALSE;
    if (SUCCEEDED(factory5->CheckFeatureSupport(
            DXGI_FEATURE_PRESENT_ALLOW_TEARING,
            &allowTearing, sizeof(allowTearing))))
    {
        m_tearingSupported = (allowTearing == TRUE);
    }
    return m_tearingSupported;
}

void D3D11Device::destroy_targets()
{
    if (m_context) {
        // Unbind render targets before destroying them
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
    }
    m_rtv.Reset();
}

bool D3D11Device::create_swapchain_and_targets(uint32_t width, uint32_t height)
{
    assert(m_device && m_context);
    if (!m_device || !m_context) return false;

    // Acquire factory from the device's adapter to avoid mismatched adapters.
    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = m_device.As(&dxgiDevice);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIFactory1> baseFactory;
    hr = adapter->GetParent(IID_PPV_ARGS(&baseFactory));
    if (FAILED(hr)) return false;

    ComPtr<IDXGIFactory2> factory2;
    hr = baseFactory.As(&factory2);
    if (FAILED(hr)) return false;

    // Disable DXGI's default Alt+Enter behavior (we'll manage windowing ourselves).
    factory2->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);

    // Build a modern flip-model swap chain.
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width       = width;
    desc.Height      = height;
    desc.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Stereo      = FALSE;
    desc.SampleDesc  = { 1, 0 };
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2; // double-buffered is usually ideal for flip-model
    desc.Scaling     = DXGI_SCALING_STRETCH;
    desc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode   = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags       = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT |
                       (m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);

    ComPtr<IDXGISwapChain1> swapchain;
    hr = factory2->CreateSwapChainForHwnd(
        m_device.Get(),
        m_hwnd,
        &desc,
        /*pFullscreenDesc*/ nullptr,
        /*pRestrictToOutput*/ nullptr,
        swapchain.GetAddressOf());

    if (FAILED(hr)) {
        dbg_print("[D3D11Device] CreateSwapChainForHwnd failed: 0x%08X\n", hr);
        return false;
    }

    m_swapchain = swapchain;

    // Configure waitable swap chain (max frame latency = 1) and cache the handle.
    ComPtr<IDXGISwapChain2> sc2;
    if (SUCCEEDED(m_swapchain.As(&sc2))) {
        sc2->SetMaximumFrameLatency(1);
        m_frameLatencyWaitable = sc2->GetFrameLatencyWaitableObject(); // do NOT CloseHandle
    } else {
        m_frameLatencyWaitable = nullptr; // not available on older OS
    }

    // Create RTV from the back buffer
    ComPtr<ID3D11Texture2D> backBuffer;
    hr = m_swapchain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) {
        dbg_print("[D3D11Device] GetBuffer(0) failed: 0x%08X\n", hr);
        return false;
    }

    ComPtr<ID3D11RenderTargetView> rtv;
    hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &rtv);
    if (FAILED(hr)) {
        dbg_print("[D3D11Device] CreateRenderTargetView failed: 0x%08X\n", hr);
        return false;
    }
    m_rtv = rtv;

    // Set viewport
    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width    = static_cast<float>(width);
    vp.Height   = static_cast<float>(height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    return true;
}

// --- Public API --------------------------------------------------------------

bool D3D11Device::initialize(HWND hwnd, uint32_t width, uint32_t height, bool request_debug_layer)
{
    shutdown(); // in case of re-init

    m_hwnd = hwnd;
    m_width = width;
    m_height = height;

    // Query tearing support early to decide swap-chain flags
    query_tearing_support();

    // Build device
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    if (request_debug_layer) flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    m_debugLayerEnabled = (flags & D3D11_CREATE_DEVICE_DEBUG) != 0;

    // Try hardware with current flags; if debug layer missing, fall back without it.
    if (!try_create_device(D3D_DRIVER_TYPE_HARDWARE, flags)) {
#if defined(_DEBUG)
        if (m_debugLayerEnabled) {
            dbg_print("[D3D11Device] Debug layer unavailable; retrying without it.\n");
            flags &= ~D3D11_CREATE_DEVICE_DEBUG;
            m_debugLayerEnabled = false;
            if (!try_create_device(D3D_DRIVER_TYPE_HARDWARE, flags)) {
                dbg_print("[D3D11Device] Hardware device failed; trying WARP.\n");
                if (!try_create_device(D3D_DRIVER_TYPE_WARP, flags)) return false;
            }
        } else
#endif
        {
            dbg_print("[D3D11Device] Hardware device failed; trying WARP.\n");
            if (!try_create_device(D3D_DRIVER_TYPE_WARP, flags)) return false;
        }
    }

    // Create swap chain + RTV
    if (!create_swapchain_and_targets(width, height)) {
        shutdown();
        return false;
    }

    dbg_print("[D3D11Device] Initialized (FL %x), tearing=%d\n",
              unsigned(m_featureLevel), m_tearingSupported ? 1 : 0);
    return true;
}

void D3D11Device::shutdown()
{
    // Release in reverse order of dependencies
    destroy_targets();

    // Release swap chain last among DXGI objects that reference the device
    m_swapchain.Reset();

    // Note: Do NOT CloseHandle(m_frameLatencyWaitable); DXGI manages it.
    m_frameLatencyWaitable = nullptr;

    m_context.Reset();
    m_device.Reset();

    m_width = m_height = 0;
    m_hwnd = nullptr;
    // preserve m_tearingSupported & m_debugLayerEnabled for diagnostics if desired
}

bool D3D11Device::resize(uint32_t width, uint32_t height)
{
    if (!m_swapchain || !m_device || !m_context) return false;

    // Ignore minimized case; we'll rebuild when restored to >0 size.
    if (width == 0 || height == 0) {
        m_width = width;
        m_height = height;
        return true;
    }

    if (width == m_width && height == m_height && m_rtv) {
        // Nothing to do.
        return true;
    }

    destroy_targets();

    m_width = width;
    m_height = height;

    UINT flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT |
                 (m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);

    HRESULT hr = m_swapchain->ResizeBuffers(
        /*BufferCount*/ 0, // keep same
        /*Width*/       width,
        /*Height*/      height,
        /*NewFormat*/   DXGI_FORMAT_UNKNOWN,
        /*SwapChainFlags*/ flags);

    if (FAILED(hr)) {
        dbg_print("[D3D11Device] ResizeBuffers failed: 0x%08X\n", hr);
        return false;
    }

    // Recreate RTV
    ComPtr<ID3D11Texture2D> backBuffer;
    hr = m_swapchain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) {
        dbg_print("[D3D11Device] GetBuffer(0) after Resize failed: 0x%08X\n", hr);
        return false;
    }

    hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_rtv.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        dbg_print("[D3D11Device] CreateRenderTargetView after Resize failed: 0x%08X\n", hr);
        return false;
    }

    // Refresh waitable handle (should remain valid, but re-query is harmless)
    ComPtr<IDXGISwapChain2> sc2;
    if (SUCCEEDED(m_swapchain.As(&sc2))) {
        sc2->SetMaximumFrameLatency(1);
        m_frameLatencyWaitable = sc2->GetFrameLatencyWaitableObject();
    } else {
        m_frameLatencyWaitable = nullptr;
    }

    // Update viewport
    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width    = static_cast<float>(width);
    vp.Height   = static_cast<float>(height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    return true;
}

void D3D11Device::begin_frame(const float clear_color[4])
{
    assert(m_rtv && m_context);
    if (!m_rtv || !m_context) return;

    ID3D11RenderTargetView* rtv = m_rtv.Get();
    m_context->OMSetRenderTargets(1, &rtv, nullptr);

    // Keep viewport in sync if someone changed width/height externally
    D3D11_VIEWPORT vp;
    UINT num = 1;
    m_context->RSGetViewports(&num, &vp);
    if (num != 1 || vp.Width != float(m_width) || vp.Height != float(m_height)) {
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width    = static_cast<float>(m_width);
        vp.Height   = static_cast<float>(m_height);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &vp);
    }

    m_context->ClearRenderTargetView(m_rtv.Get(), clear_color);
}

bool D3D11Device::present(bool vsync)
{
    if (!m_swapchain) return false;

    const UINT syncInterval = vsync ? 1u : 0u;
    const UINT presentFlags = (!vsync && m_tearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0u;

    HRESULT hr = m_swapchain->Present(syncInterval, presentFlags);
    if (FAILED(hr)) {
        // Handle device loss (TDR, driver upgrade, etc.)
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            HRESULT reason = m_device ? m_device->GetDeviceRemovedReason() : S_OK;
            dbg_print("[D3D11Device] Device lost (Present hr=0x%08X, reason=0x%08X). Reinitializing...\n", hr, reason);

            // Cache state and rebuild
            HWND hwnd = m_hwnd;
            uint32_t w = m_width;
            uint32_t h = m_height;
            bool wantDebug = m_debugLayerEnabled;

            shutdown();
            return initialize(hwnd, w, h, wantDebug);
        }
        dbg_print("[D3D11Device] Present failed: 0x%08X\n", hr);
        return false;
    }

    return true;
}

} // namespace render
