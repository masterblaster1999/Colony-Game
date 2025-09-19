#include "d3d11_device.h"
#include <cassert>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

namespace gfx
{
    static void SetViewport(ID3D11DeviceContext* ctx, uint32_t w, uint32_t h)
    {
        D3D11_VIEWPORT vp{};
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width    = static_cast<float>(w);
        vp.Height   = static_cast<float>(h);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        ctx->RSSetViewports(1, &vp);
    }

    D3D11Device::~D3D11Device()
    {
        Shutdown();
    }

    bool D3D11Device::Initialize(const CreateParams& params)
    {
        m_params = params;
        m_width  = params.width;
        m_height = params.height;
        m_vsync  = params.vsync;

        if (!CreateDeviceAndContext())
            return false;

        ConfigureDebugLayer();

        // Acquire DXGI factory for swapchain creation
        ComPtr<IDXGIDevice> dxgiDevice;
        if (FAILED(m_device.As(&dxgiDevice)))
            return false;

        ComPtr<IDXGIAdapter> adapter;
        if (FAILED(dxgiDevice->GetAdapter(&adapter)))
            return false;

        if (FAILED(adapter->GetParent(IID_PPV_ARGS(&m_factory2))))
            return false;

        m_tearingSupported = CheckTearingSupport();
        m_allowTearing = params.allowTearing && m_tearingSupported;

        if (!CreateSwapChainAndViews())
            return false;

        // Profiling markers
        (void)m_context.As(&m_annotation);

        m_initialized = true;
        return true;
    }

    void D3D11Device::Shutdown()
    {
        DestroySwapChainAndViews();
        m_annotation.Reset();
        m_factory2.Reset();
        m_context.Reset();
        m_device.Reset();
        m_initialized = false;
    }

    bool D3D11Device::Resize(uint32_t width, uint32_t height)
    {
        if (!m_swapChain) return false;

        if (width == 0 || height == 0)
            return true; // Ignore minimization

        m_width  = width;
        m_height = height;

        // Release RTV before resizing buffers
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
        m_rtv.Reset();

        UINT flags = m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
        HRESULT hr = m_swapChain->ResizeBuffers(
            0, // preserve buffer count
            width,
            height,
            DXGI_FORMAT_UNKNOWN, // preserve format
            flags);

        if (FAILED(hr))
            return false;

        // Recreate RTV and viewport
        ComPtr<ID3D11Texture2D> backBuffer;
        if (FAILED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
            return false;

        const DXGI_FORMAT rtvFormat = m_params.useSRGB
            ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
            : DXGI_FORMAT_R8G8B8A8_UNORM;

        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = rtvFormat;
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

        if (FAILED(m_device->CreateRenderTargetView(backBuffer.Get(), &rtvDesc, &m_rtv)))
            return false;

        SetViewport(m_context.Get(), m_width, m_height);
        return true;
    }

    void D3D11Device::BeginFrame(const float clearColor[4])
    {
        ID3D11RenderTargetView* rtv = m_rtv.Get();
        m_context->OMSetRenderTargets(1, &rtv, nullptr);
        m_context->ClearRenderTargetView(m_rtv.Get(), clearColor);
        SetViewport(m_context.Get(), m_width, m_height);
    }

    HRESULT D3D11Device::Present()
    {
        const UINT syncInterval = m_vsync ? 1u : 0u;
        UINT presentFlags = 0;
        if (!m_vsync && m_allowTearing)
            presentFlags |= DXGI_PRESENT_ALLOW_TEARING;

        HRESULT hr = m_swapChain->Present(syncInterval, presentFlags);

        // Handle device-removed/reset
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
        {
            // Attempt to recreate the whole chain
            Recreate();
        }
        return hr;
    }

    void D3D11Device::SetMarker(const wchar_t* name)
    {
        if (m_annotation) m_annotation->SetMarker(name);
    }
    void D3D11Device::BeginEvent(const wchar_t* name)
    {
        if (m_annotation) m_annotation->BeginEvent(name);
    }
    void D3D11Device::EndEvent()
    {
        if (m_annotation) m_annotation->EndEvent();
    }

    bool D3D11Device::Recreate()
    {
        DestroySwapChainAndViews();
        m_context.Reset();
        m_device.Reset();
        m_factory2.Reset();

        if (!CreateDeviceAndContext())
            return false;

        ConfigureDebugLayer();

        // Reacquire factory
        ComPtr<IDXGIDevice> dxgiDevice;
        if (FAILED(m_device.As(&dxgiDevice))) return false;

        ComPtr<IDXGIAdapter> adapter;
        if (FAILED(dxgiDevice->GetAdapter(&adapter))) return false;

        if (FAILED(adapter->GetParent(IID_PPV_ARGS(&m_factory2)))) return false;

        m_tearingSupported = CheckTearingSupport();
        m_allowTearing = m_params.allowTearing && m_tearingSupported;

        return CreateSwapChainAndViews();
    }

    bool D3D11Device::CreateDeviceAndContext()
    {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
        if (m_params.enableDebug) flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        static const D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0
        };
        D3D_FEATURE_LEVEL created{};

        HRESULT hr = D3D11CreateDevice(
            nullptr,                    // default adapter
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            levels, _countof(levels),
            D3D11_SDK_VERSION,
            &m_device,
            &created,
            &m_context);

        return SUCCEEDED(hr);
    }

