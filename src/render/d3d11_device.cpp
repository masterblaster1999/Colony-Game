// src/render/d3d11_device.cpp
#include "d3d11_device.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgi1_5.h>
#include <wrl/client.h>
#include <cstdio>
#include <iterator>

#ifndef DXGI_PRESENT_ALLOW_TEARING
#define DXGI_PRESENT_ALLOW_TEARING 0x00000200u
#endif

using Microsoft::WRL::ComPtr;

namespace render {

//--------------------------------------------------------------------------------------------------
// Local debug print
//--------------------------------------------------------------------------------------------------
static void debug_print(const char* msg)
{
#if defined(_DEBUG)
    OutputDebugStringA(msg);
#else
    (void)msg;
#endif
}

//--------------------------------------------------------------------------------------------------
// Initialize: device + immediate context + swap chain + backbuffer RTV
//--------------------------------------------------------------------------------------------------
bool D3D11Device::initialize(HWND hwnd, uint32_t width, uint32_t height, bool request_debug_layer)
{
    m_hwnd = hwnd;
    m_width = width;
    m_height = height;
    m_debugLayerEnabled = false;
    m_tearingSupported = false;

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
            // 3) Last-ditch REFERENCE (very slow; often not installed). If this fails, bail.
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

    // Query OS support for tearing (DXGI 1.5+) before creating the swap chain.
    query_tearing_support(); // uses IDXGIFactory5::CheckFeatureSupport. See docs. 

    if (!create_swapchain_and_targets(width, height)) {
        shutdown();
        return false;
    }

    return true;
}

//--------------------------------------------------------------------------------------------------
// Shutdown all GPU objects owned by this wrapper
//--------------------------------------------------------------------------------------------------
void D3D11Device::shutdown()
{
    destroy_targets();
    m_swapchain.Reset();
    m_context.Reset();
    m_device.Reset();
    m_hwnd = nullptr;
    m_width = m_height = 0;
    m_debugLayerEnabled = false;
    m_tearingSupported = false;
    m_featureLevel = static_cast<D3D_FEATURE_LEVEL>(0);
}

//--------------------------------------------------------------------------------------------------
// Resize swap chain (safe with minimized windows). Recreate RTV & viewport.
//--------------------------------------------------------------------------------------------------
bool D3D11Device::resize(uint32_t width, uint32_t height)
{
    if (!m_swapchain) return false;

    if (width == 0 || height == 0) {
        // Minimized: just drop RTV so OMSetRenderTargets won't bind stale pointers.
        destroy_targets();
        m_width = width;
        m_height = height;
        return true;
    }

    destroy_targets();

    const UINT flags = m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    const HRESULT hrResize = m_swapchain->ResizeBuffers(
        0, width, height, DXGI_FORMAT_UNKNOWN, flags);
    if (FAILED(hrResize)) {
        debug_print("[D3D11] ResizeBuffers failed.\n");
        return false;
    }

    m_width = width;
    m_height = height;

    // Recreate backbuffer RTV & viewport
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = m_swapchain->GetBuffer(
        0, __uuidof(ID3D11Texture2D),
        reinterpret_cast<void**>(backBuffer.GetAddressOf()));
    if (FAILED(hr)) {
        debug_print("[D3D11] GetBuffer(0) failed after resize.\n");
        return false;
    }

    hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_rtv.GetAddressOf());
    if (FAILED(hr)) {
        debug_print("[D3D11] CreateRenderTargetView failed after resize.\n");
        return false;
    }

