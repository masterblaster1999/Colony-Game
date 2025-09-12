// -------------------------------------------------------------------------------------------------
// d3d11_device.cpp
// Windows-only Direct3D 11 device + swap chain helper for Colony-Game
//
// - Header hygiene: avoids WIN32_LEAN_AND_MEAN redefinition; uses centralized Windows include
// - Release warning fixes (C4100) for debug-only parameters
// - Adapter selection (prefer high-performance GPU via IDXGIFactory6 when available)
// - Robust device creation (debug layer attempt → fallback, HW → WARP fallback)
// - Flip-model swap chain, optional tearing, optional sRGB RTV
// - Resize/present helpers, frame begin/end, fullscreen toggle, HDR colorspace helper
//
// Build: link with d3d11.lib and dxgi.lib
// License: MIT (c) 2025 Colony-Game contributors
// -------------------------------------------------------------------------------------------------

#if !defined(_WIN32)
#  error "This file is Windows-only (D3D11)."
#endif

// Try to use the centralized Windows policy if available.
// Falls back to a guarded minimal include to prevent macro redefinitions.
#if __has_include("platform/win_base.hpp")
  #include "platform/win_base.hpp"
#else
  #ifndef UNICODE
  #  define UNICODE
  #endif
  #ifndef _UNICODE
  #  define _UNICODE
  #endif
  #ifndef NOMINMAX
  #  define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
  #  define WIN32_LEAN_AND_MEAN
  #endif
  #include <Windows.h>
  #ifndef CG_UNUSED
  #  define CG_UNUSED(x) (void)(x)
  #endif
#endif

#include <wrl/client.h>
#include <d3d11_1.h>
#include <dxgi1_6.h>
#include <string>
#include <cstdarg>
#include <cstdio>
#include <utility>
#include <algorithm>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

// -------------------------------------------------------------------------------------------------
// Small utilities
// -------------------------------------------------------------------------------------------------
#ifndef CG_UNUSED
#  define CG_UNUSED(x) (void)(x)
#endif

static inline void dbgprintf(const char* fmt, ...) {
    char buf[2048];
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
}

static inline std::string hr_to_string(HRESULT hr) {
    char* msg = nullptr;
    DWORD len = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                               nullptr, (DWORD)hr, 0, (LPSTR)&msg, 0, nullptr);
    if (len && msg) {
        std::string s(msg, len);
        LocalFree(msg);
        // Trim trailing newlines.
        while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
        return s;
    }
    char tmp[64];
    _snprintf_s(tmp, _TRUNCATE, "HRESULT(0x%08X)", (unsigned)hr);
    return tmp;
}

// Convert UNORM to the matching sRGB format, when such mapping exists.
static DXGI_FORMAT ToSRGB(DXGI_FORMAT f) {
    switch (f) {
        case DXGI_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case DXGI_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        case DXGI_FORMAT_B8G8R8X8_UNORM: return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
        default:                         return f; // no sRGB alias
    }
}

static void SetupD3D11Debug(ComPtr<ID3D11Device>& dev) {
#if defined(_DEBUG)
    ComPtr<ID3D11InfoQueue> iq;
    if (SUCCEEDED(dev.As(&iq)) && iq) {
        // Reduce noise a bit, but still break on serious issues.
        const D3D11_MESSAGE_SEVERITY breaks[] = {
            D3D11_MESSAGE_SEVERITY_CORRUPTION,
            D3D11_MESSAGE_SEVERITY_ERROR
        };
        for (auto s : breaks) iq->SetBreakOnSeverity(s, TRUE);
    }
#else
    CG_UNUSED(dev);
#endif
}

static bool CheckAllowTearing(ComPtr<IDXGIFactory2>& fac2) {
    ComPtr<IDXGIFactory5> fac5;
    if (FAILED(fac2.As(&fac5)) || !fac5) return false;
    BOOL allow = FALSE;
    if (SUCCEEDED(fac5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                            &allow, sizeof(allow)))) {
        return allow == TRUE;
    }
    return false;
}

