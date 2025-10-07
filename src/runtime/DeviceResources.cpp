// src/runtime/DeviceResources.cpp
#include "DeviceResources.h"
#include "Diagnostics.h"

using Microsoft::WRL::ComPtr;

static inline UINT Align(UINT v, UINT a) { return (v + (a - 1)) & ~(a - 1); }

DeviceResources::~DeviceResources()
{
  try {
    WaitForGPU();
  } catch(...) {}
  if (m_fenceEvent) { CloseHandle(m_fenceEvent); m_fenceEvent = nullptr; }
}

void DeviceResources::Initialize(HWND hwnd, UINT width, UINT height, bool startVsync)
{
  m_hwnd  = hwnd;
  m_width = width  ? width  : 1280;
  m_height= height ? height : 720;
  m_vsync = startVsync;

  // 1) Debug layer first (Debug builds)
  EnableD3D12DebugLayer();

  // 2) Factory (with debug flags if available)
  CreateFactory();

  // 3) Device + direct queue
  CreateDeviceAndQueue();

  // 4) Tearing support
  m_allowTearing = CheckTearingSupport();

  // 5) Swapchain + RTVs
  CreateSwapChain();
  CreateRTVHeapAndTargets();

  // 6) Fence/event
  DX_THROW_IF_FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
  m_fenceValue = 1;
  m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  if (!m_fenceEvent) throw std::runtime_error("CreateEvent failed");
}

void DeviceResources::CreateFactory()
{
  UINT factoryFlags = 0;
#if D3D12_ENABLE_DEBUG_LAYER
  factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
  ComPtr<IDXGIFactory6> factory;
  DX_THROW_IF_FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory)));
  m_factory = factory;

  // We'll handle Alt+Enter ourselves (borderless toggle) if you wire it—disable DXGI's default.
  if (m_hwnd) {
    m_factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);
  }
}

void DeviceResources::CreateDeviceAndQueue()
{
  // Pick a hardware adapter (skip software/wrapper adapters)
  ComPtr<IDXGIAdapter1> adapter;
  for (UINT i = 0; m_factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
  {
    DXGI_ADAPTER_DESC1 desc{};
    adapter->GetDesc1(&desc);
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

    // Try to create a D3D12 device on this adapter
    if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
      break; // adapter is good
    adapter.Reset();
  }

  // If not found, fall back to WARP (software) to at least launch gracefully
  if (!adapter) {
    ComPtr<IDXGIAdapter> warp;
    DX_THROW_IF_FAILED(m_factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
    DX_THROW_IF_FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));
  } else {
    DX_THROW_IF_FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));
  }

  // Create a direct command queue
  D3D12_COMMAND_QUEUE_DESC qdesc{};
  qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  qdesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  DX_THROW_IF_FAILED(m_device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&m_queue)));
}

bool DeviceResources::CheckTearingSupport() const
{
  BOOL allow = FALSE;
  if (m_factory) {
    // DXGI 1.5+: query feature support for tearing
    if (FAILED(m_factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow))))
      allow = FALSE;
  }
  return allow == TRUE;
}

void DeviceResources::CreateSwapChain()
{
  DX_ASSERT(m_hwnd != nullptr);

  // Always destroy old swapchain-dependent resources first
  DestroySwapChainDependentResources();

  DXGI_SWAP_CHAIN_DESC1 scd{};
  scd.Width = m_width;
  scd.Height = m_height;
  scd.Format = kBackBufferFormat;
  scd.Stereo = FALSE;
  scd.SampleDesc = { 1, 0 };
  scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  scd.BufferCount = kBackBufferCount;
  scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // flip model
  scd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
  scd.Flags = m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

  ComPtr<IDXGISwapChain1> sc1;
  DX_THROW_IF_FAILED(m_factory->CreateSwapChainForHwnd(
      m_queue.Get(),
      m_hwnd,
      &scd,
      nullptr, // fullscreen desc (nullptr => windowed)
      nullptr, // restrict to output (nullptr => all)
      &sc1));

  // Disable automatic Alt+Enter handling (we handle ourselves)
  m_factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);

  DX_THROW_IF_FAILED(sc1.As(&m_swapChain));
  m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void DeviceResources::CreateRTVHeapAndTargets()
{
  // RTV heap
  D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
  rtvDesc.NumDescriptors = kBackBufferCount;
  rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  DX_THROW_IF_FAILED(m_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_rtvHeap)));
  m_rtvStride = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  // Back buffer RTVs
  D3D12_CPU_DESCRIPTOR_HANDLE h = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
  for (UINT i = 0; i < kBackBufferCount; ++i)
  {
    DX_THROW_IF_FAILED(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])));
    m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, h);
    h.ptr += m_rtvStride;
  }
}

