// src/gfx/DeviceResources.cpp
#include "DeviceResources.h"
#include "Diagnostics.h"
#include <dxgidebug.h>

#if defined(_MSC_VER)
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#endif

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
}

DeviceResources::~DeviceResources()
{
    if (m_commandQueue && m_fence && m_fenceEvent) WaitForGPU();
    if (m_fenceEvent) CloseHandle(m_fenceEvent);
}

void DeviceResources::Initialize(HWND hwnd, UINT width, UINT height, bool vsync)
{
    m_hwnd  = hwnd;
    m_width = width;
    m_height= height;
    m_vsync = vsync;

#if defined(_DEBUG)
    // Optional: enable GPU validation by setting the env var CG_GPU_VALIDATION=1
    const bool gpuValidate = (::GetEnvironmentVariableA("CG_GPU_VALIDATION", nullptr, 0) > 0);
    EnableD3D12DebugLayer(gpuValidate); // Must be before device creation
#endif

    CreateFactory();
    CreateDevice();
    CreateCommandQueue();
    CreateSwapChain();
    CreateRTVHeapAndTargets();

    // fence init
    ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)),
                  "CreateFence failed");
    m_fenceValues.fill(0);
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent) ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()), "CreateEvent failed");
}

void DeviceResources::CreateFactory()
{
    UINT flags = 0;
#if defined(_DEBUG)
    // Use DXGI debug factory when available (requires Graphics Tools feature)
    ComPtr<IDXGIInfoQueue> infoQueue;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&infoQueue))))
        flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

    ThrowIfFailed(CreateDXGIFactory2(flags, IID_PPV_ARGS(&m_factory)),
                  "CreateDXGIFactory2 failed");

    // Probe tearing support (DXGI 1.5+) via IDXGIFactory5::CheckFeatureSupport
    BOOL allowTearing = FALSE;
    ComPtr<IDXGIFactory5> f5;
    if (SUCCEEDED(m_factory.As(&f5)))
    {
        if (FAILED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                           &allowTearing, sizeof(allowTearing))))
        {
            allowTearing = FALSE;
        }
    }
    m_allowTearing = (allowTearing == TRUE);
    // Tearing requires flip-model swapchains and can only be used with sync interval 0.
    // (We'll honor this in Present().) :contentReference[oaicite:4]{index=4}
}

void DeviceResources::CreateDevice()
{
    // Use default adapter; (you can upgrade to EnumAdapterByGpuPreference if desired)
    ThrowIfFailed(D3D12CreateDevice(
                      nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)),
                  "D3D12CreateDevice failed");
}

void DeviceResources::CreateCommandQueue()
{
    D3D12_COMMAND_QUEUE_DESC desc{};
    desc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    ThrowIfFailed(m_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_commandQueue)),
                  "CreateCommandQueue failed");
}

void DeviceResources::CreateSwapChain()
{
    // Disable DXGI's Alt+Enter handling (we'll implement our own if needed)
    m_factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);

    DXGI_SWAP_CHAIN_DESC1 sc{};
    sc.Width = m_width;
    sc.Height = m_height;
    sc.Format = kBackBufferFormat;
    sc.SampleDesc = {1, 0};
    sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc.BufferCount = FrameCount;
    sc.Scaling = DXGI_SCALING_STRETCH;
    sc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // flip-model recommended
    sc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    sc.Flags = m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0; // allow tearing if supported

    ComPtr<IDXGISwapChain1> sc1;
    ThrowIfFailed(m_factory->CreateSwapChainForHwnd(
                      m_commandQueue.Get(), m_hwnd, &sc, nullptr, nullptr, &sc1),
                  "CreateSwapChainForHwnd failed");

    ThrowIfFailed(sc1.As(&m_swapChain), "Query IDXGISwapChain3 failed");
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Flip model & tearing guidance from Microsoft. :contentReference[oaicite:5]{index=5}
}

