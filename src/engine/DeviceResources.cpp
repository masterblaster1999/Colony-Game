#include "DeviceResources.h"
#include <stdexcept>
#include <algorithm>

using namespace cg::gfx;
using Microsoft::WRL::ComPtr;

static void ThrowIfFailed(HRESULT hr, const char* what)
{
    if (FAILED(hr)) throw std::runtime_error(what);
}

DeviceResources::~DeviceResources()
{
    ReleaseSizeDependentResources();
}

void DeviceResources::Initialize(const CreateDesc& desc)
{
    m_hwnd              = desc.hwnd;
    m_requestSRGB       = desc.requestSRGB;
    m_backBufferCount   = std::max<UINT>(2u, desc.backBufferCount); // flip-model requires >= 2
    m_backBufferFormat  = desc.backBufferFormat;
    m_depthFormat       = desc.depthFormat;

    if (!m_hwnd) throw std::runtime_error("DeviceResources.Initialize: hwnd is null");

    CreateDeviceAndFactory(desc.enableDebug);
    DisableAltEnter();                   // prevent DXGI from handling Alt+Enter for us
    m_allowTearing = CheckTearingSupport();

    UpdateClientRect();
    CreateSwapChain();
    CreateSizeDependentResources();
}

void DeviceResources::CreateDeviceAndFactory(bool enableDebug)
{
    // Always request BGRA support for modern Windows rendering interop.
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

#if defined(_DEBUG)
    // In debug builds we *can* use the D3D debug layer if requested.
    if (enableDebug)
    {
        flags |= D3D11_CREATE_DEVICE_DEBUG;
    }
#else
    // In non-debug builds we ignore the flag but explicitly mark it used to
    // avoid C4100 ("unreferenced parameter") when /WX is on.
    (void)enableDebug;
#endif

    // Create device on default adapter (you can enumerate if you wish).
    D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    ComPtr<ID3D11Device>        dev;
    ComPtr<ID3D11DeviceContext> ctx;
    D3D_FEATURE_LEVEL           flOut{};

    // First attempt with whatever flags we computed (possibly including debug).
    HRESULT hr = D3D11CreateDevice(
        nullptr,                    // default adapter (or enumerate via IDXGIFactory if desired)
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        featureLevels, _countof(featureLevels),
        D3D11_SDK_VERSION,
        dev.GetAddressOf(),
        &flOut,
        ctx.GetAddressOf()
    );

#if defined(_DEBUG)
    // If the debug layer isn't installed, creation can fail. In that case,
    // retry without the debug flag so Debug builds still run on player PCs.
    if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG))
    {
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;

        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            featureLevels, _countof(featureLevels),
            D3D11_SDK_VERSION,
            dev.GetAddressOf(),
            &flOut,
            ctx.GetAddressOf()
        );
    }
#endif

    ThrowIfFailed(hr, "D3D11CreateDevice failed");

    m_device  = std::move(dev);
    m_context = std::move(ctx);

    // Create a DXGI factory (highest available; CreateDXGIFactory2 not strictly required here).
    ComPtr<IDXGIDevice> dxgiDevice;
    ThrowIfFailed(m_device.As(&dxgiDevice), "Query IDXGIDevice failed");

    ComPtr<IDXGIAdapter> adapter;
    ThrowIfFailed(dxgiDevice->GetAdapter(adapter.GetAddressOf()), "GetAdapter failed");

    ComPtr<IDXGIFactory> factory;
    ThrowIfFailed(adapter->GetParent(__uuidof(IDXGIFactory), &factory),
                  "GetParent IDXGIFactory failed");

    // Try newer factory interfaces (for checks like tearing support; 1.5/1.6+)
    factory.As(&m_factory); // promotes to highest IDXGIFactoryN available
}

bool DeviceResources::CheckTearingSupport()
{
    // DXGI 1.5: DXGI_FEATURE_PRESENT_ALLOW_TEARING
    BOOL allow = FALSE;
    ComPtr<IDXGIFactory5> f5;
    if (SUCCEEDED(m_factory.As(&f5)) && f5)
    {
        HRESULT hr = f5->CheckFeatureSupport(
            DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow));
        if (FAILED(hr)) allow = FALSE;
    }
    return allow ? true : false;
}

void DeviceResources::DisableAltEnter()
{
    // Disable DXGI's ALT+ENTER handling, we'll decide our own fullscreen behavior
    if (m_factory)
    {
        m_factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);
    }
}

void DeviceResources::UpdateClientRect()
{
    RECT r{}; GetClientRect(m_hwnd, &r);
    m_client = r;
    m_width  = std::max<UINT>(1, static_cast<UINT>(r.right - r.left));
    m_height = std::max<UINT>(1, static_cast<UINT>(r.bottom - r.top));
}

