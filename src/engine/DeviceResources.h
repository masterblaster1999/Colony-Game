#pragma once
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdint>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace cg::gfx
{
    using Microsoft::WRL::ComPtr;

    // Basic device & swap-chain owner for a single HWND.
    class DeviceResources
    {
    public:
        struct CreateDesc
        {
            HWND   hwnd = nullptr;
            bool   requestSRGB = true;      // If true, create SRGB RTV for backbuffer (recommended).
            bool   enableDebug = false;     // D3D11 debug layer in Debug builds.
            UINT   backBufferCount = 3;     // 2..16 recommended; triple-buffer by default. (Flip-model) 
            DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM; // UNORM for swapchain
            DXGI_FORMAT depthFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        };

        ~DeviceResources(); // calls ReleaseSizeDependentResources

        void Initialize(const CreateDesc& desc);
        void ResizeIfNeeded();     // call on WM_SIZE or DPI change
        void Present(bool vsync);  // vsync=false => tearing path if supported

        // Accessors
        ID3D11Device*           Device() const          { return m_device.Get(); }
        ID3D11DeviceContext*    Context() const         { return m_context.Get(); }
        IDXGISwapChain1*        SwapChain() const       { return m_swapChain.Get(); }
        ID3D11RenderTargetView* BackbufferRTV() const   { return m_rtv.Get(); }
        ID3D11DepthStencilView* DepthDSV() const        { return m_dsv.Get(); }
        DXGI_FORMAT             BackbufferFormat() const{ return m_backBufferFormat; }
        bool                    TearingSupported() const{ return m_allowTearing; }
        RECT                    ClientRect() const      { return m_client; }

        // Optional: set frame latency (DXGI 1.3+)
        void SetMaxFrameLatency(UINT frames);

    private:
        void CreateDeviceAndFactory(bool enableDebug);
        void CreateSwapChain();
        void CreateSizeDependentResources();
        void ReleaseSizeDependentResources();
        void UpdateClientRect();

        // Helpers
        bool CheckTearingSupport();
        void DisableAltEnter(); // DXGI_MWA_NO_ALT_ENTER

    private:
        HWND                 m_hwnd{};
        RECT                 m_client{};
        UINT                 m_width{0}, m_height{0};

        // D3D11 core
        ComPtr<ID3D11Device>           m_device;
        ComPtr<ID3D11DeviceContext>    m_context;

        // DXGI
        ComPtr<IDXGIFactory6>          m_factory;   // use highest available
        ComPtr<IDXGISwapChain1>        m_swapChain;

        // Views
        ComPtr<ID3D11RenderTargetView> m_rtv;
        ComPtr<ID3D11DepthStencilView> m_dsv;

        // State
        bool        m_requestSRGB{true};
        bool        m_allowTearing{false};
        UINT        m_backBufferCount{3};
        DXGI_FORMAT m_backBufferFormat{DXGI_FORMAT_R8G8B8A8_UNORM};
        DXGI_FORMAT m_depthFormat{DXGI_FORMAT_D24_UNORM_S8_UINT};
    };
}
