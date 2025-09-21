// src/render/d3d11_device.cpp
#include "d3d11_device.h"
#include <algorithm>
#include <iterator>
#include <cstdio>

#ifndef DXGI_PRESENT_ALLOW_TEARING
#define DXGI_PRESENT_ALLOW_TEARING 0x00000200u
#endif

namespace render {

static void debug_print(const char* msg) {
#if defined(_DEBUG)
    OutputDebugStringA(msg);
#else
    (void)msg;
#endif
}

bool D3D11Device::initialize(HWND hwnd, uint32_t width, uint32_t height, bool request_debug_layer) {
    m_hwnd = hwnd;
    m_width = width;
    m_height = height;
    m_debugLayerEnabled = false;

    // Attempt hardware + debug (if requested), fall back as needed.
    UINT flags = 0;
#if defined(_DEBUG)
    if (request_debug_layer) {
        flags |= D3D11_CREATE_DEVICE_DEBUG;
    }
#endif

    // 1) Try HARDWARE
    if (!try_create_device(D3D_DRIVER_TYPE_HARDWARE, flags)) {
        debug_print("[D3D11] Hardware device creation failed; trying WARP...\n");
        // 2) Try WARP (software rasterizer)
        if (!try_create_device(D3D_DRIVER_TYPE_WARP, flags & ~D3D11_CREATE_DEVICE_DEBUG)) {
            debug_print("[D3D11] WARP device creation failed; trying REFERENCE...\n");
            // 3) Last-ditch REFERENCE (very slow; often not installed).
            if (!try_create_device(D3D_DRIVER_TYPE_REFERENCE, flags & ~D3D11_CREATE_DEVICE_DEBUG)) {
                debug_print("[D3D11] Failed to create any D3D11 device.\n");
                return false;
            }
        }
    }

    // Validate feature level
    if (m_featureLevel < D3D_FEATURE_LEVEL_10_0) {
        debug_print("[D3D11] Feature level < 10.0 not supported. Initialization aborted.\n");
        shutdown();
        return false;
    }

    query_tearing_support();

    if (!create_swapchain_and_targets(width, height)) {
        shutdown();
        return false;
    }

    return true;
}

void D3D11Device::shutdown() {
    destroy_targets();
    release_waitable(); // close waitable handle if any
    m_swapchain2.Reset();
    m_swapchain.Reset();
    m_context.Reset();
    m_device.Reset();

    m_hwnd = nullptr;
    m_width = m_height = 0;
    m_waitableSwapChain = false;
    m_tearingSupported = false;
}

bool D3D11Device::resize(uint32_t width, uint32_t height) {
    if (!m_swapchain) return false;

    if (width == 0 || height == 0) {
        // Minimized: drop RTV so OMSetRenderTargets won't bind stale pointers
        destroy_targets();
        m_width = width;
        m_height = height;
        return true;
    }

    destroy_targets();

    // IMPORTANT: Flags passed to ResizeBuffers must match creation
    // with respect to FRAME_LATENCY_WAITABLE_OBJECT; you can't toggle it post-creation.
    UINT flags = 0;
    if (m_tearingSupported) flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    flags |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    HRESULT hr = m_swapchain->ResizeBuffers(
        0, width, height, DXGI_FORMAT_UNKNOWN, flags
    );
    if (FAILED(hr)) {
        debug_print("[D3D11] ResizeBuffers failed.\n");
        return false;
    }

    m_width = width;
    m_height = height;

    // Reacquire backbuffer RTV & viewport
    ComPtr<ID3D11Texture2D> backBuffer;
    hr = m_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(backBuffer.GetAddressOf()));
    if (FAILED(hr)) {
        debug_print("[D3D11] GetBuffer(0) failed after resize.\n");
        return false;
    }

    hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_rtv.GetAddressOf());
    if (FAILED(hr)) {
        debug_print("[D3D11] CreateRenderTargetView failed after resize.\n");
        return false;
    }

    D3D11_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width  = static_cast<float>(m_width);
    vp.Height = static_cast<float>(m_height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    // Reacquire the waitable handle (should be stable, but documented safe to fetch).
    if (m_swapchain2) {
        HANDLE h = m_swapchain2->GetFrameLatencyWaitableObject();
        if (h && h != m_frameLatencyWaitable) {
            release_waitable();
            m_frameLatencyWaitable = h;
        }
        // Restore desired latency
        if (m_waitableSwapChain) {
            m_swapchain2->SetMaximumFrameLatency(m_maxFrameLatency); // ignore failure if device lost
        }
    }

    return true;
}

void D3D11Device::begin_frame(const float clear_color[4]) {
    if (!m_context || !m_rtv) return;

    ID3D11RenderTargetView* rtvs[] = { m_rtv.Get() };
    m_context->OMSetRenderTargets(1, rtvs, nullptr);

    D3D11_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width  = static_cast<float>(m_width);
    vp.Height = static_cast<float>(m_height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    m_context->ClearRenderTargetView(m_rtv.Get(), clear_color);
}

bool D3D11Device::present(bool vsync) {
    if (!m_swapchain) return false;

    // Official guidance: ALLOW_TEARING only with sync interval 0 (vsync==false) and if supported.
    const UINT syncInterval = vsync ? 1u : 0u;
    const UINT flags = (!vsync && m_tearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0u;

    HRESULT hr = m_swapchain->Present(syncInterval, flags);

    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "[D3D11] Present failed: device lost (0x%08X).\n", (unsigned)hr);
            debug_print(buf);
        } else {
            debug_print("[D3D11] Present failed.\n");
        }
        return false;
    }
    return true;
}

