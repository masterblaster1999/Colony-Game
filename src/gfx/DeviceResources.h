// src/gfx/DeviceResources.h
#pragma once

#include <array>
#include <stdexcept>
#include <string>
#include <wrl.h>
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>

class DeviceResources
{
public:
    static constexpr UINT FrameCount = 3;

    DeviceResources() = default;
    ~DeviceResources();

    void Initialize(HWND hwnd, UINT width, UINT height, bool vsync);
    void OnResize(UINT width, UINT height); // safe on WM_SIZE
    void Present();                          // handles tearing with vsync off
    void HandleDeviceLost();                 // recreate device/swap chain on removal

    // Accessors you can use in your renderer
    ID3D12Device*           GetDevice()               const { return m_device.Get(); }
    ID3D12CommandQueue*     GetCommandQueue()         const { return m_commandQueue.Get(); }
    IDXGISwapChain3*        GetSwapChain()            const { return m_swapChain.Get(); }
    ID3D12Resource*         GetBackBuffer()           const { return m_renderTargets[m_frameIndex].Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRTV() const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += SIZE_T(m_frameIndex) * m_rtvDescriptorSize;
        return h;
    }
    UINT                    GetWidth()                const { return m_width;  }
    UINT                    GetHeight()               const { return m_height; }
    UINT                    GetCurrentFrameIndex()    const { return m_frameIndex; }
    bool                    IsTearingSupported()      const { return m_allowTearing; }

private:
    // helpers
    void CreateFactory();
    void CreateDevice();
    void CreateCommandQueue();
    void CreateSwapChain();
    void CreateRTVHeapAndTargets();
    void ReleaseSwapChainResources(); // release back buffers before ResizeBuffers
    void WaitForGPU();
    void MoveToNextFrame();

private:
    HWND m_hwnd = nullptr;
    UINT m_width = 0, m_height = 0;
    bool m_vsync = true;
    bool m_allowTearing = false;

    Microsoft::WRL::ComPtr<IDXGIFactory4>   m_factory;
    Microsoft::WRL::ComPtr<ID3D12Device>    m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    UINT m_rtvDescriptorSize = 0;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, FrameCount> m_renderTargets{};
    UINT m_frameIndex = 0;

    // fence/timeline
    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    std::array<UINT64, FrameCount> m_fenceValues{};
    HANDLE m_fenceEvent = nullptr;
};

// Small helper for HRESULT -> exception
inline void ThrowIfFailed(HRESULT hr, const char* msg)
{
    if (FAILED(hr)) throw std::runtime_error(msg);
}
