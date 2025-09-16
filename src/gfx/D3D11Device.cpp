// src/gfx/D3D11Device.cpp
#include "D3D11Device.h"
#include <cstdio>
#include <cstdarg>
#include <algorithm>

#include <wincodec.h> // WIC for screenshots
#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;
namespace gfx {

// ------------------------ Helpers ------------------------
static bool IsSoftwareAdapter_(const DXGI_ADAPTER_DESC1& d) {
    return (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
}

static DXGI_COLOR_SPACE_TYPE ColorSpaceForMode_(BackbufferMode m) {
    switch (m) {
        case BackbufferMode::HDR10_PQ:     return DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
        case BackbufferMode::scRGB_Linear: return DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709; // linear 1.0
        case BackbufferMode::SDR_sRGB:
        default:                           return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709; // ~sRGB 2.2
    }
}

static DXGI_FORMAT FormatForMode_(BackbufferMode m, DXGI_FORMAT sdrPreferred) {
    switch (m) {
        case BackbufferMode::HDR10_PQ:     return DXGI_FORMAT_R10G10B10A2_UNORM;
        case BackbufferMode::scRGB_Linear: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case BackbufferMode::SDR_sRGB:
        default:                           return sdrPreferred; // typically R8G8B8A8_UNORM
    }
}

// ------------------------ Public API ------------------------

bool D3D11Device::Initialize(HWND hwnd, uint32_t w, uint32_t h, bool vsync) {
    InitParams p;
    p.hwnd   = hwnd;
    p.width  = w;
    p.height = h;
    p.vsync  = vsync;
    return Initialize(p);
}

bool D3D11Device::Initialize(const InitParams& p) {
    hwnd_            = p.hwnd;
    width_           = std::max(1u, p.width);
    height_          = std::max(1u, p.height);
    vsync_           = p.vsync;
    mode_            = p.mode;
    sdrFormat_       = p.preferredSDRFormat;
    sdrSRGB_         = p.sdrSRGB;
    bufferCount_     = std::max<UINT>(2, p.bufferCount);
    maxFrameLatency_ = std::clamp<UINT>(p.maxFrameLatency, 1, 4);

    if (!createFactory_(/*enableDebugDXGI*/ false)) return false;

    ComPtr<IDXGIAdapter1> adapter;
    if (!pickAdapter_(adapter)) return false;
    if (!createDevice_(p.enableDebugLayer, adapter.Get(), p.forceWARP)) return false;

    if (!createSwapChain_()) return false;
    if (!createBackbufferAndRTV_()) return false;

    // GPU markers (PIX/RenderDoc)
    if (context_) (void)context_.As(&annotation_);

#if __has_include(<d3d11sdklayers.h>)
    if (p.enableDebugLayer) {
        device_.As(&infoQueue_);
        if (infoQueue_) {
            caps_.hasDebugLayer = true;
            // You can enable breaks in code with EnableDebugBreaks(...)
        }
    }
#endif

    return true;
}

void D3D11Device::Shutdown() {
    if (notify_) notify_->OnDeviceLost(); // give app first chance to release GPU resources
    DestroyDepthStencil();
    releaseSwapChainRT_();
    swapChain3_.Reset();
    swapChain_.Reset();
    context_.Reset();
    device_.Reset();
    factory2_.Reset();
    caps_ = DeviceCaps{};
    if (frameLatencyEvent_) { CloseHandle(frameLatencyEvent_); frameLatencyEvent_ = nullptr; }
}

bool D3D11Device::Resize(uint32_t w, uint32_t h) {
    w = std::max(1u, w);
    h = std::max(1u, h);
    if (!swapChain_) { width_=w; height_=h; return true; }

    // Unbind & drop old views
    DestroyDepthStencil();
    releaseSwapChainRT_();

    UINT flags = caps_.allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    HRESULT hr = swapChain_->ResizeBuffers(bufferCount_, w, h, backbufferFmt_, flags);
    if (FAILED(hr)) {
        handleDeviceLost_("ResizeBuffers", hr);
        return false;
    }
    width_=w; height_=h;

    // Recreate RTV (and keep color space as configured)
    if (!createBackbufferAndRTV_()) return false;
    return true;
}

bool D3D11Device::SetBackbufferMode(BackbufferMode m) {
    if (mode_ == m) return true;
    mode_ = m;

    // Full rebuild of swap chain to switch format/colorspace safely
    DestroyDepthStencil();
    releaseSwapChainRT_();
    swapChain3_.Reset();
    swapChain_.Reset();

    if (!createSwapChain_()) return false;
    if (!createBackbufferAndRTV_()) return false;
    return true;
}

bool D3D11Device::SetSDR_sRGB(bool enable) {
    if (mode_ != BackbufferMode::SDR_sRGB) return false;
    if (sdrSRGB_ == enable && rtv_) return true;
    sdrSRGB_ = enable;

    // Recreate RTV (backbuffer stays)
    rtv_.Reset();
    return createBackbufferAndRTV_();
}

HRESULT D3D11Device::Present() {
    UINT sync  = vsync_ ? 1u : 0u;
    UINT flags = (!vsync_ && caps_.allowTearing) ? DXGI_PRESENT_ALLOW_TEARING : 0u; // requires sync==0
    HRESULT hr = swapChain_->Present(sync, flags);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG) {
        handleDeviceLost_("Present", hr);
        return hr;
    }
    if (FAILED(hr)) logf_("Present failed: 0x%08X", hr);
    return hr;
}

void D3D11Device::SetMaximumFrameLatency(UINT frames) {
    maxFrameLatency_ = std::clamp<UINT>(frames, 1, 4);
    if (swapChain3_) {
        swapChain3_->SetMaximumFrameLatency(maxFrameLatency_);
        if (frameLatencyEvent_) { CloseHandle(frameLatencyEvent_); frameLatencyEvent_ = nullptr; }
        frameLatencyEvent_ = swapChain3_->GetFrameLatencyWaitableObject();
    }
}

DWORD D3D11Device::WaitForNextFrame(DWORD timeoutMs) {
    if (!frameLatencyEvent_) return WAIT_FAILED;
    return WaitForSingleObjectEx(frameLatencyEvent_, timeoutMs, TRUE);
}

bool D3D11Device::CreateDepthStencil(DXGI_FORMAT fmt, bool shaderReadable) {
    if (!device_) return false;
    DestroyDepthStencil();

    // To expose SRV on a depth buffer, the *texture* must be typeless.
    DXGI_FORMAT texFmt = fmt;
    DXGI_FORMAT dsvFmt = fmt;
    DXGI_FORMAT srvFmt = DXGI_FORMAT_UNKNOWN;
    if (shaderReadable) {
        switch (fmt) {
            case DXGI_FORMAT_D24_UNORM_S8_UINT:
                texFmt = DXGI_FORMAT_R24G8_TYPELESS;
                dsvFmt = DXGI_FORMAT_D24_UNORM_S8_UINT;
                srvFmt = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
                break;
            case DXGI_FORMAT_D32_FLOAT:
                texFmt = DXGI_FORMAT_R32_TYPELESS;
                dsvFmt = DXGI_FORMAT_D32_FLOAT;
                srvFmt = DXGI_FORMAT_R32_FLOAT;
                break;
            case DXGI_FORMAT_D16_UNORM:
                texFmt = DXGI_FORMAT_R16_TYPELESS;
                dsvFmt = DXGI_FORMAT_D16_UNORM;
                srvFmt = DXGI_FORMAT_R16_UNORM;
                break;
            default: break;
        }
    }

    D3D11_TEXTURE2D_DESC td{};
    td.Width = width_; td.Height = height_;
    td.MipLevels = 1; td.ArraySize = 1;
    td.Format = texFmt;
    td.SampleDesc = {1,0};
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL | (shaderReadable && srvFmt!=DXGI_FORMAT_UNKNOWN ? D3D11_BIND_SHADER_RESOURCE : 0);
    HRESULT hr = device_->CreateTexture2D(&td, nullptr, &depthTex_);
    if (FAILED(hr)) { logf_("CreateTexture2D(depth) failed: 0x%08X", hr); return false; }

    D3D11_DEPTH_STENCIL_VIEW_DESC dd{};
    dd.Format = dsvFmt;
    dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    hr = device_->CreateDepthStencilView(depthTex_.Get(), &dd, &dsv_);
    if (FAILED(hr)) { logf_("CreateDepthStencilView failed: 0x%08X", hr); return false; }

    if (shaderReadable && srvFmt!=DXGI_FORMAT_UNKNOWN) {
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = srvFmt;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        hr = device_->CreateShaderResourceView(depthTex_.Get(), &sd, &dsvSRV_);
        if (FAILED(hr)) logf_("CreateShaderResourceView(depth) failed: 0x%08X", hr);
    }

    // Set default viewport (if app hasn't)
    D3D11_VIEWPORT vp{0,0,(float)width_,(float)height_,0.0f,1.0f};
    context_->RSSetViewports(1, &vp);
    return true;
}

void D3D11Device::DestroyDepthStencil() {
    dsvSRV_.Reset();
    dsv_.Reset();
    depthTex_.Reset();
}

bool D3D11Device::SetFullscreenBorderless(bool on) {
    if (!hwnd_) return false;
    if (on == isBorderlessFS_) return true;

    if (on) {
        GetWindowRect(hwnd_, &windowedRect_);
        prevStyle_ = GetWindowLongW(hwnd_, GWL_STYLE);

        HMONITOR mon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(mon, &mi);

        SetWindowLongW(hwnd_, GWL_STYLE, prevStyle_ & ~(WS_OVERLAPPEDWINDOW));
        SetWindowPos(hwnd_, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        isBorderlessFS_ = true;
    } else {
        SetWindowLongW(hwnd_, GWL_STYLE, prevStyle_);
        SetWindowPos(hwnd_, nullptr,
                     windowedRect_.left, windowedRect_.top,
                     windowedRect_.right - windowedRect_.left,
                     windowedRect_.bottom - windowedRect_.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        isBorderlessFS_ = false;
    }
    return true;
}

void D3D11Device::EnableDebugBreaks(bool breakOnError, bool breakOnCorruption) {
#if __has_include(<d3d11sdklayers.h>)
    if (!infoQueue_) return;
    infoQueue_->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR,      breakOnError ? TRUE : FALSE);
    infoQueue_->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, breakOnCorruption ? TRUE : FALSE);
#else
    (void)breakOnError; (void)breakOnCorruption;
#endif
}

void D3D11Device::SetDebugName(ID3D11DeviceChild* obj, const char* name) {
    if (!obj || !name) return;
    obj->SetPrivateData(WKPDID_D3DDebugObjectName, UINT(strlen(name)), name);
}

bool D3D11Device::SaveBackbufferPNG(const std::wstring& path) {
    if (!backbuffer_) return false;

    // We only implement a direct path for R8G8B8A8 backbuffers here.
    if (backbufferFmt_ != DXGI_FORMAT_R8G8B8A8_UNORM &&
        backbufferFmt_ != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
        log_("SaveBackbufferPNG: unsupported backbuffer format (convert in a postpass).");
        return false;
    }

    // Copy to staging (CPU read)
    D3D11_TEXTURE2D_DESC desc{};
    backbuffer_->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> staging;
    HRESULT hr = device_->CreateTexture2D(&desc, nullptr, &staging);
    if (FAILED(hr)) { logf_("CreateTexture2D(staging) failed: 0x%08X", hr); return false; }

    context_->CopyResource(staging.Get(), backbuffer_.Get());

    D3D11_MAPPED_SUBRESOURCE map{};
    hr = context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map);
    if (FAILED(hr)) { logf_("Map(staging) failed: 0x%08X", hr); return false; }

