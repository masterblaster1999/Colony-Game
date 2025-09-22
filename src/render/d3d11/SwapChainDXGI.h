// File: src/render/d3d11/SwapChainDXGI.h
//
// Windows‑only, D3D11 + DXGI flip‑model, waitable swap chain helper.
// Massively expanded edition for Colony‑Game (Windows):
//   • Tearing detection (DXGI 1.5+) and correct Present / Present1 flagging
//   • Waitable frame latency object for tight, low‑jitter pacing
//   • Exclusive fullscreen + borderless fullscreen helpers (with window style snapshot/restore)
//   • Resize / recreate paths that preserve DXGI flags consistency across ResizeBuffers
//   • Color space plumbing (SDR, scRGB, HDR10) + HDR10 metadata helpers (SwapChain4)
//   • Adapter/output introspection (basic descriptors, refresh rate query)
//   • Present1 support (dirty rects / scroll rect) and occlusion handling
//   • Frame statistics, latency knobs, factory association flags (NO_ALT_ENTER, etc.)
//   • Logger callback + lifecycle callbacks (OnResize/OnRecreate) to reinstate dependent resources
//
// This header is self‑contained (implementation expected in SwapChainDXGI.cpp).
// Requires Windows 10 SDK (DXGI 1.6 headers) and links: d3d11.lib, dxgi.lib.
//
// Typical usage:
//   cg::dxgi::SwapChainDXGI sc;
//   cg::dxgi::SwapChainOptions opt{};
//   opt.width = w; opt.height = h; opt.format = DXGI_FORMAT_R8G8B8A8_UNORM; // or _SRGB
//   sc.Initialize(device, hwnd, opt);
//   ...
//   sc.Present({ /*vsync=*/false }); // uses tearing when legal and waits on latency handle
//
#pragma once

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>
#include <dxgi1_4.h>
#include <dxgi1_5.h>
#include <dxgi1_6.h>
#include <wrl/client.h>   // Microsoft::WRL::ComPtr

#include <cstdint>
#include <cstdarg>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#ifndef _WIN32
  #error "SwapChainDXGI.h is Windows-only."
#endif

namespace cg::dxgi {

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------------------------------------------------
// Logging callback (optional)
// ---------------------------------------------------------------------------------------------------------------------
using LogFn = void(*)(const char* fmt, ...);

// Convenience internal logger trampoline (variadic)
inline void _cg_logf(LogFn logger, const char* fmt, ...) {
    if (!logger) return;
    va_list ap; va_start(ap, fmt);
    // Forward to user-provided varargs logger
    // NOTE: We cannot vprintf_s here because logger is unknown; caller must implement formatting.
    // This helper exists only to centralize the "if (logger)" branching when we need it inline.
    logger(fmt, ap);
    va_end(ap);
}

// ---------------------------------------------------------------------------------------------------------------------
// Enumerations / modes
// ---------------------------------------------------------------------------------------------------------------------
enum class FullscreenMode : uint8_t { Windowed, Borderless, Exclusive };

enum class SwapEffectPref : uint8_t { FlipDiscard, FlipSequential };

enum class HDRMode : uint8_t {
    SDR,        // DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709
    scRGB,      // DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 (linear 0..~7.2)
    HDR10       // DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 (ST2084)
};

// ---------------------------------------------------------------------------------------------------------------------
// Simple RAII record for window style/rect when toggling borderless
// ---------------------------------------------------------------------------------------------------------------------
struct WindowedRect {
    RECT  rect{};
    DWORD style     = 0;
    DWORD exStyle   = 0;
    int   showCmd   = SW_SHOWNORMAL;
    bool  valid     = false;
};

// ---------------------------------------------------------------------------------------------------------------------
// Options at creation / recreation
// ---------------------------------------------------------------------------------------------------------------------
struct SwapChainOptions {
    // Backbuffer
    UINT        width       = 0;
    UINT        height      = 0;
    DXGI_FORMAT format      = DXGI_FORMAT_R8G8B8A8_UNORM;  // Use _SRGB if your shader outputs linear
    UINT        bufferCount = 2;                           // 2 (lower latency) or 3 (smoother under load)
    UINT        sampleCount = 1;                           // 1 for flip model; MSAA requires a resolve

