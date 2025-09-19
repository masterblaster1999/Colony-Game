#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <string>
#include <wrl/client.h>
#include <d3d11_1.h>
#include <dxgi1_6.h>

namespace gfx
{
    // Lightweight wrapper that owns Device/Context/SwapChain and the default RTV.
    class D3D11Device
    {
    public:
        struct CreateParams
        {
            HWND      hwnd            = nullptr;
            uint32_t  width           = 1280;
            uint32_t  height          = 720;
            bool      useSRGB         = true;   // sRGB RTV for gamma-correct output
            bool      vsync           = true;   // Present sync interval 1 when true
            bool      allowTearing    = true;   // Requires OS support; ignored if unsupported
            bool      enableDebug     = false;  // D3D11 debug layer if available
            uint32_t  backBufferCount = 2;      // 2–3 typical
        };

        D3D11Device() = default;
        ~D3D11Device();

        bool Initialize(const CreateParams& params);
        void Shutdown();

        // Handles window resize; preserves flags, recreates backbuffer view.
        bool Resize(uint32_t width, uint32_t height);

        // Sets and clears the default render target for the frame.
        void BeginFrame(const float clearColor[4]);
        // Presents; returns HRESULT to allow caller to check for device removal.
        HRESULT Present();

        // Marker helpers (no-ops if profiling tool not attached)
        void SetMarker(const wchar_t* name);
        void BeginEvent(const wchar_t* name);
        void EndEvent();

        // Accessors
        ID3D11Device*           Device()        const { return m_device.Get(); }
        ID3D11DeviceContext*    Context()       const { return m_context.Get(); }
        IDXGISwapChain1*        SwapChain()     const { return m_swapChain.Get(); }
        ID3D11RenderTargetView* BackbufferRTV() const { return m_rtv.Get(); }
        DXGI_FORMAT             BackbufferFormat() const { return m_backbufferFormat; }

        // Options toggles (can be changed between frames)
        void SetVSync(bool v)           { m_vsync = v; }
        void SetAllowTearing(bool v)    { m_allowTearing = v && m_tearingSupported; }

        // Recreate everything after DXGI / device removal.
        bool Recreate();

    private:
        bool CreateDeviceAndContext();
        bool CreateSwapChainAndViews();
        void DestroySwapChainAndViews();
        bool CheckTearingSupport();
        void ConfigureDebugLayer();

        // State
        CreateParams m_params{};
        bool m_initialized = false;
        bool m_tearingSupported = false;
        bool m_vsync = true;
        bool m_allowTearing = true;

        uint32_t m_width  = 0;
        uint32_t m_height = 0;

        DXGI_FORMAT m_backbufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM; // swapchain format (UNORM for flip-model)

        // D3D objects
        Microsoft::WRL::ComPtr<ID3D11Device>            m_device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext>     m_context;
        Microsoft::WRL::ComPtr<IDXGISwapChain1>         m_swapChain;
        Microsoft::WRL::ComPtr<IDXGIFactory2>           m_factory2;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>  m_rtv;

        // Optional: profiling markers
        Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> m_annotation;
    };
} // namespace gfx
