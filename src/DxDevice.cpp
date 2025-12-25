#include "DxDevice.h"

#include <algorithm> // std::clamp
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

static UINT ClampFrameLatency(UINT v) noexcept
{
    // DXGI uses an implementation-defined upper bound. Common guidance is 1..16.
    // We clamp to a conservative range so invalid values don't fail Present loops.
    return std::clamp(v, 1u, 16u);
}

void DxDevice::SetMaximumFrameLatency(UINT maxLatency) noexcept
{
    maxLatency = ClampFrameLatency(maxLatency);
    m_maxFrameLatency = maxLatency;

    // Preferred: per-swapchain (required when using the waitable-object flag).
    if (m_swap2 && (m_swapChainFlags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))
    {
        (void)m_swap2->SetMaximumFrameLatency(maxLatency);
        return;
    }

    // Fallback: device-level frame latency control (older swapchains).
    ComPtr<IDXGIDevice1> dxgiDev1;
    if (m_device && SUCCEEDED(m_device.As(&dxgiDev1)))
    {
        (void)dxgiDev1->SetMaximumFrameLatency(maxLatency);
    }
}

bool DxDevice::Init(HWND hwnd, UINT width, UINT height)
{
    // Allow re-init (e.g. after device removed/reset).
    Shutdown();

    m_hwnd = hwnd;
    m_width = width;
    m_height = height;
    m_occluded = false;

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

    return true;
}

bool DxDevice::CreateSwapchain(UINT width, UINT height)
{
    if (!m_factory || !m_device)
        return false;

    // If we previously created a waitable object, close it before recreating the swapchain.
    if (m_frameLatencyWaitableObject)
    {
        CloseHandle(m_frameLatencyWaitableObject);
        m_frameLatencyWaitableObject = nullptr;
    }
    m_swap2.Reset();
    m_swap.Reset();

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
    UINT baseFlags = m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    // First try to enable the frame-latency waitable object (biggest win for input latency + smoothness).
    desc.Flags = baseFlags | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    ComPtr<IDXGISwapChain1> swap;
    HRESULT hr = m_factory->CreateSwapChainForHwnd(
        m_device.Get(),
        m_hwnd,
        &desc,
        nullptr,
        nullptr,
        swap.GetAddressOf());

    if (FAILED(hr))
    {
        // Older OS / unusual drivers may reject the waitable-object flag. Fall back to a normal flip swapchain.
        desc.Flags = baseFlags;
        hr = m_factory->CreateSwapChainForHwnd(
            m_device.Get(),
            m_hwnd,
            &desc,
            nullptr,
            nullptr,
            swap.GetAddressOf());

        if (FAILED(hr))
            return false;
    }

    m_swap = swap;
    m_swapChainFlags = desc.Flags;
    m_occluded = false;

    // Optional: configure per-swapchain max frame latency and retrieve the wait handle.
    if ((m_swapChainFlags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) != 0)
    {
        if (SUCCEEDED(m_swap.As(&m_swap2)) && m_swap2)
        {
            SetMaximumFrameLatency(m_maxFrameLatency);

            // Waitable handle: signals when we can begin rendering the next frame.
            m_frameLatencyWaitableObject = m_swap2->GetFrameLatencyWaitableObject();
        }
    }

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
        m_swapChainFlags);

    if (FAILED(hr))
    {
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
            (void)HandleDeviceLost();
        return;
    }

    m_occluded = false;
    CreateRTV();
}

bool DxDevice::TestPresent()
{
    if (!m_swap)
        return false;

    const HRESULT hr = m_swap->Present(0, DXGI_PRESENT_TEST);
    if (hr == DXGI_STATUS_OCCLUDED)
    {
        m_occluded = true;
        return false;
    }

    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
    {
        (void)HandleDeviceLost();
        return false;
    }

    m_occluded = false;
    return true;
}

void DxDevice::Render(bool vsync)
{
    if (!m_ctx || !m_rtv || !m_swap)
        return;

    const float clear[4] = { 0.08f, 0.10f, 0.12f, 1.0f };

    m_ctx->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);
    m_ctx->ClearRenderTargetView(m_rtv.Get(), clear);

    const UINT syncInterval = vsync ? 1u : 0u;

    UINT presentFlags = 0;
    if (!vsync && m_allowTearing)
        presentFlags |= DXGI_PRESENT_ALLOW_TEARING;

    const HRESULT hr = m_swap->Present(syncInterval, presentFlags);

    if (hr == DXGI_STATUS_OCCLUDED)
    {
        // Some present paths may report occlusion; when this happens, the app should
        // stop rendering until visible again (AppWindow handles this).
        m_occluded = true;
        return;
    }

    m_occluded = false;

    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
    {
        (void)HandleDeviceLost();
    }
}

void DxDevice::Shutdown()
{
    DestroyRTV();

    if (m_frameLatencyWaitableObject)
    {
        CloseHandle(m_frameLatencyWaitableObject);
        m_frameLatencyWaitableObject = nullptr;
    }

    if (m_swap2)  m_swap2.Reset();
    if (m_swap)   m_swap.Reset();
    if (m_ctx)    m_ctx.Reset();
    if (m_device) m_device.Reset();
    if (m_factory) m_factory.Reset();

    m_hwnd = nullptr;
    m_allowTearing = false;
    m_occluded = false;
    m_width = 0;
    m_height = 0;
    m_swapChainFlags = 0;
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