    // Behavior
    bool        useWaitableObject    = true;               // FRAME_LATENCY_WAITABLE_OBJECT
    bool        preferTearing        = true;               // ALLOW_TEARING if supported (windowed/borderless)
    bool        disableAltEnter      = true;               // DXGI_MWA_NO_ALT_ENTER
    bool        disableWinChanging   = true;               // DXGI_MWA_NO_WINDOW_CHANGES (we manage styles)
    SwapEffectPref swapEffect        = SwapEffectPref::FlipDiscard;

    UINT        maxFrameLatency      = 1;                  // 1..16 (lower => lower input latency)
    DXGI_SCALING    scaling          = DXGI_SCALING_STRETCH;
    DXGI_ALPHA_MODE alphaMode        = DXGI_ALPHA_MODE_IGNORE;

    // Color management
    HDRMode                 hdrMode       = HDRMode::SDR;  // desired output mode
    DXGI_COLOR_SPACE_TYPE   colorSpace    = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709; // auto-adjusted on init

    // Presentation (advanced defaults)
    bool        enablePresent1   = true;   // allow dirty/scroll rect path when supported
    bool        usePresentStats  = false;  // try to query DXGI stats on Present (best-effort)
};

// ---------------------------------------------------------------------------------------------------------------------
// Present parameters
// ---------------------------------------------------------------------------------------------------------------------
struct PresentArgs {
    bool        vsync          = false;    // true => syncInterval>0, false => 0
    UINT        syncInterval   = 1;        // usually 1 when vsync==true; 2 for half-rate
    UINT        timeoutMs      = 1000;     // wait on latency handle (if any)
    UINT        flagsOverride  = 0;        // advanced override (usually 0; tearing flag auto-added)
    // Present1 extensions (optional)
    bool        usePresent1    = false;
    const RECT* dirtyRects     = nullptr;  // array of dirty rects
    UINT        numDirtyRects  = 0;
    const RECT* scrollRect     = nullptr;  // scrolled area
    POINT       scrollOffset   = {0, 0};   // scroll delta
};

// ---------------------------------------------------------------------------------------------------------------------
// Runtime caps
// ---------------------------------------------------------------------------------------------------------------------
struct SwapChainCaps {
    bool tearingSupported  = false;  // DXGI_FEATURE_PRESENT_ALLOW_TEARING
    bool waitableSupported = true;   // available on SwapChain2; almost universal on Win10+
    bool present1Supported = true;   // SwapChain1+ Present1 path
    bool colorSpaceSupported = true; // SwapChain3/4 color space APIs available
    bool hdrOutput         = false;  // Primary output exposes HDR (ST.2084/scRGB)
    bool hasFactory5       = false;
    bool hasFactory6       = false;
};

// ---------------------------------------------------------------------------------------------------------------------
// Frame statistics / diagnostics
// ---------------------------------------------------------------------------------------------------------------------
struct FrameStatistics {
    UINT presentCount = 0;     // from GetLastPresentCount
    DXGI_FRAME_STATISTICS dxgi{}; // GetFrameStatistics
    bool wasOccluded   = false; // Present returned DXGI_STATUS_OCCLUDED
};

// Minimal adapter description
struct AdapterInfo {
    std::wstring description;
    UINT         vendorId = 0;
    UINT         deviceId = 0;
    UINT         subSysId = 0;
    UINT         revision = 0;
};

// Output descriptor snippet (monitor)
struct OutputInfo {
    std::wstring deviceName; // L"\\.\DISPLAY1"
    RECT         desktopCoordinates{};
    bool         isHDR = false;
    UINT         refreshNum = 0;  // nominal refresh numerator (if known)
    UINT         refreshDen = 0;  // denominator
};

// ---------------------------------------------------------------------------------------------------------------------
// Free utilities (implemented in .cpp)
// ---------------------------------------------------------------------------------------------------------------------

// Acquire factories for a device; some may be null on older systems.
bool GetFactoriesFromDevice(ID3D11Device* device,
                            ComPtr<IDXGIFactory2>& outF2,
                            ComPtr<IDXGIFactory5>& outF5,
                            ComPtr<IDXGIFactory6>& outF6);

// Query ALLOW_TEARING support (factory5+).
bool QueryTearingSupport(IDXGIFactory5* f5);

// Choose the best color space for a requested HDR mode, clamped by capabilities.
DXGI_COLOR_SPACE_TYPE ChooseColorSpace(HDRMode desired, bool hdrOutputAvailable);

// Get primary adapter for a device (best‑effort).
ComPtr<IDXGIAdapter> GetAdapterFromDevice(ID3D11Device* device);

// Return the output (monitor) that contains most of this window (best‑effort).
ComPtr<IDXGIOutput> GetOutputForWindow(ComPtr<IDXGIAdapter> adapter, HWND hwnd);

// Query nominal refresh rate for the output hosting the window (best‑effort).
bool QueryOutputRefreshRate(HWND hwnd, UINT& outNumerator, UINT& outDenominator);

// Adapter & output info snapshots
AdapterInfo GetAdapterInfo(ComPtr<IDXGIAdapter> adapter);
OutputInfo  GetOutputInfo(ComPtr<IDXGIOutput> output);

// ---------------------------------------------------------------------------------------------------------------------
// Lifecycle callbacks for clients to rebuild dependent resources
// ---------------------------------------------------------------------------------------------------------------------
using OnResizeFn   = std::function<void(UINT newW, UINT newH)>;
using OnRecreateFn = std::function<void(const SwapChainOptions& oldOpt, const SwapChainOptions& newOpt)>;

// ---------------------------------------------------------------------------------------------------------------------
// SwapChainDXGI
// ---------------------------------------------------------------------------------------------------------------------
class SwapChainDXGI {
public:
    SwapChainDXGI() = default;
    ~SwapChainDXGI() = default;

