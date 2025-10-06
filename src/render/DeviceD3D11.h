#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <cstdint>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace render
{
    class DeviceD3D11
    {
    public:
        DeviceD3D11() = default;

        void Initialize(HWND hwnd, uint32_t width, uint32_t height, bool enableDebugLayer);
        void Resize(uint32_t width, uint32_t height);
        void BeginFrame(const float clearColor[4]);
        void EndFrame(uint32_t syncInterval = 1);

        // Accessors
        ID3D11Device*           Dev() const { return m_device.Get(); }
        ID3D11DeviceContext*    Ctx() const { return m_context.Get(); }
        ID3D11RenderTargetView* BackbufferRTV() const { return m_rtv.Get(); }

        uint32_t Width()  const { return m_width; }
        uint32_t Height() const { return m_height; }

    private:
        void CreateBackbuffer();
        void DestroyBackbuffer();

        Microsoft::WRL::ComPtr<ID3D11Device>        m_device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
        Microsoft::WRL::ComPtr<IDXGISwapChain>      m_swapchain;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;

        HWND      m_hwnd   = nullptr;
        uint32_t  m_width  = 0;
        uint32_t  m_height = 0;
        bool      m_debug  = false;
    };
} // namespace render
