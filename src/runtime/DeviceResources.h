// src/runtime/DeviceResources.h
#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include <wrl/client.h>
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

#ifndef NDEBUG
  #define DX_ASSERT(expr) do { if(!(expr)) { __debugbreak(); } } while(0)
#else
  #define DX_ASSERT(expr) do { (void)sizeof(expr); } while(0)
#endif

// A very small exception helper for HRESULTs.
struct DxException : public std::runtime_error {
  HRESULT hr;
  DxException(HRESULT _hr, const char* file, int line, const char* msg = "")
    : std::runtime_error(msg && *msg ? msg : "DxException"), hr(_hr) {}
};

#define DX_THROW_IF_FAILED(hrcall) do { HRESULT _hr=(hrcall); if(FAILED(_hr)) { throw DxException(_hr, __FILE__, __LINE__); } } while(0)

class DeviceResources {
public:
  static constexpr UINT kBackBufferCount = 3;
  static constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

  DeviceResources() = default;
  ~DeviceResources();

  // Create device + swapchain + RTVs
  void Initialize(HWND hwnd, UINT width, UINT height, bool startVsync = true);

  // Window resize (robust/minimize-aware)
  void Resize(UINT width, UINT height);

  // Present the frame; recreates device on removal/reset
  // Returns true if the device was reset (caller may need to rebuild some GPU resources)
  bool Present();

  // Accessors
  ID3D12Device*            Device()            const { return m_device.Get(); }
  ID3D12CommandQueue*      CommandQueue()      const { return m_queue.Get(); }
  IDXGISwapChain4*         SwapChain()         const { return m_swapChain.Get(); }
  ID3D12Resource*          CurrentBackBuffer() const { return m_renderTargets[m_frameIndex].Get(); }
  D3D12_CPU_DESCRIPTOR_HANDLE CurrentRTV()     const {
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(m_frameIndex) * m_rtvStride;
    return h;
  }
  UINT                     Width()             const { return m_width; }
  UINT                     Height()            const { return m_height; }
  UINT                     FrameIndex()        const { return m_frameIndex; }
  void                     SetVsync(bool v)          { m_vsync = v; }
  bool                     GetVsync()          const { return m_vsync; }
  bool                     TearingAllowed()    const { return m_allowTearing; }

  // Call this when your app is minimized/restored to avoid burning CPU/GPU
  void SetMinimized(bool minimized) { m_minimized = minimized; }

  // Flush GPU work (e.g., before destroying resources / resizing)
  void WaitForGPU();

  // (Optional) expose fence value for integrations wanting explicit sync
  UINT64 CurrentFenceValue() const { return m_fenceValue; }

private:
  void CreateFactory();
  void CreateDeviceAndQueue();
  void CreateSwapChain();
  void CreateRTVHeapAndTargets();
  void DestroySwapChainDependentResources();

  void MoveToNextFrame(); // per-present fence step
  bool CheckTearingSupport() const;

  // Device loss => teardown + full recreate
  void HandleDeviceLost();

private:
  HWND m_hwnd = nullptr;
  UINT m_width = 0;
  UINT m_height = 0;
  bool m_vsync = true;
  bool m_allowTearing = false;
  bool m_minimized = false;

  Microsoft::WRL::ComPtr<IDXGIFactory6>        m_factory;
  Microsoft::WRL::ComPtr<ID3D12Device>         m_device;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue>   m_queue;
  Microsoft::WRL::ComPtr<IDXGISwapChain4>      m_swapChain;

  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
  UINT m_rtvStride = 0;
  Microsoft::WRL::ComPtr<ID3D12Resource>       m_renderTargets[kBackBufferCount];
  UINT m_frameIndex = 0;

  Microsoft::WRL::ComPtr<ID3D12Fence>          m_fence;
  UINT64 m_fenceValue = 0;
  HANDLE m_fenceEvent = nullptr;
};