    SwapChainDXGI(const SwapChainDXGI&)            = delete;
    SwapChainDXGI& operator=(const SwapChainDXGI&) = delete;
    SwapChainDXGI(SwapChainDXGI&&)                 = default;
    SwapChainDXGI& operator=(SwapChainDXGI&&)      = default;

    // Initialize or recreate from scratch.
    // Returns S_OK on success. On failure, internal state is reset.
    HRESULT Initialize(ID3D11Device* device, HWND hwnd, const SwapChainOptions& opt);

    // Release all swap chain interfaces and associated state (does not destroy the device).
    void    Shutdown();

    // Quick check
    bool    IsInitialized() const { return m_sc2 != nullptr; }

    // Resize backbuffers (preserves creation flags per DXGI rule).
    // If width/height are zero, the current client rect size is used.
    HRESULT Resize(UINT newWidth, UINT newHeight, bool recreateIfFormatChanged = false);

    // Present with correct flags; also waits on frame‑latency handle when enabled.
    // Returns HRESULT from Present/Present1; DXGI_STATUS_OCCLUDED is *not* treated as a failure.
    HRESULT Present(const PresentArgs& args, FrameStatistics* outStats = nullptr);

    // Convenience: bool vsync style present
    inline HRESULT PresentVsync(bool vsync, UINT timeoutMs = 1000) {
        PresentArgs pa{};
        pa.vsync = vsync;
        pa.syncInterval = vsync ? 1u : 0u;
        pa.timeoutMs = timeoutMs;
        return Present(pa, nullptr);
    }

    // Accessors
    inline IDXGISwapChain2* Get() const  { return m_sc2.Get(); }
    inline IDXGISwapChain3* Get3() const { return m_sc3.Get(); }
    inline IDXGISwapChain4* Get4() const { return m_sc4.Get(); }

    inline ID3D11Device*        Device() const { return m_device.Get(); }
    inline ID3D11DeviceContext* Context() const { return m_immediateCtx.Get(); }

    inline HWND                  Hwnd()   const { return m_hwnd; }
    inline HANDLE                FrameLatencyHandle() const { return m_latencyHandle; }