    // Initialize WIC (best-effort)
    IWICImagingFactory* wicFactory = nullptr;
    HRESULT ci = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&wicFactory));
    if (FAILED(ci)) { context_->Unmap(staging.Get(), 0); return false; }

    // Create a WIC bitmap pointing at our RGBA memory
    IWICBitmap* bmp = nullptr;
    hr = wicFactory->CreateBitmapFromMemory(desc.Width, desc.Height,
                                            GUID_WICPixelFormat32bppRGBA,
                                            map.RowPitch,
                                            map.RowPitch * desc.Height,
                                            static_cast<BYTE*>(map.pData), &bmp);
    if (FAILED(hr)) { wicFactory->Release(); context_->Unmap(staging.Get(), 0); return false; }

    // Encode PNG
    IWICStream* stream = nullptr;
    hr = wicFactory->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);

    IWICBitmapEncoder* enc = nullptr;
    if (SUCCEEDED(hr)) hr = wicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    if (SUCCEEDED(hr)) hr = enc->Initialize(stream, WICBitmapEncoderNoCache);

    IWICBitmapFrameEncode* frame = nullptr;
    if (SUCCEEDED(hr)) hr = enc->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
    if (SUCCEEDED(hr)) hr = frame->SetSize(desc.Width, desc.Height);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppRGBA;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&fmt);
    if (SUCCEEDED(hr)) hr = frame->WriteSource(bmp, nullptr);
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = enc->Commit();

    // Cleanup
    if (frame) frame->Release();
    if (enc) enc->Release();
    if (stream) stream->Release();
    if (bmp) bmp->Release();
    if (wicFactory) wicFactory->Release();

    context_->Unmap(staging.Get(), 0);
    return SUCCEEDED(hr);
}

