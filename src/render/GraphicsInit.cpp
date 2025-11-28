// GraphicsInit.cpp
// Windows-only: D3D12/D3D11 device creation with Debug Layer and High-Performance GPU preference.

#include <Windows.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <d3d11_4.h>
#include <d3d12sdklayers.h>
#include <d3d11sdklayers.h>
#include <wrl/client.h>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

static inline void ThrowIfFailed(HRESULT hr) {
    if (FAILED(hr)) throw std::runtime_error("HRESULT failed");
}

// --- Laptop dGPU hints (NVIDIA/AMD) -----------------------------------------
// Export these from the EXE to push the system towards the high-performance GPU.
extern "C" {
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;          // NVIDIA
    __declspec(dllexport) int   AmdPowerXpressRequestHighPerformance = 1;  // AMD
}
// ----------------------------------------------------------------------------

static UINT DxgiFactoryFlags() {
    UINT flags = 0;
#ifdef _DEBUG
    flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
    return flags;
}

static ComPtr<IDXGIFactory6> CreateFactory6() {
    ComPtr<IDXGIFactory6> factory6;
    ThrowIfFailed(CreateDXGIFactory2(DxgiFactoryFlags(), IID_PPV_ARGS(&factory6)));
    return factory6;
}

static ComPtr<IDXGIAdapter1> PickHighPerformanceAdapter() {
    ComPtr<IDXGIFactory6> factory6 = CreateFactory6();
    ComPtr<IDXGIAdapter1> adapter;

    for (UINT idx = 0;; ++idx) {
        if (factory6->EnumAdapterByGpuPreference(
                idx, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(&adapter)) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue; // skip WARP
        return adapter; // first hardware HP adapter
    }

    // Fallback: first hardware adapter, any preference
    ComPtr<IDXGIFactory1> factory1;
    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory1)));
    for (UINT i = 0; factory1->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        return adapter;
    }
    // Ultimate fallback (software); caller may choose to fail
    return adapter;
}

#ifdef _DEBUG
static void EnableD3D12DebugLayer() {
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
        debug->EnableDebugLayer();
    }
}

static void ConfigureD3D12InfoQueue(ID3D12Device* device) {
    ComPtr<ID3D12InfoQueue> iq;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&iq)))) {
        iq->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        iq->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        iq->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

        // Optional spam filters:
        D3D12_MESSAGE_ID hide[] = {
            D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
            D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
            D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE
        };
        D3D12_INFO_QUEUE_FILTER f{};
        f.DenyList.NumIDs   = _countof(hide);
        f.DenyList.pIDList  = hide;
        iq->AddStorageFilterEntries(&f);
    }
}
#endif

// --- D3D12 creation path -----------------------------------------------------
void CreateD3D12DeviceHighPerf(ComPtr<ID3D12Device>& outDevice, ComPtr<IDXGIAdapter1>& outAdapter) {
#ifdef _DEBUG
    EnableD3D12DebugLayer();
#endif

    outAdapter = PickHighPerformanceAdapter();
    ThrowIfFailed(D3D12CreateDevice(outAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&outDevice)));

#ifdef _DEBUG
    ConfigureD3D12InfoQueue(outDevice.Get());
#endif
}

// --- D3D11 creation path -----------------------------------------------------
void CreateD3D11DeviceHighPerf(ComPtr<ID3D11Device>& outDev,
                               ComPtr<ID3D11DeviceContext>& outCtx,
                               D3D_FEATURE_LEVEL* outFL = nullptr) {
    ComPtr<IDXGIAdapter1> adapter = PickHighPerformanceAdapter();

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    static const D3D_FEATURE_LEVEL req[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0
    };
    D3D_FEATURE_LEVEL flOut = D3D_FEATURE_LEVEL_11_0;

    ThrowIfFailed(D3D11CreateDevice(
        adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
        req, _countof(req), D3D11_SDK_VERSION,
        &outDev, &flOut, &outCtx));

#ifdef _DEBUG
    ComPtr<ID3D11InfoQueue> iq;
    if (SUCCEEDED(outDev.As(&iq))) {
        iq->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        iq->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
        iq->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING, TRUE);
    }
#endif
    if (outFL) *outFL = flOut;
}