void DeviceResources::CreateSwapChain()
{
    // Create or recreate a flip‑model swap chain bound to HWND
    ComPtr<IDXGIFactory2> f2;
    ThrowIfFailed(m_factory.As(&f2), "IDXGIFactory2 not available");

    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width       = 0;  // let DXGI size from the HWND (will be updated on resize)
    scd.Height      = 0;
    scd.Format      = m_backBufferFormat; // UNORM; we’ll make an SRGB RTV if requested
    scd.Stereo      = FALSE;
    scd.SampleDesc  = { 1, 0 };           // flip‑model requires Count = 1
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = std::max<UINT>(2u, m_backBufferCount);
    scd.Scaling     = DXGI_SCALING_STRETCH;  // or DXGI_SCALING_NONE if you manage scaling yourself
    scd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD; // best perf, Win10+
    scd.AlphaMode   = DXGI_ALPHA_MODE_IGNORE;
    scd.Flags       = m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    ComPtr<IDXGISwapChain1> sc1;
    ThrowIfFailed(f2->CreateSwapChainForHwnd(
        m_device.Get(), m_hwnd, &scd, nullptr, nullptr, sc1.GetAddressOf()),
        "CreateSwapChainForHwnd failed");

    m_swapChain = std::move(sc1);
}

void DeviceResources::ReleaseSizeDependentResources()
{
    if (m_context) m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_dsv.Reset();
    m_rtv.Reset();
}

void DeviceResources::CreateSizeDependentResources()
{
    ReleaseSizeDependentResources();

    // Get backbuffer
    ComPtr<ID3D11Texture2D> backbuf;
    ThrowIfFailed(m_swapChain->GetBuffer(0, IID_PPV_ARGS(backbuf.GetAddressOf())),
                  "SwapChain GetBuffer(0) failed");

    // Create SRGB RTV if requested (DXGI special case lets us create _SRGB RTV for UNORM backbuffer)
    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.ViewDimension       = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice  = 0;
    rtvDesc.Format = (m_requestSRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : m_backBufferFormat);

    HRESULT hr = m_device->CreateRenderTargetView(backbuf.Get(), &rtvDesc, m_rtv.GetAddressOf());
    if (FAILED(hr) && m_requestSRGB)
    {
        // Fallback to non‑SRGB if driver refuses SRGB RTV on backbuffer
        rtvDesc.Format = m_backBufferFormat;
        ThrowIfFailed(m_device->CreateRenderTargetView(backbuf.Get(), &rtvDesc, m_rtv.GetAddressOf()),
                      "CreateRenderTargetView failed");
    }

    // Create/resize depth‑stencil
    D3D11_TEXTURE2D_DESC dsDesc{};
    dsDesc.Width              = m_width;
    dsDesc.Height             = m_height;
    dsDesc.MipLevels          = 1;
    dsDesc.ArraySize          = 1;
    dsDesc.Format             = m_depthFormat;
    dsDesc.SampleDesc         = { 1, 0 };
    dsDesc.BindFlags          = D3D11_BIND_DEPTH_STENCIL;

    ComPtr<ID3D11Texture2D> depth;
    ThrowIfFailed(m_device->CreateTexture2D(&dsDesc, nullptr, depth.GetAddressOf()),
                  "CreateTexture2D(depth) failed");

    ThrowIfFailed(m_device->CreateDepthStencilView(depth.Get(), nullptr, m_dsv.GetAddressOf()),
                  "CreateDepthStencilView failed");

    // Set viewport
    D3D11_VIEWPORT vp{};
    vp.TopLeftX = 0.0f; vp.TopLeftY = 0.0f;
    vp.Width    = static_cast<float>(m_width);
    vp.Height   = static_cast<float>(m_height);
    vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    // Bind OM
    ID3D11RenderTargetView* rtvs[] = { m_rtv.Get() };
    m_context->OMSetRenderTargets(1, rtvs, m_dsv.Get());
}

void DeviceResources::ResizeIfNeeded()
{
    RECT prev = m_client;
    UpdateClientRect();
    if (EqualRect(&prev, &m_client))
        return;

    // Resize swap chain buffers, keep the flags consistent (ALLOW_TEARING if supported)
    ReleaseSizeDependentResources();
    HRESULT hr = m_swapChain->ResizeBuffers(
        std::max<UINT>(2u, m_backBufferCount),
        0, 0, m_backBufferFormat,
        m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
    ThrowIfFailed(hr, "ResizeBuffers failed");

    CreateSizeDependentResources();
}

void DeviceResources::Present(bool vsync)
{
    // Flip‑model present: vsync off uses tearing flag if supported
    UINT sync  = vsync ? 1u : 0u;
    UINT flags = 0;
    if (!vsync && m_allowTearing)
        flags |= DXGI_PRESENT_ALLOW_TEARING; // Valid only with SyncInterval = 0

    HRESULT hr = m_swapChain->Present(sync, flags);
    if (FAILED(hr))
        throw std::runtime_error("SwapChain Present failed");
}

void DeviceResources::SetMaxFrameLatency(UINT frames)
{
    ComPtr<IDXGISwapChain2> sc2;
    if (SUCCEEDED(m_swapChain.As(&sc2)) && sc2)
    {
        sc2->SetMaximumFrameLatency(std::max<UINT>(1, frames));
    }
}
