#include "Swapchain.h"
#include <cassert>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;
using namespace renderer;

// Small helper: get factory from a D3D11 device.
static HRESULT GetDXGIFactoryFromDevice(ID3D11Device* device, IDXGIFactory2** outFactory2)
{
    if (!device || !outFactory2) return E_INVALIDARG;

    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (FAILED(hr)) return hr;

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) return hr;

    ComPtr<IDXGIFactory1> factory1;
    hr = adapter->GetParent(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(factory1.GetAddressOf()));
    if (FAILED(hr)) return hr;

    // We want IDXGIFactory2 for CreateSwapChainForHwnd
    return factory1.As(outFactory2);
}

// Query support for tearing (VRR)
static bool QueryAllowTearing(IDXGIFactory2* factory2)
{
    if (!factory2) return false;

    ComPtr<IDXGIFactory5> factory5;
    if (FAILED(factory2->QueryInterface(IID_PPV_ARGS(&factory5))))
        return false;

    BOOL allow = FALSE;
    if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                                &allow, sizeof(allow))))
    {
        return allow == TRUE;
    }
    return false;
}

HRESULT Swapchain::Initialize(ID3D11Device* device,
                              ID3D11DeviceContext* context,
                              const SwapchainCreateInfo& info)
{
    if (!device || !context || !info.hwnd || info.width == 0 || info.height == 0)
        return E_INVALIDARG;

    m_device  = device;
    m_context = context;
    m_hwnd    = info.hwnd;
    m_width   = info.width;
    m_height  = info.height;
    m_backbufferFormat = info.colorFormat;
    m_useSRGB = info.sRGB;
    m_bufferCount = info.tripleBuffer ? 3u : 2u;

    HRESULT hr = GetDXGIFactoryFromDevice(m_device.Get(), &m_factory2);
    if (FAILED(hr)) return hr;

    // Disable default Alt+Enter handling
    m_factory2->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);

    // Detect tearing/VRR support (Windows 10 1803+ + driver + monitor)
    m_allowTearing = info.allowTearingIfSupported && QueryAllowTearing(m_factory2.Get());

    hr = createSwapChain(info);
    if (FAILED(hr)) return hr;

    // Optional: queue depth/rtv creation now
    return createTargets();
}

void Swapchain::Shutdown()
{
    m_context.Reset();
    m_device.Reset();
    m_rtv.Reset();
    m_dsv.Reset();
    m_backBuffer.Reset();
    m_depth.Reset();
    m_swapChain.Reset();
    m_factory2.Reset();
}

HRESULT Swapchain::createSwapChain(const SwapchainCreateInfo& info)
{
    // Create a modern flip-model chain for a window (HWND)
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width       = m_width;
    desc.Height      = m_height;
    desc.Format      = m_backbufferFormat;           // UNORM swap-chain (we'll use SRGB RTV if requested)
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = m_bufferCount;                // 2 or 3
    desc.SampleDesc  = { 1, 0 };                     // no MSAA for swap-chain
    desc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Scaling     = DXGI_SCALING_STRETCH;
    desc.AlphaMode   = DXGI_ALPHA_MODE_IGNORE;

    m_swapChainFlags = m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    desc.Flags       = m_swapChainFlags;

    ComPtr<IDXGISwapChain1> sc;
    HRESULT hr = m_factory2->CreateSwapChainForHwnd(
        m_device.Get(), info.hwnd, &desc,
        nullptr, // full-screen desc (nullptr => windowed)
        nullptr, // restrict output
        &sc);
    if (FAILED(hr)) return hr;

    m_swapChain = sc;

    // Low-latency hint (not guaranteed)
    ComPtr<IDXGIDevice1> dxgiDev1;
    if (SUCCEEDED(m_device.As(&dxgiDev1)))
    {
        dxgiDev1->SetMaximumFrameLatency(info.maxFrameLatency);
    }

    return S_OK;
}

HRESULT Swapchain::createTargets()
{
    // Release previous
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_rtv.Reset();
    m_dsv.Reset();
    m_backBuffer.Reset();
    m_depth.Reset();

    // Backbuffer -> RTV (optionally SRGB)
    ComPtr<ID3D11Texture2D> bb;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&bb));
    if (FAILED(hr)) return hr;

    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Format = m_useSRGB
        ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
        : DXGI_FORMAT_R8G8B8A8_UNORM;

    hr = m_device->CreateRenderTargetView(bb.Get(), &rtvDesc, &m_rtv);
    if (FAILED(hr)) return hr;

    // Depth buffer + DSV
    D3D11_TEXTURE2D_DESC dsDesc = {};
    dsDesc.Width              = m_width;
    dsDesc.Height             = m_height;
    dsDesc.MipLevels          = 1;
    dsDesc.ArraySize          = 1;
    dsDesc.Format             = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsDesc.SampleDesc.Count   = 1;
    dsDesc.BindFlags          = D3D11_BIND_DEPTH_STENCIL;
    dsDesc.Usage              = D3D11_USAGE_DEFAULT;

    ComPtr<ID3D11Texture2D> ds;
    hr = m_device->CreateTexture2D(&dsDesc, nullptr, &ds);
    if (FAILED(hr)) return hr;

    hr = m_device->CreateDepthStencilView(ds.Get(), nullptr, &m_dsv);
    if (FAILED(hr)) return hr;

    // Save references
    m_backBuffer = bb;
    m_depth      = ds;

    return S_OK;
}

HRESULT Swapchain::Resize(UINT width, UINT height)
{
    if (!m_swapChain) return E_FAIL;
    if (width == 0 || height == 0) return E_INVALIDARG;

    m_width  = width;
    m_height = height;

    // Unbind and release current targets before ResizeBuffers (required)
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_rtv.Reset();
    m_dsv.Reset();
    m_backBuffer.Reset();
    m_depth.Reset();

    // Per VRR docs: ResizeBuffers must carry the same tearing flag used at creation. :contentReference[oaicite:3]{index=3}
    HRESULT hr = m_swapChain->ResizeBuffers(
        m_bufferCount,
        m_width,
        m_height,
        DXGI_FORMAT_UNKNOWN,      // keep existing format
        m_swapChainFlags);        // preserve ALLOW_TEARING if used
    if (FAILED(hr)) return hr;

    return createTargets();
}

HRESULT Swapchain::Present(bool vsync)
{
    if (!m_swapChain) return E_FAIL;

    // syncInterval=1 => vsync; 0 => uncapped; if tearing supported, pass flag when uncapped. :contentReference[oaicite:4]{index=4}
    const UINT syncInterval = vsync ? 1u : 0u;

    UINT flags = 0;
    if (!vsync && m_allowTearing)
        flags |= DXGI_PRESENT_ALLOW_TEARING;

    return m_swapChain->Present(syncInterval, flags);
}