bool D3D11Device::wait_for_next_frame(uint32_t timeout_ms) {
    if (!m_waitableSwapChain || !m_frameLatencyWaitable) {
        // No waitable chain available; nothing to wait on.
        return true;
    }
    // Wait for DXGI to signal the end of the previous present before starting to render the next frame.
    DWORD result = WaitForSingleObjectEx(m_frameLatencyWaitable, timeout_ms, TRUE);
    return (result == WAIT_OBJECT_0);
}

void D3D11Device::set_maximum_frame_latency(UINT max_latency) {
    // Valid range [1..16] for DXGI frame latency settings.
    m_maxFrameLatency = std::max<UINT>(1u, std::min<UINT>(16u, max_latency));
    if (m_swapchain2 && m_waitableSwapChain) {
        // Only valid if the swap chain was created with FRAME_LATENCY_WAITABLE_OBJECT.
        (void)m_swapchain2->SetMaximumFrameLatency(m_maxFrameLatency);
    }
}

bool D3D11Device::try_create_device(D3D_DRIVER_TYPE driverType, UINT flags) {
    static const D3D_FEATURE_LEVEL fls[] = {
    #if defined(D3D_FEATURE_LEVEL_11_1)
        D3D_FEATURE_LEVEL_11_1,
    #endif
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    D3D_FEATURE_LEVEL flOut{};

    HRESULT hr = D3D11CreateDevice(
        nullptr, // default adapter
        driverType,
        nullptr, // software module
        flags,
        fls, static_cast<UINT>(std::size(fls)),
        D3D11_SDK_VERSION,
        dev.GetAddressOf(), &flOut, ctx.GetAddressOf()
    );

#if defined(_DEBUG)
    if (SUCCEEDED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
        m_debugLayerEnabled = true;
    }
#endif

    if (FAILED(hr)) return false;

    m_device = std::move(dev);
    m_context = std::move(ctx);
    m_featureLevel = flOut;
    return true;
}

bool D3D11Device::create_swapchain_and_targets(uint32_t width, uint32_t height) {
    // Get factory from device
    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = m_device.As(&dxgiDevice);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
    if (FAILED(hr)) return false;

    ComPtr<IDXGIFactory1> factory1;
    hr = adapter->GetParent(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(factory1.GetAddressOf()));
    if (FAILED(hr)) return false;

    ComPtr<IDXGIFactory2> factory2;
    hr = factory1.As(&factory2);
    if (FAILED(hr)) return false;

    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width  = width;
    scd.Height = height;
    scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.Stereo = FALSE;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2; // double-buffer
    scd.Scaling = DXGI_SCALING_STRETCH;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // flip model for best perf
    scd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    // Always include waitable flag at creation; optionally add ALLOW_TEARING.
    scd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT; // required for waitable handle/latency APIs
    if (m_tearingSupported) {
        scd.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING; // enables Present(... ALLOW_TEARING) when vsync==false
    }

    ComPtr<IDXGISwapChain1> swap;
    hr = factory2->CreateSwapChainForHwnd(
        m_device.Get(), m_hwnd, &scd,
        nullptr, // fullscreen desc
        nullptr, // restrict output
        swap.GetAddressOf()
    );
    if (FAILED(hr)) {
        debug_print("[D3D11] CreateSwapChainForHwnd failed.\n");
        return false;
    }

    // Disable DXGI's default Alt+Enter; the app manages windowing explicitly.
    factory2->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);

    m_swapchain = std::move(swap);

    // QI to IDXGISwapChain2 for latency control and waitable handle.
    m_swapchain2.Reset();
    (void)m_swapchain.As(&m_swapchain2);

    m_waitableSwapChain = (m_swapchain2 != nullptr);
    if (m_waitableSwapChain) {
        // Default minimal latency; bump to 2 if you need more parallelism.
        m_maxFrameLatency = 1;
        m_swapchain2->SetMaximumFrameLatency(m_maxFrameLatency); // ignore failure if device lost

        // Acquire the waitable object handle (must be closed on shutdown).
        m_frameLatencyWaitable = m_swapchain2->GetFrameLatencyWaitableObject();
    }

    // Create RTV
    ComPtr<ID3D11Texture2D> backBuffer;
    hr = m_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(backBuffer.GetAddressOf()));
    if (FAILED(hr)) return false;

    hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_rtv.GetAddressOf());
    if (FAILED(hr)) return false;

    // Initial viewport
    D3D11_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width  = static_cast<float>(width);
    vp.Height = static_cast<float>(height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    return true;
}

void D3D11Device::destroy_targets() {
    if (m_context) {
        ID3D11RenderTargetView* nullRTV[1] = { nullptr };
        m_context->OMSetRenderTargets(1, nullRTV, nullptr);
    }
    m_rtv.Reset();
}

bool D3D11Device::query_tearing_support() {
    m_tearingSupported = false;

    ComPtr<IDXGIFactory1> factory1;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(factory1.GetAddressOf())))) {
        return false;
    }

    // Tearing support check via IDXGIFactory5 (DXGI_FEATURE_PRESENT_ALLOW_TEARING).
    ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(factory1.As(&factory5))) {
        BOOL allowTearing = FALSE;
        if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing)))) {
            m_tearingSupported = (allowTearing == TRUE);
        }
    }

    return m_tearingSupported;
}

void D3D11Device::release_waitable() {
    if (m_frameLatencyWaitable) {
        // Per docs, the application should CloseHandle when done with it.
        CloseHandle(m_frameLatencyWaitable);
        m_frameLatencyWaitable = nullptr;
    }
}

} // namespace render
