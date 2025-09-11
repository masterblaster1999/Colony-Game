// -------------------------------------------------------------------------------------------------
// d3d11_device.cpp
// Windows-only Direct3D 11 device + swap chain helper for Colony-Game
// Fixes non‑static member misuse, adds robust error handling, tearing support, resize/present helpers,
// and optional sRGB backbuffer RTV creation.
// -------------------------------------------------------------------------------------------------
// Build: link with d3d11.lib and dxgi.lib
// License: MIT (c) 2025 Colony-Game contributors
// -------------------------------------------------------------------------------------------------

#ifndef UNICODE
#  define UNICODE
#endif
#ifndef _UNICODE
#  define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <wrl/client.h>
#include <d3d11_1.h>
#include <dxgi1_6.h> // newer factory (tearing detection, adapter info)
#include <string>
#include <cstdio>
#include <cstdarg>
#include <utility>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

// -------------------------------------------------------------------------------------------------
// Small utilities
// -------------------------------------------------------------------------------------------------
static inline void dbgprintf(const char* fmt, ...) {
    char buf[2048];
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
}

// Convert UNORM to the matching sRGB format, when such mapping exists.
static DXGI_FORMAT ToSRGB(DXGI_FORMAT f) {
    switch (f) {
        case DXGI_FORMAT_R8G8B8A8_UNORM:   return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case DXGI_FORMAT_B8G8R8A8_UNORM:   return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        case DXGI_FORMAT_B8G8R8X8_UNORM:   return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
        default:                           return f; // no sRGB alias
    }
}

