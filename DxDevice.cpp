#include "DxDevice.h"

#include <array>
#include <iterator> // std::size

// Small helper: check whether the OS/driver combo supports variable-refresh / tearing.
// Requires DXGI 1.5+ and flip-model swapchains.
static bool CheckTearing(IDXGIFactory6* factory)
{
    BOOL allow = FALSE;

    ComPtr<IDXGIFactory5> f5;
    if (factory && SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&f5))))
    {
        if (FAILED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow))))
            allow = FALSE;
    }

    return allow == TRUE;
}

static HRESULT CreateD3D11Device(UINT flags,
                                D3D_DRIVER_TYPE driverType,
                                ComPtr<ID3D11Device>& outDevice,
                                ComPtr<ID3D11DeviceContext>& outCtx)
{
    // Ask for the best available feature level down to 10.0.
    static constexpr D3D_FEATURE_LEVEL kLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    D3D_FEATURE_LEVEL created = D3D_FEATURE_LEVEL_11_0;

    return D3D11CreateDevice(
        nullptr,
        driverType,
        nullptr,
        flags,
        kLevels,
        static_cast<UINT>(std::size(kLevels)),
        D3D11_SDK_VERSION,
        outDevice.GetAddressOf(),
        &created,
        outCtx.GetAddressOf());
}

bool DxDevice::Init(HWND hwnd, UINT width, UINT height)
{
    const bool wasInitialized = (m_device != nullptr) || (m_ctx != nullptr) || (m_swap != nullptr);

    // Reset the edge-trigger; Init() will set it again on a successful re-init.
    m_deviceRecreated = false;

    // Allow re-init (e.g. after device removed/reset).
    Shutdown();

    m_hwnd = hwnd;
    m_width = width;
    m_height = height;

    auto fail = [&]() -> bool {
        Shutdown();
        return false;
    };

    // -------------------------------------------------------------------------
    // Create D3D11 device (with robust fallbacks)
    // -------------------------------------------------------------------------
    UINT flags = 0;

#if defined(_DEBUG)
    // The debug layer is optional and not always installed. We try it first and
    // fall back to a non-debug device if creation fails.
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;

    HRESULT hr = CreateD3D11Device(flags, D3D_DRIVER_TYPE_HARDWARE, dev, ctx);

#if defined(_DEBUG)
    if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG))
    {
        // Retry without the debug layer (common on machines without Graphics Tools).
        const UINT noDbg = flags & ~D3D11_CREATE_DEVICE_DEBUG;
        hr = CreateD3D11Device(noDbg, D3D_DRIVER_TYPE_HARDWARE, dev, ctx);
    }
#endif

    if (FAILED(hr))
    {
        // Hardware device creation can fail on some VMs/remote sessions. WARP is
        // slower but keeps the app runnable.
        hr = CreateD3D11Device(0, D3D_DRIVER_TYPE_WARP, dev, ctx);
        if (FAILED(hr))
            return fail();
    }

    m_device = dev;
    m_ctx = ctx;

    // -------------------------------------------------------------------------
    // Get the DXGI factory used by this device (for swapchain + tearing support)
    // -------------------------------------------------------------------------
    ComPtr<IDXGIDevice> dxgiDev;
    if (FAILED(m_device.As(&dxgiDev)))
        return fail();

    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDev->GetAdapter(adapter.GetAddressOf())))
        return fail();

    ComPtr<IDXGIFactory6> factory;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()))))
        return fail();

    m_factory = factory;

    // Disable DXGI's default Alt+Enter fullscreen handling; AppWindow manages it.
    if (m_factory)
        (void)m_factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);

    m_allowTearing = CheckTearing(m_factory.Get());

    if (!CreateSwapchain(width, height))
        return fail();

    // Ensure a sane default viewport for callers that don't manage it yet
    // (e.g., ImGui or early prototype rendering code).
    ApplyDefaultViewport();

    // Edge-trigger for higher-level layers (e.g. ImGui reinit on device-lost).
    m_deviceRecreated = wasInitialized;

    return true;
}