// Create RTV for the current swap chain backbuffer; can optionally force sRGB view.
static HRESULT CreateRTVFromSwap(ComPtr<ID3D11Device>& dev,
                                 ComPtr<IDXGISwapChain1>& swap,
                                 ComPtr<ID3D11RenderTargetView>& outRTV,
                                 DXGI_FORMAT backFmt,
                                 bool srgbView)
{
    outRTV.Reset();
    ComPtr<ID3D11Texture2D> back;
    HRESULT hr = swap->GetBuffer(0, IID_PPV_ARGS(&back));
    if (FAILED(hr)) return hr;

    if (srgbView) {
        D3D11_RENDER_TARGET_VIEW_DESC rtd{};
        rtd.Format = ToSRGB(backFmt);
        rtd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        rtd.Texture2D.MipSlice = 0;
        hr = dev->CreateRenderTargetView(back.Get(), &rtd, &outRTV);
    } else {
        hr = dev->CreateRenderTargetView(back.Get(), nullptr, &outRTV);
    }
    return hr;
}

// Pick a hardware adapter (prefer high-performance GPU when available). Returns null on failure.
static ComPtr<IDXGIAdapter1> PickAdapter(ComPtr<IDXGIFactory2> fac2, bool preferHighPerf)
{
    ComPtr<IDXGIAdapter1> adapter;

    // Prefer the newest factory for GPU preference enumeration.
    if (ComPtr<IDXGIFactory6> fac6; SUCCEEDED(fac2.As(&fac6)) && fac6) {
        for (UINT i = 0;; ++i) {
            ComPtr<IDXGIAdapter1> cand;
            const DXGI_GPU_PREFERENCE pref =
                preferHighPerf ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_UNSPECIFIED;
            if (fac6->EnumAdapterByGpuPreference(i, pref, IID_PPV_ARGS(&cand)) == DXGI_ERROR_NOT_FOUND)
                break;

            DXGI_ADAPTER_DESC1 desc{};
            if (SUCCEEDED(cand->GetDesc1(&desc))) {
                if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue; // skip WARP here
                adapter = cand;
                break;
            }
        }
    }

    if (!adapter) {
        // Fallback: first hardware adapter from EnumAdapters1.
        ComPtr<IDXGIFactory1> fac1;
        if (SUCCEEDED(fac2.As(&fac1)) && fac1) {
            for (UINT i = 0;; ++i) {
                ComPtr<IDXGIAdapter1> cand;
                if (fac1->EnumAdapters1(i, &cand) == DXGI_ERROR_NOT_FOUND) break;
                DXGI_ADAPTER_DESC1 desc{};
                if (SUCCEEDED(cand->GetDesc1(&desc))) {
                    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
                    adapter = cand;
                    break;
                }
            }
        }
    }

    if (!adapter) {
        dbgprintf("[d3d] No hardware adapter found. Will rely on WARP.\n");
    }
    return adapter;
}

// -------------------------------------------------------------------------------------------------
// D3D11Device: small RAII-ish aggregate with helpers
// -------------------------------------------------------------------------------------------------
struct D3D11Device {
    ComPtr<ID3D11Device>           dev;
    ComPtr<ID3D11DeviceContext>    ctx;
    ComPtr<IDXGISwapChain1>        swap;
    ComPtr<ID3D11RenderTargetView> rtv;

    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    DXGI_FORMAT       backFmt      = DXGI_FORMAT_B8G8R8A8_UNORM;
    bool              allowTearing = false;

    [[nodiscard]] bool isValid() const noexcept { return dev && ctx; }

