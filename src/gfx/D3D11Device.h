#pragma once
// Windows-only D3D11 device wrapper with VRR/tearing, HDR, depth-stencil,
// device-lost handling, frame pacing, GPU markers, and debug layer support.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>          // ID3DUserDefinedAnnotation
#include <wrl/client.h>

#ifndef __has_include
  #define __has_include(x) 0
#endif
#if __has_include(<dxgi1_6.h>)
  #include <dxgi1_6.h>
#elif __has_include(<dxgi1_5.h>)
  #include <dxgi1_5.h>
#else
  #include <dxgi1_4.h>
#endif

#if __has_include(<d3d11sdklayers.h>)
  #include <d3d11sdklayers.h> // ID3D11InfoQueue (optional)
#endif

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace gfx {

// Clients can subscribe to device-lost/restored to recreate GPU resources.
struct IDeviceNotify {
    virtual ~IDeviceNotify() = default;
    virtual void OnDeviceLost() = 0;  // release GPU-only resources
    virtual void OnDeviceRestored(ID3D11Device* dev, ID3D11DeviceContext* ctx) = 0; // recreate
};

// Backbuffer presentation mode / color pipeline
enum class BackbufferMode : uint8_t {
    SDR_sRGB,     // R8G8B8A8 + sRGB color space (default & safe)
    HDR10_PQ,     // R10G10B10A2 + Rec.2020 + ST.2084 (PQ)
    scRGB_Linear  // R16G16B16A16_FLOAT + linear (full-range RGB)
};

// Small RAII for GPU markers (PIX / RenderDoc)
struct ScopedAnnotation {
    ID3DUserDefinedAnnotation* ann = nullptr;
    explicit ScopedAnnotation(ID3DUserDefinedAnnotation* a, const wchar_t* name) : ann(a) { if (ann) ann->BeginEvent(name); }
    ~ScopedAnnotation() { if (ann) ann->EndEvent(); }
    // non-copyable
    ScopedAnnotation(const ScopedAnnotation&) = delete; ScopedAnnotation& operator=(const ScopedAnnotation&) = delete;
    // movable
    ScopedAnnotation(ScopedAnnotation&& o) noexcept : ann(o.ann) { o.ann = nullptr; }
    ScopedAnnotation& operator=(ScopedAnnotation&& o) noexcept { if (this!=&o){if(ann)ann->EndEvent(); ann=o.ann; o.ann=nullptr;} return *this; }
};

// Device capabilities / stats
struct DeviceCaps {
    bool allowTearing = false;
    bool supportsHDR10 = false;
    bool supportsScRGB = false;
    bool hasDebugLayer = false;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    DXGI_ADAPTER_DESC1 adapterDesc{};      // Description, VendorId, etc.
    wchar_t outputName[128] = L"";         // Primary output name (if available)
};

// Optional initialization parameters (power users)
struct InitParams {
    HWND        hwnd = nullptr;
    uint32_t    width = 0, height = 0;
    bool        vsync = true;
    BackbufferMode mode = BackbufferMode::SDR_sRGB;
    DXGI_FORMAT preferredSDRFormat = DXGI_FORMAT_R8G8B8A8_UNORM; // RTV created as _SRGB if sdrSRGB=true
    bool        sdrSRGB = true;            // create RTV as _SRGB for correct blending in SDR
    UINT        bufferCount = 3;           // recommend 3 for flip model
    UINT        maxFrameLatency = 2;       // used when waitable swap chain is present
    bool        enableDebugLayer = false;  // request D3D11 debug (if installed)
    bool        forceWARP = false;         // software rasterizer fallback
};

class D3D11Device {
public:
    D3D11Device() = default;
    ~D3D11Device() = default;
    D3D11Device(const D3D11Device&) = delete;
    D3D11Device& operator=(const D3D11Device&) = delete;

    // --- Backward-compatible Initialize
    bool Initialize(HWND hwnd, uint32_t width, uint32_t height, bool vsync);

    // --- Extended Initialize with options
    bool Initialize(const InitParams& p);

    void Shutdown();

    // Resize on WM_SIZE (backbuffer & depth)
    bool Resize(uint32_t width, uint32_t height);

    // Toggle vsync at runtime (false => VRR/tearing if supported)
    void SetVSync(bool vs) { vsync_ = vs; }
    bool GetVSync() const { return vsync_; }

    // Switch color pipeline (recreates swap chain & RTV)
    bool SetBackbufferMode(BackbufferMode m);
    BackbufferMode GetBackbufferMode() const { return mode_; }

    // Control sRGB RTV creation in SDR mode (requires SetBackbufferMode(SDR_sRGB))
    bool SetSDR_sRGB(bool enable); // recreates RTV; no swapchain rebuild
    bool GetSDR_sRGB() const { return sdrSRGB_; }

    // Present current frame (handles device-lost/rebuild)
    // Returns HRESULT from IDXGISwapChain::Present (S_OK on success)
    HRESULT Present();

    // Frame pacing helpers (available only if waitable swap chain is supported)
    void SetMaximumFrameLatency(UINT frames);  // default from InitParams
    HANDLE FrameLatencyWaitableObject() const { return frameLatencyEvent_; }
    // Wait until the swap chain is ready to render the next frame (optional)
    DWORD WaitForNextFrame(DWORD timeoutMs);

    // Depth-stencil (optional)
    bool   CreateDepthStencil(DXGI_FORMAT fmt = DXGI_FORMAT_D24_UNORM_S8_UINT, bool shaderReadable = false);
    void   DestroyDepthStencil();
    ID3D11DepthStencilView*      DepthStencilView() const { return dsv_.Get(); }
    ID3D11ShaderResourceView*    DepthSRV()         const { return dsvSRV_.Get(); }

    // Borderless fullscreen helper (non-exclusive)
    bool SetFullscreenBorderless(bool on);
    bool IsFullscreenBorderless() const { return isBorderlessFS_; }

    // Accessors
    ID3D11Device*           Device()        const { return device_.Get(); }
    ID3D11DeviceContext*    Context()       const { return context_.Get(); }
    IDXGISwapChain1*        SwapChain()     const { return swapChain_.Get(); }
    ID3D11RenderTargetView* BackbufferRTV() const { return rtv_.Get(); }
    ID3D11Texture2D*        BackbufferTex() const { return backbuffer_.Get(); }
    bool SupportsTearing()  const { return caps_.allowTearing; }
    const DeviceCaps&       Caps()          const { return caps_; }

    // PIX / RenderDoc annotations (BeginEvent/EndEvent via RAII)
    ID3DUserDefinedAnnotation* Annotation() const { return annotation_.Get(); }

    // Optional debug helpers (no-ops if SDK layers absent)
    void EnableDebugBreaks(bool breakOnError, bool breakOnCorruption);
    static void SetDebugName(ID3D11DeviceChild* obj, const char* name);

    // Screenshot (R8G8B8A8 path) -> PNG file. Returns false if unsupported backbuffer format.
    bool SaveBackbufferPNG(const std::wstring& path);

    void SetNotify(IDeviceNotify* n) { notify_ = n; }

    // --- Logging setup (two overloads for compatibility) ---------------------
    // Preferred: pass a sink that accepts const char* (no allocations).
    void SetLog(std::function<void(const char*)> fn) { logFn_ = std::move(fn); }
    // Compatibility: if caller has std::string, we adapt it.
    void SetLog(std::function<void(const std::string&)> fn) {
        logFn_ = [fn = std::move(fn)](const char* s){ fn(std::string(s ? s : "")); };
    }

private:
    // creation
    bool createFactory_(bool enableDebugDXGI);
    bool pickAdapter_(Microsoft::WRL::ComPtr<IDXGIAdapter1>& outAdapter);
    bool createDevice_(bool enableDebug, IDXGIAdapter1* adapter, bool forceWARP);
    bool createSwapChain_();
    bool createBackbufferAndRTV_();
    void releaseSwapChainRT_();

    // presentation pipeline selection & color space
    DXGI_FORMAT chooseBackbufferFormat_() const;
    bool        updateSwapchainColorSpace_();
    bool        queryColorSpaceSupport_(DXGI_COLOR_SPACE_TYPE cs, bool& supported);

    // state transitions
    void handleDeviceLost_(const char* where, HRESULT hr);

    // logging
    void log_(const char* s);
    void log_(const std::string& s); // NEW: accept std::string directly
    void logf_(const char* fmt, ...);

private:
    // DXGI / D3D objects
    Microsoft::WRL::ComPtr<IDXGIFactory2>       factory2_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1>     swapChain_;
    Microsoft::WRL::ComPtr<IDXGISwapChain3>     swapChain3_; // for color space & waitable latency
    Microsoft::WRL::ComPtr<ID3D11Device>        device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>     backbuffer_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>     depthTex_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> dsvSRV_;
    Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> annotation_;

    // optional debug queue
#if __has_include(<d3d11sdklayers.h>)
    Microsoft::WRL::ComPtr<ID3D11InfoQueue>     infoQueue_;
#endif

    // config/state
    HWND        hwnd_ = nullptr;
    RECT        windowedRect_{};           // restore on borderless exit
    LONG        prevStyle_ = 0;
    bool        isBorderlessFS_ = false;

    uint32_t    width_ = 0, height_ = 0;
    bool        vsync_ = true;
    BackbufferMode mode_ = BackbufferMode::SDR_sRGB;
    DXGI_FORMAT sdrFormat_ = DXGI_FORMAT_R8G8B8A8_UNORM;
    bool        sdrSRGB_ = true;
    UINT        bufferCount_ = 3;
    UINT        maxFrameLatency_ = 2;

    HANDLE      frameLatencyEvent_ = nullptr; // waitable, if available
    DeviceCaps  caps_{};

    // selected formats / color space
    DXGI_FORMAT backbufferFmt_ = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_COLOR_SPACE_TYPE colorSpace_ = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

    IDeviceNotify* notify_ = nullptr;

    // NOTE: renamed from 'log_' (which is now the method) to avoid collision
    std::function<void(const char*)> logFn_;
};

} // namespace gfx
