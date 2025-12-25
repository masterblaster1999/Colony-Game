#pragma once

#include <windows.h>

#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// Swapchain/device creation options.
//
// This is intentionally small and Windows-only.
struct DxDeviceOptions
{
    // DXGI maximum frame latency (frames queued ahead). Clamp: 1..16.
    UINT maxFrameLatency = 1;

    // DXGI swapchain scaling policy.
    DXGI_SCALING scaling = DXGI_SCALING_NONE;

    // If true, request DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT and
    // expose FrameLatencyWaitableObject() for low-latency message-loop waiting.
    bool enableWaitableObject = true;
};

struct DxRenderStats
{
    HRESULT presentHr = S_OK;
    double presentMs = 0.0; // CPU time spent inside Present()
    bool occluded = false;  // Present returned DXGI_STATUS_OCCLUDED
};

// Minimal D3D11 device + flip-model swapchain wrapper.
// Windows-only by design for this project.
class DxDevice {
public:
    DxDevice() = default;
    ~DxDevice() { Shutdown(); }

    DxDevice(const DxDevice&) = delete;
    DxDevice& operator=(const DxDevice&) = delete;

    bool Init(HWND hwnd, UINT width, UINT height, const DxDeviceOptions& opt = {});
    void Resize(UINT width, UINT height);
    DxRenderStats Render(bool vsync);
    void Shutdown();

    [[nodiscard]] bool SupportsTearing() const noexcept { return m_allowTearing; }

    // Waitable swapchain integration (nullptr when unsupported or disabled).
    [[nodiscard]] HANDLE FrameLatencyWaitableObject() const noexcept { return m_frameLatencyWaitable; }
    [[nodiscard]] bool HasFrameLatencyWaitableObject() const noexcept { return m_frameLatencyWaitable != nullptr; }

    [[nodiscard]] UINT MaxFrameLatency() const noexcept { return m_opt.maxFrameLatency; }
    void SetMaxFrameLatency(UINT v) noexcept;

    [[nodiscard]] DXGI_SCALING Scaling() const noexcept { return m_opt.scaling; }
    void SetScaling(DXGI_SCALING s) noexcept { m_opt.scaling = s; }

private:
    // If the D3D device is removed/reset, tear down and recreate everything.
    // Returns false if recreation fails.
    bool HandleDeviceLost(HRESULT triggeringHr, const wchar_t* stage);

    bool CreateSwapchain(UINT width, UINT height);
    void CreateRTV();
    void DestroyRTV();

    void CloseFrameLatencyHandle() noexcept;
    void ApplyFrameLatencyIfPossible() noexcept;

    ComPtr<ID3D11Device>           m_device;
    ComPtr<ID3D11DeviceContext>    m_ctx;
    ComPtr<IDXGISwapChain1>        m_swap;
    ComPtr<IDXGIFactory6>          m_factory;
    ComPtr<ID3D11RenderTargetView> m_rtv;

    bool        m_allowTearing = false;
    DXGI_FORMAT m_backbufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM; // flip model prefers UNORM
    HWND        m_hwnd = nullptr;

    UINT m_width  = 0;
    UINT m_height = 0;

    // Options requested for creation / device lost recreation.
    DxDeviceOptions m_opt{};

    // Swapchain waitable object (CloseHandle() on shutdown).
    HANDLE m_frameLatencyWaitable = nullptr;
    bool   m_createdWithWaitableFlag = false;

    // The exact flags used to create the current swapchain (must be reused
    // for ResizeBuffers).
    UINT   m_swapchainFlags = 0;
};