    // Create device + flip-model swap chain + RTV.
    // Added parameters (defaulted): preferHighPerformanceGPU, bufferCount
    static D3D11Device create(HWND hwnd,
                              DXGI_FORMAT fmt                 = DXGI_FORMAT_B8G8R8A8_UNORM,
                              bool        enableDebugLayer    = false,
                              bool        requestAllowTearing = true,
                              bool        createSRGBRTV       = false,
                              bool        preferHighPerfGPU   = true,
                              UINT        bufferCount         = 2)
    {
        D3D11Device out{};
        out.backFmt = fmt;

        // 0) Create DXGI factory
        UINT factoryFlags = 0;
#if defined(_DEBUG)
        // Use debug factory if available (harmless if debug tools are missing).
        factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
        ComPtr<IDXGIFactory2> fac2;
        HRESULT hr = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&fac2));
        if (FAILED(hr) || !fac2) {
#if defined(_DEBUG)
            dbgprintf("[d3d] CreateDXGIFactory2 failed (%s). Retrying without debug flag.\n",
                      hr_to_string(hr).c_str());
            hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&fac2));
#endif
            if (FAILED(hr) || !fac2) {
                dbgprintf("[d3d] CreateDXGIFactory2 failed (%s). Aborting device creation.\n",
                          hr_to_string(hr).c_str());
                return out;
            }
        }

        // 1) Pick adapter (prefer discrete GPU if possible)
        ComPtr<IDXGIAdapter1> adapter = PickAdapter(fac2, preferHighPerfGPU);

        // 2) Create device (try Debug in Debug builds, then fallback; then WARP if HW fails)
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
        if (enableDebugLayer) flags |= D3D11_CREATE_DEVICE_DEBUG;
#else
        CG_UNUSED(enableDebugLayer); // silence C4100 in Release builds
#endif
        const D3D_FEATURE_LEVEL flsWanted[] = {
            D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
        };

        auto try_create = [&](IDXGIAdapter* adp, D3D_DRIVER_TYPE driver, UINT f) -> HRESULT {
            return D3D11CreateDevice(adp, driver, nullptr, f,
                                     flsWanted, (UINT)_countof(flsWanted),
                                     D3D11_SDK_VERSION, &out.dev, &out.featureLevel, &out.ctx);
        };

        // First attempt: chosen hardware adapter (if any)
        if (adapter) {
            hr = try_create(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, flags);
#if defined(_DEBUG)
            if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
                // Retry without debug flag if debug layer not installed.
                dbgprintf("[d3d] HW+Debug device failed (%s). Retrying w/o Debug.\n",
                          hr_to_string(hr).c_str());
                hr = try_create(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, flags & ~D3D11_CREATE_DEVICE_DEBUG);
            }
#endif
        } else {
            // No adapter: use default hardware selection
            hr = try_create(nullptr, D3D_DRIVER_TYPE_HARDWARE, flags);
#if defined(_DEBUG)
            if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
                dbgprintf("[d3d] HW+Debug device failed (%s). Retrying w/o Debug.\n",
                          hr_to_string(hr).c_str());
                hr = try_create(nullptr, D3D_DRIVER_TYPE_HARDWARE, flags & ~D3D11_CREATE_DEVICE_DEBUG);
            }
