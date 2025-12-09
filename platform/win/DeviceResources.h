// platform/win/DeviceResources.h
#pragma once
#include <wrl.h>
#include <dxgi1_6.h>
#include <d3d12.h>

class DeviceResources {
public:
    void Initialize(HWND hwnd, uint32_t width, uint32_t height, bool vsync);
    void Resize(uint32_t width, uint32_t height);
    ~DeviceResources();

    ID3D12Device*         GetDevice() const noexcept { return m_device.Get(); }
    IDXGISwapChain3*      GetSwapChain() const noexcept { return m_swapChain.Get(); }
    ID3D12CommandQueue*   GetQueue() const noexcept { return m_queue.Get(); }
    UINT                  CurrentFrameIndex() const noexcept { return m_frameIndex; }

private:
    void CreateFactory();
    void CreateDeviceAndQueue();
    void CreateSwapChain();
    void CreateRTVHeapAndTargets();
    void MoveToNextFrame();

    bool                  m_allowTearing = false;
    bool                  m_vsync = true;
    UINT                  m_frameIndex = 0;

    Microsoft::WRL::ComPtr<IDXGIFactory7>   m_factory;
    Microsoft::WRL::ComPtr<ID3D12Device>    m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_queue;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D12Fence>     m_fence;
    HANDLE                m_fenceEvent = nullptr;
    UINT64                m_fenceValues[3] = {};
    // RTV heap & per-backbuffer RTVs here...
};
