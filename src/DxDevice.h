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

    // ---------------------------------------------------------------------------------
    // Low-latency presentation (flip-model waitable swapchain)
    // ---------------------------------------------------------------------------------
    //
    // If the swapchain was created with DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT,
    // DXGI can expose a waitable object that signals when it's OK to begin rendering the
    // next frame (i.e., the swapchain is not still presenting).
    //
    // Using this handle in the message loop avoids CPU spinning and can reduce input
    // latency by limiting how many frames are queued.
    void SetMaximumFrameLatency(UINT maxLatency) noexcept;
    [[nodiscard]] UINT MaximumFrameLatency() const noexcept { return m_maxFrameLatency; }
    [[nodiscard]] HANDLE FrameLatencyWaitableObject() const noexcept { return m_frameLatencyWaitableObject; }

    // Best-effort occlusion tracking (Present can report DXGI_STATUS_OCCLUDED on some paths).
    [[nodiscard]] bool IsOccluded() const noexcept { return m_occluded; }

    // Cheap visibility check: calls Present with DXGI_PRESENT_TEST to update IsOccluded().
    // Returns true if the swapchain is visible (not occluded).
    bool TestPresent();

private:
    // If the D3D device is removed/reset, tear down and recreate everything.
    // Returns false if recreation fails.
    bool HandleDeviceLost();

    bool CreateSwapchain(UINT width, UINT height);
    void CreateRTV();
    void DestroyRTV();

    ComPtr<ID3D11Device>           m_device;
    ComPtr<ID3D11DeviceContext>    m_ctx;
    ComPtr<IDXGISwapChain1>        m_swap;   // base interface (used for Present/Resize/GetBuffer)
    ComPtr<IDXGISwapChain2>        m_swap2;  // optional (waitable object + per-swapchain frame latency)
    ComPtr<IDXGIFactory6>          m_factory;
    ComPtr<ID3D11RenderTargetView> m_rtv;

    bool        m_allowTearing = false;
    bool        m_occluded = false;

    DXGI_FORMAT m_backbufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM; // flip model prefers UNORM for backbuffer
    HWND        m_hwnd = nullptr;

    UINT        m_width = 0;
    UINT        m_height = 0;

    // Swapchain configuration
    UINT        m_swapChainFlags = 0;

    // Low-latency configuration
    UINT        m_maxFrameLatency = 1;
    HANDLE      m_frameLatencyWaitableObject = nullptr;
};
