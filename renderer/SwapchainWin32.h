#pragma once
#ifdef _WIN32

#include <cstdint>
#include <stdexcept>
#include <windows.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <wrl/client.h>

namespace cg
{
    using Microsoft::WRL::ComPtr;

    struct SwapchainDesc
    {
        HWND        hwnd            = nullptr;
        UINT        width           = 0;   // 0 => use client size
        UINT        height          = 0;   // 0 => use client size
        DXGI_FORMAT format          = DXGI_FORMAT_R8G8B8A8_UNORM;
        UINT        bufferCount     = 3;   // Flip model: 2 or 3; 3 recommended
        bool        startVsyncOn    = true;
    };

    class SwapchainWin32
    {
    public:
        SwapchainWin32() = default;
        ~SwapchainWin32() = default;

        // queue: ID3D12CommandQueue used for CreateSwapChainForHwnd
        bool initialize(ID3D12CommandQueue* queue, const SwapchainDesc& desc);

        // Resize swap chain (call on WM_SIZE when not minimized and not actively sizing)
        void resize(UINT width, UINT height);

        // Toggle borderless fullscreen (recommended instead of exclusive FSE)
        void toggle_borderless();
        bool is_borderless() const { return m_isBorderless; }

        // Present current frame (vsync can be toggled at runtime).
        void set_vsync(bool on) { m_vsync = on; }
        bool vsync() const { return m_vsync; }
        void present();

        // Accessors
        IDXGISwapChain3* swapchain() const { return m_swapchain.Get(); }
        UINT current_backbuffer_index() const { return m_frameIndex; }
        bool tearing_supported() const { return m_tearingSupported; }

    private:
        void create_factory_();
        void create_swapchain_();
        static RECT window_client_rect_(HWND hwnd);

        ComPtr<IDXGIFactory4>  m_factory;
        ComPtr<IDXGISwapChain3> m_swapchain;
        ComPtr<ID3D12CommandQueue> m_queue;

        HWND        m_hwnd          = nullptr;
        DXGI_FORMAT m_format        = DXGI_FORMAT_R8G8B8A8_UNORM;
        UINT        m_width         = 0;
        UINT        m_height        = 0;
        UINT        m_bufferCount   = 3;
        UINT        m_frameIndex    = 0;

        bool        m_vsync             = true;
        bool        m_tearingSupported  = false;
        bool        m_isBorderless      = false;

        // Restore info for borderless toggle
        RECT  m_windowedRect{};
        DWORD m_windowedStyle = 0;
    };
}
#endif // _WIN32