    bool D3D11Device::CreateSwapChainAndViews()
    {
        assert(m_factory2);
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = m_width;
        desc.Height = m_height;
        desc.Format = m_backbufferFormat;                // keep UNORM under flip-model
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = m_params.backBufferCount;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // prefer flip discard
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        desc.Flags = m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

        ComPtr<IDXGISwapChain1> swapchain;
        HRESULT hr = m_factory2->CreateSwapChainForHwnd(
            m_device.Get(), m_params.hwnd, &desc,
            nullptr, nullptr, &swapchain);

        if (FAILED(hr)) return false;

        // Disable legacy Alt+Enter behavior; handle yourself if desired
        m_factory2->MakeWindowAssociation(m_params.hwnd, DXGI_MWA_NO_ALT_ENTER);

        m_swapChain = swapchain;

        // Create RTV with SRGB view if requested
        ComPtr<ID3D11Texture2D> backBuffer;
        if (FAILED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
            return false;

        const DXGI_FORMAT rtvFormat = m_params.useSRGB
            ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
            : DXGI_FORMAT_R8G8B8A8_UNORM;

        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = rtvFormat;
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

        if (FAILED(m_device->CreateRenderTargetView(backBuffer.Get(), &rtvDesc, &m_rtv)))
            return false;

        SetViewport(m_context.Get(), m_width, m_height);
        return true;
    }

    void D3D11Device::DestroySwapChainAndViews()
    {
        if (m_context) m_context->OMSetRenderTargets(0, nullptr, nullptr);
        m_rtv.Reset();
        m_swapChain.Reset();
    }

    bool D3D11Device::CheckTearingSupport()
    {
        // Tearing support requires DXGI 1.5 (IDXGIFactory5)
        ComPtr<IDXGIFactory5> factory5;
        BOOL allowTearing = FALSE;
        if (SUCCEEDED(m_factory2.As(&factory5)))
        {
            if (FAILED(factory5->CheckFeatureSupport(
                DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                &allowTearing, sizeof(allowTearing))))
            {
                allowTearing = FALSE;
            }
        }
        return !!allowTearing;
    }

    void D3D11Device::ConfigureDebugLayer()
    {
#ifdef _DEBUG
        if (!m_params.enableDebug) return;

        ComPtr<ID3D11Debug> debug;
        if (SUCCEEDED(m_device.As(&debug)))
        {
            ComPtr<ID3D11InfoQueue> infoQueue;
            if (SUCCEEDED(debug.As(&infoQueue)))
            {
                // Optional: break on severe issues to catch misuse early.
                infoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
                infoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
            }
        }
#endif
    }
} // namespace gfx