    inline const SwapChainCaps&     Caps()    const { return m_caps; }
    inline const SwapChainOptions&  Options() const { return m_opt; }

    // Backbuffer helpers
    HRESULT GetBackBufferTexture(UINT index, ComPtr<ID3D11Texture2D>& outTex) const;

    HRESULT CreateBackBufferRTV(ID3D11RenderTargetView** outRTV,
                                UINT bufferIndex = 0,
                                const D3D11_RENDER_TARGET_VIEW_DESC* overrideDesc = nullptr) const;

    // Optional SRV creation (useful for postprocess passes reading from backbuffer)
    HRESULT CreateBackBufferSRV(ID3D11ShaderResourceView** outSRV,
                                UINT bufferIndex = 0,
                                const D3D11_SHADER_RESOURCE_VIEW_DESC* overrideDesc = nullptr) const;

    // Retrieve current backbuffer size (after ResizeBuffers)
    void    GetBackBufferSize(UINT& outW, UINT& outH) const { outW = m_sizeW; outH = m_sizeH; }

    // Fullscreen helpers
    HRESULT SetExclusiveFullscreen(bool enable);
    bool    IsExclusiveFullscreen() const { return m_isExclusiveFS; }

    HRESULT EnterBorderlessFullscreen();  // WS_POPUP styled, on monitor hosting the window
    HRESULT ExitBorderlessFullscreen();
    bool    IsBorderlessFullscreen() const { return m_isBorderlessFS; }

    HRESULT SetFullscreenMode(FullscreenMode mode); // Unified entry point
    FullscreenMode GetFullscreenMode() const {
        if (m_isExclusiveFS)  return FullscreenMode::Exclusive;
        if (m_isBorderlessFS) return FullscreenMode::Borderless;
        return FullscreenMode::Windowed;
    }

    // Frame latency
    HRESULT SetMaximumFrameLatency(UINT frames);  // 1..16; no‑op if waitable disabled
    UINT    GetMaximumFrameLatency() const { return m_maxFrameLatency; }
    bool    WaitForNextFrame(UINT timeoutMs);

    // Tearing & flags control
    void    SetTearingPreferred(bool prefer) { m_opt.preferTearing = prefer; }
    bool    IsTearingPreferred() const { return m_opt.preferTearing; }
    bool    IsTearingSupported() const { return m_caps.tearingSupported; }

    // Color / HDR
    HRESULT SetColorSpace(DXGI_COLOR_SPACE_TYPE cs);      // best‑effort (SwapChain3/4)
    DXGI_COLOR_SPACE_TYPE CurrentColorSpace() const { return m_colorSpace; }
    HRESULT SetHDRMode(HDRMode mode);                     // recreates color space if needed
    HDRMode CurrentHDRMode() const { return m_opt.hdrMode; }

    // HDR10 metadata (SwapChain4). E_NOINTERFACE when unsupported.
    HRESULT SetHDR10Metadata(const DXGI_HDR_METADATA_HDR10& md);
    HRESULT ClearHDR10Metadata(); // resets metadata (SwapChain4)

    // DXGI association flags (reapply on demand)
    void    MakeWindowAssociationFlags(UINT dxgiFlags); // e.g., DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES

    // Recreate swap chain with new options (format/buffers/hdr/etc.). Preserves HWND.
    HRESULT Recreate(const SwapChainOptions& newOpt);

    // Callbacks
    void    SetOnResizeCallback(OnResizeFn cb)     { m_onResize = std::move(cb); }
    void    SetOnRecreateCallback(OnRecreateFn cb) { m_onRecreate = std::move(cb); }

    // Diagnostics
    HRESULT GetFrameStatistics(FrameStatistics& out) const;
    AdapterInfo GetAdapterInfo() const;
    OutputInfo  GetCurrentOutputInfo() const;

    // Advanced: switch bufferCount at runtime (forces Recreate)
    HRESULT SetBufferCount(UINT newCount) {
        if (newCount == 0) return E_INVALIDARG;
        SwapChainOptions n = m_opt; n.bufferCount = newCount; return Recreate(n);
    }

