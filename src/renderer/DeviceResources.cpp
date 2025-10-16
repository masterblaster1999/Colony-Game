#include "DeviceResources.h"
#include <stdexcept>
#include <cassert>

using Microsoft::WRL::ComPtr;

static inline void ThrowIfFailed(HRESULT hr) {
    if (FAILED(hr)) throw std::runtime_error("HRESULT failed");
}

DeviceResources::DeviceResources(HWND hwnd, uint32_t width, uint32_t height) : m_hwnd(hwnd) {
    createDeviceAndFactory();
    createSwapChain(width, height);
    createRenderTarget();
}

DeviceResources::~DeviceResources() {
    if (m_frameLatencyWaitableObject) {
        CloseHandle(m_frameLatencyWaitableObject);
        m_frameLatencyWaitableObject = nullptr;
    }
}

void DeviceResources::createDeviceAndFactory() {
    // Create D3D11 device + immediate context
    UINT devFlags = 0;
#if defined(_DEBUG)
    devFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0
    };
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL flOut{};
    ThrowIfFailed(D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        devFlags, levels, _countof(levels),
        D3D11_SDK_VERSION, device.GetAddressOf(), &flOut, context.GetAddressOf()));
    m_device  = device;
    m_context = context;

    // Create a DXGI factory (debug in Debug builds)
    UINT factoryFlags = 0;
#if defined(_DEBUG)
    factoryFlags |= DXGI_CREATE_FACTORY_DEBUG; // requires Graphics Tools present
#endif
    ComPtr<IDXGIFactory2> fac;
    ThrowIfFailed(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(fac.GetAddressOf())));
    ComPtr<IDXGIFactory6> fac6;
    ThrowIfFailed(fac.As(&fac6));
    m_factory = fac6;

    // Check tearing support (DXGI 1.5+)
    ComPtr<IDXGIFactory5> fac5;
    if (SUCCEEDED(m_factory.As(&fac5))) {
        BOOL allowTearing = FALSE;
        if (SUCCEEDED(fac5->CheckFeatureSupport(
                DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                &allowTearing, sizeof(allowTearing)))) {
            m_allowTearing = (allowTearing == TRUE);
        }
    }
}

void DeviceResources::createSwapChain(uint32_t width, uint32_t height) {
    // Describe flip-model swapchain
    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width  = width;    // can be 0 to auto-size from hwnd
    scd.Height = height;
    scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // back buffer format (UNORM)
    scd.Stereo = FALSE;
    scd.SampleDesc.Count = 1;  // MSAA not supported directly in flip model :contentReference[oaicite:7]{index=7}
    scd.SampleDesc.Quality = 0;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = m_bufferCount;         // triple buffer
    scd.Scaling = DXGI_SCALING_STRETCH;      // or DXGI_SCALING_NONE if you handle letterboxing :contentReference[oaicite:8]{index=8}
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // modern flip model :contentReference[oaicite:9]{index=9}
    scd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    scd.Flags = 0;
    if (m_allowTearing) {
        scd.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING; // needed to use DXGI_PRESENT_ALLOW_TEARING :contentReference[oaicite:10]{index=10}
    }
    scd.Flags |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT; // to use SetMaximumFrameLatency + WaitableObject :contentReference[oaicite:11]{index=11}

    ComPtr<IDXGISwapChain1> swap;
    ThrowIfFailed(m_factory->CreateSwapChainForHwnd(
        m_device.Get(), m_hwnd, &scd,
        nullptr,   // fullscreen desc (null = windowed/borderless recommended) :contentReference[oaicite:12]{index=12}
        nullptr,   // restrict to output
        swap.GetAddressOf()));

    // Disable Alt+Enter legacy path (prefer borderless fullscreen)
    ThrowIfFailed(m_factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER));

    m_swapChain1 = swap;

    // Frame latency pacing
    ComPtr<IDXGISwapChain2> sc2;
    if (SUCCEEDED(m_swapChain1.As(&sc2))) {
        ThrowIfFailed(sc2->SetMaximumFrameLatency(2)); // typical low-latency sweet spot :contentReference[oaicite:13]{index=13}
        m_frameLatencyWaitableObject = sc2->GetFrameLatencyWaitableObject();
    }
}

void DeviceResources::createRenderTarget() {
    m_rtv.Reset();
    m_backBuffer.Reset();

    ThrowIfFailed(m_swapChain1->GetBuffer(0, IID_PPV_ARGS(m_backBuffer.GetAddressOf())));

    // Bind SRGB RTV on an UNORM back buffer (special DXGI case) :contentReference[oaicite:14]{index=14}
    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    ThrowIfFailed(m_device->CreateRenderTargetView(m_backBuffer.Get(), &rtvDesc, m_rtv.GetAddressOf()));
}

void DeviceResources::Resize(uint32_t width, uint32_t height) {
    if (!m_swapChain1) return;

    m_context->OMSetRenderTargets(0, nullptr, nullptr); // unbind before ResizeBuffers
    m_rtv.Reset();
    m_backBuffer.Reset();

    // Note: keep BufferCount & format; pass 0 to reuse previous
    ThrowIfFailed(m_swapChain1->ResizeBuffers(m_bufferCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM,
        (m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0) |
        DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT));
    createRenderTarget();
}

void DeviceResources::BeginFrame() {
    // With waitable swapchain, wait until it’s time to start rendering the next frame
    if (m_frameLatencyWaitableObject) {
        // INFINITE is OK; you can choose a timeout if you need pump work
        WaitForSingleObject(m_frameLatencyWaitableObject, INFINITE); // :contentReference[oaicite:15]{index=15}
    }
    // Clear happens in caller using BackBufferRTV()
}

void DeviceResources::EndFrame(bool vsync) {
    UINT syncInterval = vsync ? 1u : 0u;
    UINT flags = 0u;
    if (!vsync && m_allowTearing) {
        flags |= DXGI_PRESENT_ALLOW_TEARING; // only valid with syncInterval==0 :contentReference[oaicite:16]{index=16}
    }
    // Present
    HRESULT hr = m_swapChain1->Present(syncInterval, flags);
    if (FAILED(hr)) {
        // Handle device lost etc. (omitted here)
    }
}
