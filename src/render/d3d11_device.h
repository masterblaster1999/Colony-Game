// src/render/d3d11_device.h
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdint>

namespace render {

using Microsoft::WRL::ComPtr;

struct D3D11Device
{
    // Init / lifetime
    bool initialize(HWND hwnd, uint32_t width, uint32_t height, bool request_debug_layer = false);
    void shutdown();

    // Resize swapchain buffers and recreate RTV/viewport
    bool resize(uint32_t width, uint32_t height);

    // Frame
    void begin_frame(const float clear_color[4]);
    bool present(bool vsync);

    // Accessors
    ID3D11Device*           device()  const { return m_device.Get(); }
    ID3D11DeviceContext*    context() const { return m_context.Get(); }
    IDXGISwapChain1*        swap_chain() const { return m_swapchain.Get(); }
    ID3D11RenderTargetView* rtv() const { return m_rtv.Get(); }
    D3D_FEATURE_LEVEL       feature_level() const { return m_featureLevel; }
    bool                    is_valid() const { return m_device && m_context && m_swapchain && m_rtv; }
    bool                    tearing_supported() const { return m_tearingSupported; }

private:
    bool try_create_device(D3D_DRIVER_TYPE driverType, UINT flags);
    bool create_swapchain_and_targets(uint32_t width, uint32_t height);
    void destroy_targets();
    bool query_tearing_support();

    HWND                    m_hwnd = nullptr;
    uint32_t                m_width = 0;
    uint32_t                m_height = 0;

    ComPtr<ID3D11Device>            m_device;
    ComPtr<ID3D11DeviceContext>     m_context;
    ComPtr<IDXGISwapChain1>         m_swapchain;
    ComPtr<ID3D11RenderTargetView>  m_rtv;

    D3D_FEATURE_LEVEL       m_featureLevel = D3D_FEATURE_LEVEL_11_0;
    bool                    m_tearingSupported = false;
    bool                    m_debugLayerEnabled = false;
};

} // namespace render
