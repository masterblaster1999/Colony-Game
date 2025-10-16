#pragma once
#include <wrl/client.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <cstdint>

class DeviceResources {
public:
    DeviceResources(HWND hwnd, uint32_t width, uint32_t height);
    ~DeviceResources();

    void Resize(uint32_t width, uint32_t height);
    void BeginFrame();   // waits on waitable object (if enabled)
    void EndFrame(bool vsync);

    ID3D11Device*           Device()        const { return m_device.Get(); }
    ID3D11DeviceContext*    Context()       const { return m_context.Get(); }
    ID3D11RenderTargetView* BackBufferRTV() const { return m_rtv.Get(); }

    bool TearingSupported() const { return m_allowTearing; }

private:
    void createDeviceAndFactory();
    void createSwapChain(uint32_t width, uint32_t height);
    void createRenderTarget();

private:
    HWND m_hwnd{};
    Microsoft::WRL::ComPtr<IDXGIFactory6>       m_factory;
    Microsoft::WRL::ComPtr<ID3D11Device>        m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain1>     m_swapChain1;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>     m_backBuffer;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;

    HANDLE m_frameLatencyWaitableObject = nullptr;
    bool   m_allowTearing = false;
    UINT   m_bufferCount  = 3; // flip model: 2..16; use 3 by default. :contentReference[oaicite:6]{index=6}
};