static void SetupD3D11Debug(ComPtr<ID3D11Device>& dev) {
#if defined(_DEBUG)
    ComPtr<ID3D11InfoQueue> iq;
    if (SUCCEEDED(dev.As(&iq)) && iq) {
        // Reduce noise a bit.
        D3D11_MESSAGE_SEVERITY breaks[] = {
            D3D11_MESSAGE_SEVERITY_CORRUPTION,
            D3D11_MESSAGE_SEVERITY_ERROR
        };
        for (auto s : breaks) iq->SetBreakOnSeverity(s, TRUE);
    }
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

    // If caller wants an sRGB view and the format supports it, force that in the RTV desc.
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

// -------------------------------------------------------------------------------------------------
// D3D11Device: small RAII-ish aggregate with helpers
// -------------------------------------------------------------------------------------------------
struct D3D11Device {
    ComPtr<ID3D11Device>           dev;
    ComPtr<ID3D11DeviceContext>    ctx;
    ComPtr<IDXGISwapChain1>        swap;
    ComPtr<ID3D11RenderTargetView> rtv;

    DXGI_FORMAT backFmt = DXGI_FORMAT_B8G8R8A8_UNORM; // default backbuffer format
    bool        allowTearing = false;                 // DXGI_PRESENT_ALLOW_TEARING support

    // Create device + flip-model swap chain + RTV.
    // NOTE: the old code accessed a non-static member (backFmt) from a static function.
    //       We fix it by passing the desired format as a parameter and writing it to
    //       the returned object (no misuse of non-static state).
    static D3D11Device create(HWND hwnd,
                              DXGI_FORMAT fmt                 = DXGI_FORMAT_B8G8R8A8_UNORM,
                              bool enableDebugLayer           = false,
                              bool requestAllowTearingIfAvail = true,
                              bool createSRGBRTV              = false)
    {
        D3D11Device out{};
        out.backFmt = fmt;

        // 1) Create device (hardware → WARP fallback)
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
        if (enableDebugLayer) flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        D3D_FEATURE_LEVEL flsWanted[] = {
            D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
        };
        D3D_FEATURE_LEVEL flOut = D3D_FEATURE_LEVEL_11_0;

        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            flsWanted, (UINT)_countof(flsWanted),
            D3D11_SDK_VERSION, &out.dev, &flOut, &out.ctx
        );
        if (FAILED(hr)) {
            dbgprintf("[d3d] HW device failed (0x%08X). Falling back to WARP.\n", hr);
            hr = D3D11CreateDevice(
                nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                flsWanted, (UINT)_countof(flsWanted),
                D3D11_SDK_VERSION, &out.dev, &flOut, &out.ctx
            );
        }
        if (FAILED(hr)) {
            dbgprintf("[d3d] Failed to create D3D11 device (0x%08X).\n", hr);
            return out; // invalid (dev is null)
        }
        SetupD3D11Debug(out.dev);

        // 2) Factory chain
        ComPtr<IDXGIDevice> dxgiDev; out.dev.As(&dxgiDev);
        ComPtr<IDXGIAdapter> adp;    dxgiDev->GetAdapter(&adp);
        ComPtr<IDXGIFactory2> fac2;  adp->GetParent(IID_PPV_ARGS(&fac2));
        if (!fac2) {
            dbgprintf("[d3d] Could not query IDXGIFactory2.\n");
            return out;
        }

        // 3) Tearing support
        out.allowTearing = requestAllowTearingIfAvail && CheckAllowTearing(fac2);

        // 4) Flip-model swap chain
        DXGI_SWAP_CHAIN_DESC1 d{};
        d.Format = fmt;
        d.Width  = 0; // use current client area
        d.Height = 0;
        d.BufferCount = 2;
        d.SampleDesc = {1, 0};
        d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        d.Scaling = DXGI_SCALING_STRETCH;
        d.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        d.Flags = out.allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

        ComPtr<IDXGISwapChain1> swap;
        hr = fac2->CreateSwapChainForHwnd(out.dev.Get(), hwnd, &d, nullptr, nullptr, &swap);
        if (FAILED(hr)) {
            dbgprintf("[d3d] CreateSwapChainForHwnd failed (0x%08X).\n", hr);
            return out;
        }

        // Block Alt+Enter (we handle fullscreen ourselves if desired)
        if (ComPtr<IDXGIFactory> fac0; SUCCEEDED(fac2.As(&fac0)) && fac0) {
            fac0->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        }

        // 5) RTV
        ComPtr<ID3D11RenderTargetView> rtv;
        hr = CreateRTVFromSwap(out.dev, swap, rtv, fmt, createSRGBRTV);
        if (FAILED(hr)) {
            dbgprintf("[d3d] CreateRenderTargetView (backbuffer) failed (0x%08X).\n", hr);
            return out;
        }

        out.swap = std::move(swap);
        out.rtv  = std::move(rtv);
        return out;
    }

    // Resize swap chain (recreate RTV). Width/height can be zero to use the client rect.
    bool resize(UINT width, UINT height, bool createSRGBRTV = false) {
        if (!swap || !dev) return false;
        rtv.Reset();

        // Keep tearing flag consistent on resize.
        UINT flags = allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
        HRESULT hr = swap->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, flags);
        if (FAILED(hr)) {
            dbgprintf("[d3d] ResizeBuffers failed (0x%08X).\n", hr);
            return false;
        }

        // Refresh RTV after resize
        hr = CreateRTVFromSwap(dev, swap, rtv, backFmt, createSRGBRTV);
        if (FAILED(hr)) {
            dbgprintf("[d3d] Re-create RTV after resize failed (0x%08X).\n", hr);
            return false;
        }
        return true;
    }

    // Present with optional vsync. If tearing is allowed and vsync=false, we present with tearing.
    void present(bool vsync) {
        if (!swap) return;
        UINT flags = (!vsync && allowTearing) ? DXGI_PRESENT_ALLOW_TEARING : 0;
        HRESULT hr = swap->Present(vsync ? 1 : 0, flags);
        if (FAILED(hr)) {
            dbgprintf("[d3d] Present failed (0x%08X).\n", hr);
        }
    }

    // Query current backbuffer size (client size)
    std::pair<UINT,UINT> backbufferSize() const {
        if (!swap) return {0,0};
        DXGI_SWAP_CHAIN_DESC1 desc{};
        if (SUCCEEDED(swap->GetDesc1(&desc))) {
            // Width/Height might be 0 if using automatic sizing; ask the buffer directly:
            ComPtr<ID3D11Texture2D> bb;
            if (SUCCEEDED(swap->GetBuffer(0, IID_PPV_ARGS(&bb))) && bb) {
                D3D11_TEXTURE2D_DESC td{};
                bb->GetDesc(&td);
                return { td.Width, td.Height };
            }
            return { desc.Width, desc.Height };
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
//                                        /*requestAllowTearingIfAvail*/ true,
//                                        /*createSRGBRTV*/ true);
//
//  if (!gfx.dev) { /* handle error */ }
//
//  // Use gfx.dev / gfx.ctx to render to gfx.rtv; on window resize:
//  gfx.resize(newW, newH, /*createSRGBRTV*/ true);
//
//  // Present:
//  gfx.present(/*vsync*/ true);
// -------------------------------------------------------------------------------------------------
