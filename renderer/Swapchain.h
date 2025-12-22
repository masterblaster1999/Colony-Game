#pragma once
#ifndef RENDERER_SWAPCHAIN_H
#define RENDERER_SWAPCHAIN_H

// Windows + D3D11/DXGI flip-model swap chain with optional tearing (VRR).
// Link: d3d11.lib, dxgi.lib
// Usage:
//   Swapchain sc;
//   Swapchain::CreateInfo ci{ hwnd, width, height };
//   sc.Initialize(device, context, ci);
//   sc.Present(vsync /*true/false*/);

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

namespace renderer
{
    struct SwapchainCreateInfo
    {
        HWND  hwnd = nullptr;
        UINT  width = 0;
        UINT  height = 0;

        // Backbuffer settings
        DXGI_FORMAT colorFormat = DXGI_FORMAT_R8G8B8A8_UNORM; // swap-chain format
        bool  sRGB = true;            // if true, RTV is created as _SRGB
        bool  tripleBuffer = true;    // BufferCount 3 (otherwise 2)
        bool  allowTearingIfSupported = true; // use DXGI tearing when !vsync and supported
        UINT  maxFrameLatency = 1;    // 1 is typical for low latency
    };

    class Swapchain
    {
    public:
        Swapchain() = default;
        ~Swapchain() = default;

        // device/context are retained (AddRef'd)
        HRESULT Initialize(ID3D11Device* device,
                           ID3D11DeviceContext* context,
                           const SwapchainCreateInfo& info);

        void    Shutdown();

        // Recreate backbuffer/DS after ResizeBuffers.
        HRESULT Resize(UINT width, UINT height);

        // Present with vsync flag (true = syncInterval=1; false = 0 (+ALLOW_TEARING if supported))
        HRESULT Present(bool vsync);

        // Accessors
        ID3D11Device*                Device()       const { return m_device.Get(); }
        ID3D11DeviceContext*         Context()      const { return m_context.Get(); }
        IDXGISwapChain1*             Chain()        const { return m_swapChain.Get(); }
        ID3D11RenderTargetView*      RTV()          const { return m_rtv.Get(); }
        ID3D11DepthStencilView*      DSV()          const { return m_dsv.Get(); }
        ID3D11Texture2D*             BackBuffer()   const { return m_backBuffer.Get(); }
        ID3D11Texture2D*             DepthBuffer()  const { return m_depth.Get(); }
        DXGI_FORMAT                  BackbufferFormat() const { return m_backbufferFormat; }

        UINT  Width()  const { return m_width;  }
        UINT  Height() const { return m_height; }
        bool  SupportsTearing() const { return m_allowTearing; }

    private:
        HRESULT createSwapChain(const SwapchainCreateInfo& info);
        HRESULT createTargets(); // RTV/DSV (called at init and after resize)

        Microsoft::WRL::ComPtr<ID3D11Device>           m_device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext>    m_context;

        Microsoft::WRL::ComPtr<IDXGIFactory2>          m_factory2;   // for CreateSwapChainForHwnd
        Microsoft::WRL::ComPtr<IDXGISwapChain1>        m_swapChain;  // flip-model chain

        Microsoft::WRL::ComPtr<ID3D11Texture2D>        m_backBuffer;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;
        Microsoft::WRL::ComPtr<ID3D11Texture2D>        m_depth;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_dsv;

        DXGI_FORMAT m_backbufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        UINT        m_swapChainFlags   = 0;   // reuse on ResizeBuffers
        UINT        m_bufferCount      = 3;   // 2 or 3
        UINT        m_width            = 0;
        UINT        m_height           = 0;
        bool        m_useSRGB          = true;
        bool        m_allowTearing     = false; // queried via IDXGIFactory5
        HWND        m_hwnd             = nullptr;
    };
}

#endif // RENDERER_SWAPCHAIN_H