// ------------------------ Internals ------------------------

bool D3D11Device::createFactory_(bool enableDebugDXGI) {
    UINT fxFlags = 0;
#if defined(_DEBUG)
    if (enableDebugDXGI) fxFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
    HRESULT hr = CreateDXGIFactory2(fxFlags, IID_PPV_ARGS(&factory2_));
    if (FAILED(hr)) {
        // Fallback to non-debug
        hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory2_));
        if (FAILED(hr)) { logf_("DXGI factory creation failed: 0x%08X", hr); return false; }
    }
    if (hwnd_) {
        factory2_->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);
    }
    return true;
}

bool D3D11Device::pickAdapter_(ComPtr<IDXGIAdapter1>& outAdapter) {
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i=0; factory2_->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d{}; adapter->GetDesc1(&d);
        if (!IsSoftwareAdapter_(d)) { outAdapter = adapter; caps_.adapterDesc = d; break; }
        adapter.Reset();
    }
    // Record primary output name (best effort)
    if (outAdapter) {
        ComPtr<IDXGIOutput> out;
        if (SUCCEEDED(outAdapter->EnumOutputs(0, &out)) && out) {
            DXGI_OUTPUT_DESC od{}; if (SUCCEEDED(out->GetDesc(&od))) {
                wcsncpy_s(caps_.outputName, od.DeviceName, _TRUNCATE);
            }
        }
    }
    return true;
}

