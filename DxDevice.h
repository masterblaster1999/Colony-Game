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

    // Frame helpers:
    //  - BeginFrame() clears and sets up render targets + viewport
    //  - EndFrame() presents (with optional vsync/tearing)
    void BeginFrame();
    void EndFrame(bool vsync);

    // Legacy convenience (kept so older code still compiles).
    void Render(bool vsync) { BeginFrame(); EndFrame(vsync); }
    void Shutdown();

    [[nodiscard]] bool SupportsTearing() const noexcept { return m_allowTearing; }

    [[nodiscard]] UINT Width() const noexcept { return m_width; }
    [[nodiscard]] UINT Height() const noexcept { return m_height; }

    // Low-level accessors for integration layers (ImGui, renderer experiments, etc.)
    [[nodiscard]] ID3D11Device* Device() const noexcept { return m_device.Get(); }
    [[nodiscard]] ID3D11DeviceContext* Context() const noexcept { return m_ctx.Get(); }
    [[nodiscard]] IDXGISwapChain1* SwapChain() const noexcept { return m_swap.Get(); }
    [[nodiscard]] ID3D11RenderTargetView* RenderTargetView() const noexcept { return m_rtv.Get(); }

    // Returns true once after a device-lost recovery recreated the D3D device.
    // Useful for reinitializing ImGui or other device-owned resources.
    [[nodiscard]] bool ConsumeDeviceRecreatedFlag() noexcept
    {
        const bool v = m_deviceRecreated;
        m_deviceRecreated = false;
        return v;
    }

    // Optional: customize clear color.
    void SetClearColor(float r, float g, float b, float a) noexcept
    {
        m_clearColor[0] = r;
        m_clearColor[1] = g;
        m_clearColor[2] = b;
        m_clearColor[3] = a;
    }

private:
    // If the D3D device is removed/reset, tear down and recreate everything.
    // Returns false if recreation fails.
    bool HandleDeviceLost();

    bool CreateSwapchain(UINT width, UINT height);
    void CreateRTV();
    void DestroyRTV();
    void ApplyDefaultViewport() noexcept;

    ComPtr<ID3D11Device>           m_device;
    ComPtr<ID3D11DeviceContext>    m_ctx;
    ComPtr<IDXGISwapChain1>        m_swap;
    ComPtr<IDXGIFactory6>          m_factory;
    ComPtr<ID3D11RenderTargetView> m_rtv;

    bool       m_allowTearing = false;
    DXGI_FORMAT m_backbufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM; // flip model prefers UNORM for backbuffer
    HWND       m_hwnd = nullptr;

    UINT m_width = 0;
    UINT m_height = 0;

    bool  m_deviceRecreated = false;
    float m_clearColor[4] = { 0.08f, 0.10f, 0.12f, 1.0f };
};