#endif
        }

        // Fallback: WARP
        if (FAILED(hr)) {
            dbgprintf("[d3d] HW device creation failed (%s). Falling back to WARP.\n",
                      hr_to_string(hr).c_str());
            hr = try_create(nullptr, D3D_DRIVER_TYPE_WARP, flags & ~D3D11_CREATE_DEVICE_DEBUG);
            if (FAILED(hr)) {
                dbgprintf("[d3d] WARP device creation failed (%s). Aborting.\n",
                          hr_to_string(hr).c_str());
                return out; // invalid
            }
        }

        SetupD3D11Debug(out.dev);

        // 3) Determine tearing support
        out.allowTearing = false;
        if (requestAllowTearing) {
            out.allowTearing = CheckAllowTearing(fac2);
        }

        // 4) Create flip-model swap chain
        bufferCount = std::clamp<UINT>(bufferCount, 2u, 3u); // sensible default clamp
        DXGI_SWAP_CHAIN_DESC1 d{};
        d.Format = fmt;
        d.Width  = 0;   // automatic from client area
        d.Height = 0;
        d.BufferCount = bufferCount;
        d.SampleDesc = {1, 0};
        d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        d.Scaling = DXGI_SCALING_STRETCH;
        d.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        d.Flags = out.allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

        hr = fac2->CreateSwapChainForHwnd(out.dev.Get(), hwnd, &d, nullptr, nullptr, &out.swap);
        if (FAILED(hr) || !out.swap) {
            dbgprintf("[d3d] CreateSwapChainForHwnd failed (%s).\n", hr_to_string(hr).c_str());
            out.destroy();
            return {};
        }

        // Block Alt+Enter (we manage fullscreen ourselves)
        if (ComPtr<IDXGIFactory> fac0; SUCCEEDED(fac2.As(&fac0)) && fac0) {
            fac0->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        }

        // 5) Create RTV
        HRESULT hrRTV = CreateRTVFromSwap(out.dev, out.swap, out.rtv, fmt, createSRGBRTV);
        if (FAILED(hrRTV)) {
            dbgprintf("[d3d] CreateRenderTargetView(backbuffer) failed (%s).\n",
                      hr_to_string(hrRTV).c_str());
            out.destroy();
            return {};
        }

        return out;
    }

    // Resize swap chain (recreate RTV). Width/height can be zero to use the client rect.
    bool resize(UINT width, UINT height, bool createSRGBRTV = false) {
        if (!swap || !dev) return false;
        rtv.Reset();

        UINT flags = allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
        HRESULT hr = swap->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, flags);
        if (FAILED(hr)) {
            dbgprintf("[d3d] ResizeBuffers failed (%s).\n", hr_to_string(hr).c_str());
            return false;
        }

        hr = CreateRTVFromSwap(dev, swap, rtv, backFmt, createSRGBRTV);
        if (FAILED(hr)) {
            dbgprintf("[d3d] Re-create RTV after resize failed (%s).\n", hr_to_string(hr).c_str());
            return false;
        }
        return true;
    }

    // Present with optional vsync. If tearing is allowed and vsync=false, present with tearing.
    // Returns false if the device was removed/reset.
    bool present(bool vsync) {
        if (!swap) return false;
        const UINT flags = (!vsync && allowTearing) ? DXGI_PRESENT_ALLOW_TEARING : 0;
        HRESULT hr = swap->Present(vsync ? 1 : 0, flags);
        if (FAILED(hr)) {
            dbgprintf("[d3d] Present failed (%s).\n", hr_to_string(hr).c_str());
            if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
                // Surface the specific removal reason for easier diagnostics.
                if (dev) {
                    HRESULT rr = dev->GetDeviceRemovedReason();
                    dbgprintf("[d3d] Device removed/reset. Reason: %s\n", hr_to_string(rr).c_str());
                }
                return false;
            }
        }
        return true;
    }

    // Begin a frame: clear the backbuffer and set viewport to its size.
    void beginFrame(const float clearColor[4]) {
        if (!ctx || !rtv) return;
        ctx->ClearRenderTargetView(rtv.Get(), clearColor);

        if (ComPtr<ID3D11Texture2D> bb; SUCCEEDED(swap->GetBuffer(0, IID_PPV_ARGS(&bb))) && bb) {
            D3D11_TEXTURE2D_DESC td{}; bb->GetDesc(&td);
            D3D11_VIEWPORT vp{};
            vp.TopLeftX = 0.0f; vp.TopLeftY = 0.0f;
            vp.Width = static_cast<float>(td.Width);
            vp.Height = static_cast<float>(td.Height);
            vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;
            ctx->RSSetViewports(1, &vp);
        }

        ID3D11RenderTargetView* rtvs[] = { rtv.Get() };
        ctx->OMSetRenderTargets(1, rtvs, nullptr);
    }

    // End a frame: currently a no-op, but a hook for future per-frame cleanups.
    void endFrame() {
        // No-op for now; useful if you later add transient state to reset each frame.
    }

    // Toggle exclusive fullscreen (not required for flip model, but available if you want it).
    bool setFullscreen(bool enabled) {
        if (!swap) return false;
        HRESULT hr = swap->SetFullscreenState(enabled ? TRUE : FALSE, nullptr);
        if (FAILED(hr)) {
            dbgprintf("[d3d] SetFullscreenState(%d) failed (%s).\n",
                      enabled ? 1 : 0, hr_to_string(hr).c_str());
            return false;
        }
        return true;
    }

    // Try to set a DXGI color space on the swap chain (e.g., HDR10).
    // Returns true if supported and successfully set.
    bool setColorSpaceIfSupported(DXGI_COLOR_SPACE_TYPE cs) {
        if (!swap) return false;
        ComPtr<IDXGISwapChain3> sc3;
        if (FAILED(swap.As(&sc3)) || !sc3) return false;

        UINT support = 0;
        if (FAILED(sc3->CheckColorSpaceSupport(cs, &support))) return false;
        if (!(support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)) return false;

        HRESULT hr = sc3->SetColorSpace1(cs);
        if (FAILED(hr)) {
            dbgprintf("[d3d] SetColorSpace1 failed (%s).\n", hr_to_string(hr).c_str());
            return false;
        }
        return true;
    }

    // Set maximum frame latency (IDXGISwapChain2); useful for input latency tuning.
    bool setMaxFrameLatency(UINT latency) {
        if (!swap) return false;
        ComPtr<IDXGISwapChain2> sc2;
        if (FAILED(swap.As(&sc2)) || !sc2) return false;
        HRESULT hr = sc2->SetMaximumFrameLatency(latency);
        if (FAILED(hr)) {
            dbgprintf("[d3d] SetMaximumFrameLatency(%u) failed (%s).\n",
                      latency, hr_to_string(hr).c_str());
            return false;
        }
        return true;
    }

    // Query current backbuffer size (client size)
    std::pair<UINT,UINT> backbufferSize() const {
        if (!swap) return {0,0};
        ComPtr<ID3D11Texture2D> bb;
        if (SUCCEEDED(swap->GetBuffer(0, IID_PPV_ARGS(&bb))) && bb) {
            D3D11_TEXTURE2D_DESC td{}; bb->GetDesc(&td);
            return { td.Width, td.Height };
        }
        return {0,0};
    }

    // Destroy all D3D objects explicitly (optional; ComPtr also handles it).
    void destroy() {
        rtv.Reset();
        swap.Reset();
        ctx.Reset();
        dev.Reset();
    }
};