void DeviceResources::CreateRTVHeapAndTargets()
{
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.NumDescriptors = FrameCount;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_rtvHeap)),
                  "CreateDescriptorHeap(RTV) failed");

    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE h = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT n = 0; n < FrameCount; ++n)
    {
        ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])),
                      "GetBuffer failed");
        m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, h);
        h.ptr += m_rtvDescriptorSize;
    }
}

void DeviceResources::ReleaseSwapChainResources()
{
    for (auto& rt : m_renderTargets) rt.Reset();
}

void DeviceResources::OnResize(UINT width, UINT height)
{
    if (!m_swapChain) return;
    if (width == 0 || height == 0) return; // minimized
    if (width == m_width && height == m_height) return;

    m_width = width;
    m_height = height;

    WaitForGPU();                    // ensure GPU not using the old buffers
    ReleaseSwapChainResources();     // RELEASE ALL refs to back buffers, per DXGI docs

    UINT flags = m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    HRESULT hr = m_swapChain->ResizeBuffers(FrameCount, m_width, m_height, kBackBufferFormat, flags);

    if (FAILED(hr))
    {
        // If we still hold a reference to a back buffer, ResizeBuffers will fail.
        // Microsoft docs explicitly require releasing all direct/indirect refs first. :contentReference[oaicite:6]{index=6}
        ThrowIfFailed(hr, "ResizeBuffers failed");
    }

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    CreateRTVHeapAndTargets();
}

void DeviceResources::Present()
{
    UINT syncInterval = m_vsync ? 1 : 0;
    UINT presentFlags = 0;
    if (!m_vsync && m_allowTearing)
        presentFlags |= DXGI_PRESENT_ALLOW_TEARING; // only valid with syncInterval==0 per docs :contentReference[oaicite:7]{index=7}

    HRESULT hr = m_swapChain->Present(syncInterval, presentFlags);

    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
    {
        // Device lost: log GetDeviceRemovedReason and rebuild device/swapchain
        HandleDeviceLost();
        return;
    }
    ThrowIfFailed(hr, "Present failed");

    MoveToNextFrame();
}

void DeviceResources::WaitForGPU()
{
    const UINT64 value = ++m_fenceValues[m_frameIndex];
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), value), "Queue->Signal failed");
    ThrowIfFailed(m_fence->SetEventOnCompletion(value, m_fenceEvent), "SetEventOnCompletion failed");
    WaitForSingleObject(m_fenceEvent, INFINITE);
}

void DeviceResources::MoveToNextFrame()
{
    const UINT64 currentValue = ++m_fenceValues[m_frameIndex];
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), currentValue), "Queue->Signal failed");

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent),
                      "SetEventOnCompletion failed");
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

void DeviceResources::HandleDeviceLost()
{
    HRESULT reason = S_OK;
    if (m_device) reason = m_device->GetDeviceRemovedReason(); // retrieve why the device was removed :contentReference[oaicite:8]{index=8}

    // Tear down everything that depends on the device or swap chain
    if (m_commandQueue && m_fence && m_fenceEvent) WaitForGPU();

    ReleaseSwapChainResources();
    m_rtvHeap.Reset();
    m_swapChain.Reset();
    m_commandQueue.Reset();
    m_fence.Reset();
    if (m_fenceEvent) { CloseHandle(m_fenceEvent); m_fenceEvent = nullptr; }
    m_device.Reset();
    m_factory.Reset();

    // Rebuild
    try
    {
        CreateFactory();
        CreateDevice();
        CreateCommandQueue();
        CreateSwapChain();
        CreateRTVHeapAndTargets();

        ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)),
                      "CreateFence (restore) failed");
        m_fenceValues.fill(0);
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!m_fenceEvent) ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()), "CreateEvent failed");
    }
    catch (...)
    {
        // If re-creation fails immediately, bubble up. In production you might retry.
        throw;
    }
}
