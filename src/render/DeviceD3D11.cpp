#include "DeviceD3D11.h"
#include "HrCheck.h"
#include <dxgi1_2.h>

using Microsoft::WRL::ComPtr;

namespace render
{
    void DeviceD3D11::Initialize(HWND hwnd, uint32_t width, uint32_t height, bool enableDebugLayer)
    {
        m_hwnd   = hwnd;
        m_width  = width;
        m_height = height;
        m_debug  = enableDebugLayer;

        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    #if defined(_DEBUG)
        if (m_debug) flags |= D3D11_CREATE_DEVICE_DEBUG;
    #endif

        // Create device + immediate context
        D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
        HR_CHECK(D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            &fl, 1, D3D11_SDK_VERSION, m_device.ReleaseAndGetAddressOf(),
            nullptr, m_context.ReleaseAndGetAddressOf()));

        // Create a swapchain via the factory associated with this device
        ComPtr<IDXGIDevice> dxgiDevice;
        HR_CHECK(m_device.As(&dxgiDevice));
        ComPtr<IDXGIAdapter> adapter;
        HR_CHECK(dxgiDevice->GetAdapter(adapter.ReleaseAndGetAddressOf()));
        ComPtr<IDXGIFactory> factory;
        HR_CHECK(adapter->GetParent(__uuidof(IDXGIFactory),
                                    reinterpret_cast<void**>(factory.ReleaseAndGetAddressOf())));

        DXGI_SWAP_CHAIN_DESC desc = {};
        desc.BufferCount = 2;
        desc.BufferDesc.Width = width;
        desc.BufferDesc.Height = height;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.OutputWindow = hwnd;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD; // conservative for broad Windows support

        HR_CHECK(factory->CreateSwapChain(m_device.Get(), &desc, m_swapchain.ReleaseAndGetAddressOf()));

        CreateBackbuffer();
    }

    void DeviceD3D11::CreateBackbuffer()
    {
        DestroyBackbuffer();

        ComPtr<ID3D11Texture2D> bb;
        HR_CHECK(m_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                        reinterpret_cast<void**>(bb.ReleaseAndGetAddressOf())));
        HR_CHECK(m_device->CreateRenderTargetView(bb.Get(), nullptr, m_rtv.ReleaseAndGetAddressOf()));
    }

    void DeviceD3D11::DestroyBackbuffer()
    {
        m_rtv.Reset();
    }

    void DeviceD3D11::Resize(uint32_t width, uint32_t height)
    {
        if (!m_swapchain) return;
        if (width == 0 || height == 0) return;

        m_width = width; m_height = height;
        DestroyBackbuffer();
        HR_CHECK(m_swapchain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0));
        CreateBackbuffer();
    }

    void DeviceD3D11::BeginFrame(const float clearColor[4])
    {
        ID3D11RenderTargetView* rt = m_rtv.Get();
        m_context->OMSetRenderTargets(1, &rt, nullptr);

        D3D11_VIEWPORT vp{};
        vp.Width = static_cast<float>(m_width);
        vp.Height = static_cast<float>(m_height);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &vp);

        m_context->ClearRenderTargetView(m_rtv.Get(), clearColor);
    }

    void DeviceD3D11::EndFrame(uint32_t syncInterval)
    {
        HR_CHECK(m_swapchain->Present(syncInterval, 0));
    }
} // namespace render