// -------------------------------------------------------------------------------------------------
// (Optional) tiny usage example — not compiled by default.
//
//  D3D11Device gfx = D3D11Device::create(hwnd,
//                                        DXGI_FORMAT_B8G8R8A8_UNORM,
//                                        /*enableDebugLayer*/ true,
//                                        /*requestAllowTearing*/ true,
//                                        /*createSRGBRTV*/ true,
//                                        /*preferHighPerfGPU*/ true,
//                                        /*bufferCount*/ 2);
//
//  if (!gfx.isValid()) { /* handle error */ }
//
//  const float clear[4] = {0.02f,0.02f,0.03f,1.0f};
//  gfx.beginFrame(clear);
//  // ... draw using gfx.dev / gfx.ctx to gfx.rtv ...
//  gfx.endFrame();
//
//  // On window resize:
//  gfx.resize(newW, newH, /*createSRGBRTV*/ true);
//
//  // Present:
//  if (!gfx.present(/*vsync*/ true)) {
//      // Device removed/reset path; tear down & recreate if desired.
//  }
//
//  // Optional extras:
//  // gfx.setFullscreen(true);
//  // gfx.setMaxFrameLatency(1);
//  // gfx.setColorSpaceIfSupported(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020); // HDR10
// -------------------------------------------------------------------------------------------------