void DeviceResources::DestroySwapChainDependentResources()
{
  for (UINT i = 0; i < kBackBufferCount; ++i) m_renderTargets[i].Reset();
  m_rtvHeap.Reset();
  // don't release swapchain here; Resize may need it—handled in Resize()
}

void DeviceResources::Resize(UINT width, UINT height)
{
  if (width == 0 || height == 0) return; // ignore bogus sizes (minimized)
  if (m_minimized) return;

  m_width = width;
  m_height = height;

  WaitForGPU(); // ensure no buffers are in use

  // Release RTVs referencing swapchain buffers before ResizeBuffers
  DestroySwapChainDependentResources();

  UINT flags = m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
  DX_THROW_IF_FAILED(m_swapChain->ResizeBuffers(kBackBufferCount, m_width, m_height, kBackBufferFormat, flags));

  m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
  CreateRTVHeapAndTargets();
}

bool DeviceResources::Present()
{
  if (m_minimized) return false;

  UINT syncInterval = m_vsync ? 1 : 0;
  UINT flags = (!m_vsync && m_allowTearing) ? DXGI_PRESENT_ALLOW_TEARING : 0;

  HRESULT hr = m_swapChain->Present(syncInterval, flags);
  if (FAILED(hr))
  {
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
    {
      HandleDeviceLost();
      return true; // device has been reset; caller might need to rebuild GPU resources
    }
    DX_THROW_IF_FAILED(hr);
  }

  MoveToNextFrame();
  return false;
}

void DeviceResources::MoveToNextFrame()
{
  // Signal and increment the fence value
  const UINT64 signal = m_fenceValue;
  DX_THROW_IF_FAILED(m_queue->Signal(m_fence.Get(), signal));
  ++m_fenceValue;

  // Wait until the previous frame is finished
  if (m_fence->GetCompletedValue() < signal)
  {
    DX_THROW_IF_FAILED(m_fence->SetEventOnCompletion(signal, m_fenceEvent));
    WaitForSingleObject(m_fenceEvent, INFINITE);
  }

  m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void DeviceResources::WaitForGPU()
{
  // Schedule a Signal and wait for it
  const UINT64 signal = m_fenceValue;
  DX_THROW_IF_FAILED(m_queue->Signal(m_fence.Get(), signal));
  ++m_fenceValue;

  DX_THROW_IF_FAILED(m_fence->SetEventOnCompletion(signal, m_fenceEvent));
  WaitForSingleObject(m_fenceEvent, INFINITE);
}

void DeviceResources::HandleDeviceLost()
{
  // Tear down everything that depends on the device/swapchain cleanly and recreate.

  // Ensure GPU is idle (best effort)
  try { WaitForGPU(); } catch(...) {}

  // Release swapchain-dependent resources
  DestroySwapChainDependentResources();

  // Release core objects in reverse order
  m_swapChain.Reset();
  m_queue.Reset();
  m_device.Reset();

  // Recreate
  CreateDeviceAndQueue();
  m_allowTearing = CheckTearingSupport();
  CreateSwapChain();
  CreateRTVHeapAndTargets();

  // Fence stays valid; reset frame index
  m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}