    // Update viewport
    D3D11_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width    = static_cast<FLOAT>(m_width);
    vp.Height   = static_cast<FLOAT>(m_height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    return true;
}

//--------------------------------------------------------------------------------------------------
// Begin frame: bind RTV, set viewport, clear
//--------------------------------------------------------------------------------------------------
void D3D11Device::begin_frame(const float clear_color[4])
{
    if (!m_context || !m_rtv) return;

    ID3D11RenderTargetView* rtvs[] = { m_rtv.Get() };
    m_context->OMSetRenderTargets(1, rtvs, nullptr);

    D3D11_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width    = static_cast<FLOAT>(m_width);
    vp.Height   = static_cast<FLOAT>(m_height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    m_context->ClearRenderTargetView(m_rtv.Get(), clear_color);
}

//--------------------------------------------------------------------------------------------------
// Present: vsync toggle + tearing flag when allowed; detect device-lost and attempt recovery
//--------------------------------------------------------------------------------------------------
bool D3D11Device::present(bool vsync)
{
    if (!m_swapchain) return false;

    const UINT flags = (!vsync && m_tearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
    const UINT syncInterval = vsync ? 1u : 0u; // tearing allowed only with syncInterval == 0

    HRESULT hr = m_swapchain->Present(syncInterval, flags);

    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        debug_print("[D3D11] Device lost on Present; attempting to recreate device and swap chain...\n");
        const HWND hwnd = m_hwnd;
        const uint32_t w = m_width ? m_width : 1;
        const uint32_t h = m_height ? m_height : 1;
        const bool debugEnabled = m_debugLayerEnabled;
        shutdown();
        return initialize(hwnd, w, h, debugEnabled);
    }

    if (FAILED(hr)) {
        debug_print("[D3D11] Present failed.\n");
        return false;
    }
    return true;
}

//--------------------------------------------------------------------------------------------------
// Try to create a D3D11 device/context with given driver type & flags
//--------------------------------------------------------------------------------------------------
bool D3D11Device::try_create_device(D3D_DRIVER_TYPE driverType, UINT flags)
{
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
        dev.GetAddressOf(),
        &flOut,
        ctx.GetAddressOf()
    );

#if defined(_DEBUG)
    if (SUCCEEDED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
        m_debugLayerEnabled = true;
    }
#endif

    if (FAILED(hr)) {
        return false;
    }

    m_device = std::move(dev);
    m_context = std::move(ctx);
    m_featureLevel = flOut;
    return true;
}

//--------------------------------------------------------------------------------------------------
// Create flip-model swap chain + RTV and set initial viewport
//--------------------------------------------------------------------------------------------------
bool D3D11Device::create_swapchain_and_targets(uint32_t width, uint32_t height)
{
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
    scd.Width = width;
    scd.Height = height;
    scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.Stereo = FALSE;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2; // double-buffer
    scd.Scaling = DXGI_SCALING_STRETCH;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // modern flip model
    scd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    scd.Flags = m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    ComPtr<IDXGISwapChain1> swap;
    hr = factory2->CreateSwapChainForHwnd(
        m_device.Get(),
        m_hwnd,
        &scd,
        nullptr, // fullscreen desc
        nullptr, // restrict output
        swap.GetAddressOf()
    );
    if (FAILED(hr)) {
        debug_print("[D3D11] CreateSwapChainForHwnd failed.\n");
        return false;
    }

    // Disable DXGI's default Alt+Enter; app will manage windowing explicitly if desired.
    factory2->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);

    m_swapchain = std::move(swap);

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
    vp.Width    = static_cast<FLOAT>(width);
    vp.Height   = static_cast<FLOAT>(height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    return true;
}

//--------------------------------------------------------------------------------------------------
// Release RTV and unbind from the output-merger
//--------------------------------------------------------------------------------------------------
void D3D11Device::destroy_targets()
{
    if (m_context) {
        ID3D11RenderTargetView* nullRTV[1] = { nullptr };
        m_context->OMSetRenderTargets(1, nullRTV, nullptr);
    }
    m_rtv.Reset();
}

//--------------------------------------------------------------------------------------------------
// Tearing support query (DXGI 1.5). Safe to call even if not available.
//--------------------------------------------------------------------------------------------------
bool D3D11Device::query_tearing_support()
{
    m_tearingSupported = false;

    ComPtr<IDXGIFactory1> factory1;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                  reinterpret_cast<void**>(factory1.GetAddressOf())))) {
        return false;
    }

    ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(factory1.As(&factory5))) {
        BOOL allowTearing = FALSE;
        if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                                    &allowTearing, sizeof(allowTearing)))) {
            m_tearingSupported = (allowTearing == TRUE);
        }
    }
    return m_tearingSupported;
}

} // namespace render
