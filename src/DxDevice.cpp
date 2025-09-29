#include "DxDevice.h"
#include <stdexcept>

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

bool DxDevice::Init(HWND hwnd, UINT width, UINT height)
{
    m_hwnd = hwnd;

    UINT flags = 0;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG; // optional
#endif
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        &fl, 1, D3D11_SDK_VERSION,
        dev.GetAddressOf(), nullptr, ctx.GetAddressOf());

    if (FAILED(hr)) return false;

    m_device = dev; m_ctx = ctx;

    ComPtr<IDXGIDevice> dxgiDev;
    m_device.As(&dxgiDev);
    ComPtr<IDXGIAdapter> adapter;
    dxgiDev->GetAdapter(adapter.GetAddressOf());
    ComPtr<IDXGIFactory6> factory;
    adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()));
    m_factory = factory;

    m_allowTearing = CheckTearing(m_factory.Get()); // DXGI 1.5+

    return CreateSwapchain(width, height);
}

bool DxDevice::CreateSwapchain(UINT width, UINT height)
{
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width  = width;
    desc.Height = height;
    desc.Format = m_backbufferFormat;                 // UNORM backbuffer
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SampleDesc = {1, 0};
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;  // flip-model
    desc.Scaling    = DXGI_SCALING_STRETCH;
    desc.AlphaMode  = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags      = m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    ComPtr<IDXGISwapChain1> swap;
    HRESULT hr = m_factory->CreateSwapChainForHwnd(
        m_device.Get(), m_hwnd, &desc, nullptr, nullptr, swap.GetAddressOf());
    if (FAILED(hr)) return false;

    m_swap = swap;
    CreateRTV();
    return true;
}

void DxDevice::CreateRTV()
{
    DestroyRTV();
    ComPtr<ID3D11Texture2D> backBuffer;
    m_swap->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_rtv.GetAddressOf());
}

void DxDevice::DestroyRTV()
{
    if (m_rtv) m_rtv.Reset();
}

void DxDevice::Resize(UINT width, UINT height)
{
    if (!m_swap || width == 0 || height == 0) return;
    DestroyRTV();
    m_swap->ResizeBuffers(0, width, height, m_backbufferFormat,
        m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
    CreateRTV();
}

void DxDevice::Render(bool vsync)
{
    if (!m_ctx || !m_rtv) return;
    float clear[4] = { 0.08f, 0.10f, 0.12f, 1.0f };
    m_ctx->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);
    m_ctx->ClearRenderTargetView(m_rtv.Get(), clear);

    UINT flags = 0;
    if (!vsync && m_allowTearing)
        flags |= DXGI_PRESENT_ALLOW_TEARING; // only valid with syncInterval==0

    m_swap->Present(vsync ? 1 : 0, flags);
}

void DxDevice::Shutdown()
{
    DestroyRTV();
    if (m_swap) m_swap.Reset();
    if (m_ctx)  m_ctx.Reset();
    if (m_device) m_device.Reset();
}
