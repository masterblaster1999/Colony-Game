// src/platform/win/DxgiSwapchain.cpp
//
// D3D11 swap chain creation helpers (flip model + optional tearing).
// Assumes MSVC / Windows SDK with DXGI 1.5+ available.
//
// This file intentionally avoids defining WIN32_LEAN_AND_MEAN/NOMINMAX unless needed,
// to prevent C4005 redefinition warnings when they are already provided by the build.

#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
  #define NOMINMAX 1
#endif

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>       // IDXGIFactory2/5, CheckFeatureSupport, etc.
#include <wrl/client.h>    // Microsoft::WRL::ComPtr
#include <cassert>
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;

// If your project already provides DX::ThrowIfFailed in a shared header,
// include it instead of this local fallback. This local definition is a
// small, headerless guard to keep this TU independent.
namespace DX {
  inline void ThrowIfFailed(HRESULT hr, const char* what = "DX call failed") {
    if (FAILED(hr)) {
      // In your engine, you likely have a richer error/log facility.
      throw std::runtime_error(std::string(what) + " (hr=0x" + std::to_string(static_cast<unsigned long>(hr)) + ")");
    }
  }
}

// Public helpers are placed under your engine namespace. Adjust to taste.
namespace cg {
namespace win {

//-------------------------------------------------------------------------------------------------
// Helper: find IDXGIFactory2 from an ID3D11Device
//-------------------------------------------------------------------------------------------------
static ComPtr<IDXGIFactory2> GetFactoryFromDevice(ID3D11Device* device) {
  assert(device);

  ComPtr<IDXGIDevice>   dxgiDevice;
  ComPtr<IDXGIAdapter>  adapter;
  ComPtr<IDXGIFactory2> factory2;

  DX::ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice)), "QueryInterface(IDXGIDevice)");
  DX::ThrowIfFailed(dxgiDevice->GetAdapter(&adapter),                 "IDXGIDevice::GetAdapter");
  DX::ThrowIfFailed(adapter->GetParent(IID_PPV_ARGS(&factory2)),      "IDXGIAdapter::GetParent(Factory2)");
  return factory2;
}

//-------------------------------------------------------------------------------------------------
// Tearing support (DXGI 1.5+)
//-------------------------------------------------------------------------------------------------
static bool IsTearingSupported(IDXGIFactory2* factory) {
  if (!factory) return false;

  ComPtr<IDXGIFactory5> factory5;
  if (FAILED(factory->QueryInterface(IID_PPV_ARGS(&factory5)))) {
    return false; // pre‑DXGI 1.5
  }

  BOOL allow = FALSE;
  if (FAILED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow)))) {
    return false;
  }
  return !!allow;
}

//-------------------------------------------------------------------------------------------------
// Compute present flags for Present/Present1 depending on vsync & tearing support
//   - DXGI_PRESENT_ALLOW_TEARING can only be used with syncInterval == 0
//-------------------------------------------------------------------------------------------------
inline UINT ComputePresentFlags(bool vsyncEnabled, bool allowTearing) {
  // If tearing is allowed and we present with interval 0, set the flag; otherwise 0.
  // See Microsoft VRR guidance. 
  // https://learn.microsoft.com/windows/win32/direct3ddxgi/variable-refresh-rate-displays
  return (!vsyncEnabled && allowTearing) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
}

//-------------------------------------------------------------------------------------------------
// Create a flip‑model swap chain for a Win32 HWND using a D3D11 device.
// This is the modern path for D3D11; for D3D12 you would pass a command queue instead.
//   - Backbuffer format: R8G8B8A8_UNORM (linear, not sRGB). Apply gamma in your post pass.
//   - Triple buffering by default.
//   - Optional tearing (VRR) when supported & requested.
//-------------------------------------------------------------------------------------------------
HRESULT CreateSwapChainForHwnd_D3D11(
    ID3D11Device*                    device,
    HWND                             hwnd,
    UINT                             width,
    UINT                             height,
    bool                             requestAllowTearing,
    ComPtr<IDXGISwapChain1>&         outSwapchain /* out */)
{
  assert(device);
  assert(hwnd);
  outSwapchain.Reset();

  auto factory = GetFactoryFromDevice(device);

  // Disable ALT+ENTER (exclusive FSE is deprecated in favor of flip‑model eFSE).
  // See: “Care and Feeding of Modern Swap Chains”. 
  // https://walbourn.github.io/care-and-feeding-of-modern-swap-chains-3/
  factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

  const bool tearSupported = IsTearingSupported(factory.Get());
  const bool allowTearing = requestAllowTearing && tearSupported;

  // --- The creation excerpt you provided, adapted for D3D11 (device instead of queue) ---
  DXGI_SWAP_CHAIN_DESC1 desc{};
  desc.Width       = width;
  desc.Height      = height;
  desc.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;          // backbuffer is *non*‑sRGB; apply gamma in post
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 3;                                   // triple buffering
  desc.SampleDesc  = {1, 0};
  desc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  desc.Flags       = allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;

  ComPtr<IDXGISwapChain1> sc1;
  // For D3D11 the first parameter is the *device*. For D3D12 it would be the *command queue*.
  // https://learn.microsoft.com/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforhwnd
  DX::ThrowIfFailed(factory->CreateSwapChainForHwnd(device, hwnd, &desc, nullptr, nullptr, &sc1),
                    "CreateSwapChainForHwnd");

  // Keep the interface at least at v1; you can QueryInterface up to v3/v4 later if needed.
  DX::ThrowIfFailed(sc1.As(&outSwapchain), "As(IDXGISwapChain1)");

  return S_OK;
}

//-------------------------------------------------------------------------------------------------
// Resize the swap chain buffers (preserving the allow‑tearing flag).
// Pass format UNKNOWN to keep the existing swap chain format; width/height > 0 to change.
//-------------------------------------------------------------------------------------------------
HRESULT ResizeSwapChain_D3D11(
    IDXGISwapChain1* swapchain,
    UINT             width,
    UINT             height,
    bool             allowTearingFlagFromCreate)
{
  if (!swapchain) return E_INVALIDARG;

  const UINT flags = allowTearingFlagFromCreate ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;

  // When using tearing, you must provide the flag on ResizeBuffers as well.
  // https://learn.microsoft.com/windows/win32/direct3ddxgi/variable-refresh-rate-displays
  return swapchain->ResizeBuffers(
      0 /* keep count */,               // 0 -> preserve BufferCount
      width, height,
      DXGI_FORMAT_UNKNOWN,              // preserve format
      flags);
}

} // namespace win
} // namespace cg