    // Advanced: change HDR/scRGB intent (forces color space update or recreate)
    HRESULT SetDesiredHDRMode(HDRMode mode) { return SetHDRMode(mode); }

    // Optional: force Present1 usage when available
    void    SetPresent1Enabled(bool enabled) { m_opt.enablePresent1 = enabled; }

    // Occlusion helper: yields CPU if window is occluded to avoid busy Present loops
    void    HandleOcclusionSleep(DWORD sleepMsWhenOccluded = 16) const;

    // Optional external logger
    void    SetLogger(LogFn fn) { m_log = fn; }

private:
    // Creation / recreation helpers
    HRESULT createSwapChain();       // uses m_device, m_hwnd, m_opt, updates caps/size/handles
    HRESULT destroySwapChain();      // release swap chain COM objects / handles

    // Present flags computation
    UINT    computePresentFlags(const PresentArgs& args) const;

    // Apply color space based on current hdrMode and caps (SwapChain3/4).
    HRESULT applyColorSpace();

    // Internal logging
    void    logf(const char* fmt, ...) const;

    // Window state helpers for borderless transitions
    void    snapshotWindowedRect();
    void    restoreWindowedRect();
    static  RECT monitorRectFromWindow(HWND hwnd);

    // Resize common path (calls ResizeBuffers and rebinds size fields)
    HRESULT resizeBuffers(UINT newW, UINT newH);

private:
    // Core D3D objects
    ComPtr<ID3D11Device>         m_device;
    ComPtr<ID3D11DeviceContext>  m_immediateCtx;

    // DXGI chain
    ComPtr<IDXGISwapChain1>      m_sc1;
    ComPtr<IDXGISwapChain2>      m_sc2;
    ComPtr<IDXGISwapChain3>      m_sc3;
    ComPtr<IDXGISwapChain4>      m_sc4;

    // Factories / adapter / output
    ComPtr<IDXGIFactory2>        m_factory2;
    ComPtr<IDXGIFactory5>        m_factory5;
    ComPtr<IDXGIFactory6>        m_factory6;
    ComPtr<IDXGIAdapter>         m_adapter;
    ComPtr<IDXGIOutput>          m_output;

    // State & options
    SwapChainOptions             m_opt{};
    SwapChainCaps                m_caps{};
    DXGI_COLOR_SPACE_TYPE        m_colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

    // Window / fullscreen state
    HWND                         m_hwnd = nullptr;
    WindowedRect                 m_savedWindowed{};
    bool                         m_isExclusiveFS  = false;
    bool                         m_isBorderlessFS = false;

    // Backbuffer size cache
    UINT                         m_sizeW = 0;
    UINT                         m_sizeH = 0;

    // Waitable frame latency handle
    HANDLE                       m_latencyHandle = nullptr;
    UINT                         m_maxFrameLatency = 1;

    // Callbacks
    OnResizeFn                   m_onResize;
    OnRecreateFn                 m_onRecreate;

    // Logger
    LogFn                        m_log = nullptr;
};

// ---------------------------------------------------------------------------------------------------------------------
// Inline convenience wrappers
// ---------------------------------------------------------------------------------------------------------------------

// Fast vsync toggle present (no stats)
inline HRESULT PresentVsync(SwapChainDXGI& sc, bool vsync, UINT timeoutMs = 1000) {
    return sc.PresentVsync(vsync, timeoutMs);
}

// Create a default RTV for backbuffer 0 (no custom desc).
inline HRESULT CreateDefaultBackbufferRTV(SwapChainDXGI& sc, ID3D11RenderTargetView** outRTV) {
    return sc.CreateBackBufferRTV(outRTV, 0, nullptr);
}

// Simple occlusion-aware present helper:
//   If occluded, Present() returns DXGI_STATUS_OCCLUDED; you can back off briefly.
inline void SleepIfOccluded(const FrameStatistics& stats, DWORD sleepMs = 16) {
    if (stats.wasOccluded) ::Sleep(sleepMs);
}

} // namespace cg::dxgi
