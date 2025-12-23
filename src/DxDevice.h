#pragma once
#include <windows.h>

#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// Minimal D3D11 device + flip-model swapchain wrapper.
// Windows-only by design for this project.
class DxDevice {
public:
    DxDevice() = default;
    ~DxDevice() { Shutdown(); }

    DxDevice(const DxDevice&) = delete;
    DxDevice& operator=(const DxDevice&) = delete;

    bool Init(HWND hwnd, UINT width, UINT height);
    void Resize(UINT width, UINT height);
    void Render(bool vsync);
    void Shutdown();

    [[nodiscard]] bool SupportsTearing() const noexcept { return m_allowTearing; }

private:
    bool CreateSwapchain(UINT width, UINT height);
    void CreateRTV();
    void DestroyRTV();

    ComPtr<ID3D11Device>           m_device;
    ComPtr<ID3D11DeviceContext>    m_ctx;
    ComPtr<IDXGISwapChain1>        m_swap;
    ComPtr<IDXGIFactory6>          m_factory;
    ComPtr<ID3D11RenderTargetView> m_rtv;

    bool       m_allowTearing = false;
    DXGI_FORMAT m_backbufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM; // flip model prefers UNORM for backbuffer
    HWND       m_hwnd = nullptr;
};