bool D3D11Device::createDevice_(bool enableDebug, IDXGIAdapter1* adapter, bool forceWARP) {
    UINT devFlags = 0;
#if defined(_DEBUG)
    if (enableDebug) devFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    static const D3D_FEATURE_LEVEL fls[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
    };
    HRESULT hr = E_FAIL;

    if (!forceWARP) {
        hr = D3D11CreateDevice(adapter, adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
                               nullptr, devFlags, fls, _countof(fls), D3D11_SDK_VERSION,
                               &device_, &caps_.featureLevel, &context_);
    }
    if (FAILED(hr)) {
        // Fallback to WARP
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, devFlags, fls, _countof(fls),
                               D3D11_SDK_VERSION, &device_, &caps_.featureLevel, &context_);
        if (FAILED(hr)) { logf_("D3D11CreateDevice failed: 0x%08X", hr); return false; }
    }

    // Tearing support query
    caps_.allowTearing = FALSE;
#if defined(__IDXGIFactory5_INTERFACE_DEFINED__)
    ComPtr<IDXGIFactory5> f5;
    if (SUCCEEDED(factory2_.As(&f5)) && f5) {
        HRESULT e = f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                            &caps_.allowTearing, sizeof caps_.allowTearing);
        if (FAILED(e)) caps_.allowTearing = FALSE;
    }
#endif

    // Conservative HDR flags (final determination after we have a swapchain)
    caps_.supportsHDR10 = false;
    caps_.supportsScRGB = false;
    return true;
}

bool D3D11Device::createSwapChain_() {
    backbufferFmt_ = chooseBackbufferFormat_();
    colorSpace_    = ColorSpaceForMode_(mode_);

    DXGI_SWAP_CHAIN_DESC1 sc{};
    sc.Format = backbufferFmt_;
    sc.Width  = width_;
    sc.Height = height_;
    sc.BufferCount = bufferCount_;
    sc.SampleDesc = {1, 0};                 // no MSAA on flip model backbuffer
    sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sc.Scaling     = DXGI_SCALING_STRETCH;
    sc.AlphaMode   = DXGI_ALPHA_MODE_IGNORE;
    sc.Flags       = caps_.allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    HRESULT hr = factory2_->CreateSwapChainForHwnd(device_.Get(), hwnd_, &sc, nullptr, nullptr, &swapChain_);
    if (FAILED(hr)) { logf_("CreateSwapChainForHwnd failed: 0x%08X", hr); return false; }

    swapChain_.As(&swapChain3_); // might be null on older OS builds

    // Frame pacing (waitable)
    if (swapChain3_) {
        swapChain3_->SetMaximumFrameLatency(maxFrameLatency_);
        frameLatencyEvent_ = swapChain3_->GetFrameLatencyWaitableObject();
    }

    // Color space selection (HDR/SDR)
    updateSwapchainColorSpace_();
    return true;
}