bool DxDevice::CreateSwapchain(UINT width, UINT height)
{
    if (!m_factory || !m_device)
        return false;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Format = m_backbufferFormat;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SampleDesc = { 1, 0 };
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;  // flip-model
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    // Allow tearing only when supported and only when presenting with syncInterval==0.
    desc.Flags = m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    ComPtr<IDXGISwapChain1> swap;
    const HRESULT hr = m_factory->CreateSwapChainForHwnd(
        m_device.Get(),
        m_hwnd,
        &desc,
        nullptr,
        nullptr,
        swap.GetAddressOf());

    if (FAILED(hr))
        return false;

    m_swap = swap;

    CreateRTV();
    return m_rtv != nullptr;
}

void DxDevice::CreateRTV()
{
    DestroyRTV();

    if (!m_swap || !m_device)
        return;

    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(m_swap->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()))))
        return;

    (void)m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_rtv.GetAddressOf());
}

void DxDevice::DestroyRTV()
{
    if (m_rtv)
        m_rtv.Reset();
}

void DxDevice::ApplyDefaultViewport() noexcept
{
    if (!m_ctx)
        return;

    const float w = (m_width > 0) ? static_cast<float>(m_width) : 1.0f;
    const float h = (m_height > 0) ? static_cast<float>(m_height) : 1.0f;

    D3D11_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width    = w;
    vp.Height   = h;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_ctx->RSSetViewports(1, &vp);
}

void DxDevice::Resize(UINT width, UINT height)
{
    m_width = width;
    m_height = height;

    if (!m_swap || width == 0 || height == 0)
        return;

    DestroyRTV();

    const HRESULT hr = m_swap->ResizeBuffers(
        0,
        width,
        height,
        m_backbufferFormat,
        m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);

    if (FAILED(hr))
    {
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
            (void)HandleDeviceLost();
        return;
    }

    CreateRTV();
    ApplyDefaultViewport();
}

void DxDevice::BeginFrame()
{
    if (!m_ctx || !m_rtv)
        return;

    // If the device is already removed, recover before issuing any commands.
    if (m_device)
    {
        const HRESULT reason = m_device->GetDeviceRemovedReason();
        if (FAILED(reason) && reason != S_OK)
        {
            if (!HandleDeviceLost())
                return;
        }
    }

    m_ctx->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);
    m_ctx->ClearRenderTargetView(m_rtv.Get(), m_clearColor);
    ApplyDefaultViewport();
}

void DxDevice::EndFrame(bool vsync)
{
    if (!m_swap)
        return;

    const UINT syncInterval = vsync ? 1u : 0u;

    UINT presentFlags = 0;
    if (!vsync && m_allowTearing)
        presentFlags |= DXGI_PRESENT_ALLOW_TEARING;

    const HRESULT hr = m_swap->Present(syncInterval, presentFlags);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
    {
        (void)HandleDeviceLost();
    }
}

void DxDevice::Shutdown()
{
    DestroyRTV();

    if (m_swap)   m_swap.Reset();
    if (m_ctx)    m_ctx.Reset();
    if (m_device) m_device.Reset();
    if (m_factory) m_factory.Reset();

    m_hwnd = nullptr;
    m_allowTearing = false;
    m_width = 0;
    m_height = 0;

    m_deviceRecreated = false;
}

bool DxDevice::HandleDeviceLost()
{
    // Device removed/reset can occur due to TDR, a driver update, or the adapter
    // changing (e.g. docking/undocking, remote sessions, etc.).
    //
    // We do a best-effort full recreation to keep the prototype running.
    const HWND hwnd = m_hwnd;
    const UINT w = m_width ? m_width : 1280u;
    const UINT h = m_height ? m_height : 720u;

    // Optional: query reason for debugging.
    if (m_device)
    {
        (void)m_device->GetDeviceRemovedReason();
    }

    Shutdown();
    return Init(hwnd, w, h);
}