bool D3D11Device::createBackbufferAndRTV_() {
    HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backbuffer_));
    if (FAILED(hr)) { logf_("GetBuffer failed: 0x%08X", hr); return false; }

    // Create sRGB RTV in SDR mode (correct blending) when requested.
    DXGI_FORMAT rtvFmt = backbufferFmt_;
    if (mode_ == BackbufferMode::SDR_sRGB && sdrSRGB_) {
        if (backbufferFmt_ == DXGI_FORMAT_R8G8B8A8_UNORM)       rtvFmt = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        else if (backbufferFmt_ == DXGI_FORMAT_B8G8R8A8_UNORM)  rtvFmt = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    }

    if (rtvFmt != backbufferFmt_) {
        D3D11_RENDER_TARGET_VIEW_DESC rd{};
        rd.Format = rtvFmt;
        rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        hr = device_->CreateRenderTargetView(backbuffer_.Get(), &rd, &rtv_);
    } else {
        hr = device_->CreateRenderTargetView(backbuffer_.Get(), nullptr, &rtv_);
    }
    if (FAILED(hr)) { logf_("CreateRenderTargetView failed: 0x%08X", hr); return false; }

    D3D11_VIEWPORT vp{0,0,(float)width_,(float)height_,0.0f,1.0f};
    context_->RSSetViewports(1, &vp);
    return true;
}

void D3D11Device::releaseSwapChainRT_() {
    if (context_) {
        ID3D11RenderTargetView* nullRTV[1] = { nullptr };
        context_->OMSetRenderTargets(1, nullRTV, nullptr);
    }
    rtv_.Reset();
    backbuffer_.Reset();
}

DXGI_FORMAT D3D11Device::chooseBackbufferFormat_() const {
    return FormatForMode_(mode_, sdrFormat_);
}

bool D3D11Device::queryColorSpaceSupport_(DXGI_COLOR_SPACE_TYPE cs, bool& supported) {
    supported = false;
    if (!swapChain3_) return true; // can't query on old OS; stay conservative
    UINT flags = 0;
    HRESULT hr = swapChain3_->CheckColorSpaceSupport(cs, &flags);
    if (SUCCEEDED(hr) && (flags & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)) {
        supported = true;
        return true;
    }
    return SUCCEEDED(hr);
}

bool D3D11Device::updateSwapchainColorSpace_() {
    if (!swapChain3_) return true; // not available; ignore

    bool supported = false;
    if (!queryColorSpaceSupport_(colorSpace_, supported)) return false;

    if (!supported) {
        // Fallback to SDR if HDR space unsupported
        colorSpace_ = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
        mode_ = BackbufferMode::SDR_sRGB;
    }
    swapChain3_->SetColorSpace1(colorSpace_);

    // Update caps we report outward
    caps_.supportsHDR10 = (mode_ == BackbufferMode::HDR10_PQ)     && supported;
    caps_.supportsScRGB = (mode_ == BackbufferMode::scRGB_Linear) && supported;
    return true;
}

void D3D11Device::handleDeviceLost_(const char* where, HRESULT hr) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "Device lost at %s (hr=0x%08X)", where, hr);
    log_(buf);

    if (notify_) notify_->OnDeviceLost();

    DestroyDepthStencil();
    releaseSwapChainRT_();
    swapChain3_.Reset();
    swapChain_.Reset();
    context_.Reset();
    device_.Reset();

    // Recreate all with existing settings
    if (!createDevice_(caps_.hasDebugLayer, nullptr, false)) return;
    if (!createSwapChain_()) return;
    if (!createBackbufferAndRTV_()) return;

    if (notify_) notify_->OnDeviceRestored(device_.Get(), context_.Get());
}

void D3D11Device::log_(const char* s) {
    if (log_) { log_(std::string(s)); }
#ifdef _DEBUG
    OutputDebugStringA(s); OutputDebugStringA("\n");
#endif
}

void D3D11Device::logf_(const char* fmt, ...) {
    char b[512];
    va_list ap; va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    log_(b);
}

} // namespace gfx
