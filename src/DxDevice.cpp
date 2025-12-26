#include "DxDevice.h"

#include "platform/win/LauncherLogSingletonWin.h"

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

#include <array>
#include <iterator> // std::size

namespace {

static const wchar_t* HrName(HRESULT hr) noexcept
{
    switch (hr)
    {
    case DXGI_ERROR_DEVICE_HUNG:           return L"DXGI_ERROR_DEVICE_HUNG";
    case DXGI_ERROR_DEVICE_REMOVED:        return L"DXGI_ERROR_DEVICE_REMOVED";
    case DXGI_ERROR_DEVICE_RESET:          return L"DXGI_ERROR_DEVICE_RESET";
    case DXGI_ERROR_DRIVER_INTERNAL_ERROR: return L"DXGI_ERROR_DRIVER_INTERNAL_ERROR";
    case DXGI_ERROR_INVALID_CALL:          return L"DXGI_ERROR_INVALID_CALL";
    case DXGI_STATUS_OCCLUDED:             return L"DXGI_STATUS_OCCLUDED";
    default:                               return nullptr;
    }
}

static std::wstring TrimW(std::wstring s)
{
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ' || s.back() == L'\t'))
        s.pop_back();
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t'))
        s.erase(s.begin());
    return s;
}

static std::wstring HrMessageW(HRESULT hr)
{
    wchar_t* buf = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD len = ::FormatMessageW(flags, nullptr, static_cast<DWORD>(hr),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&buf), 0, nullptr);

    std::wstring msg;
    if (len && buf)
    {
        msg.assign(buf, buf + len);
        ::LocalFree(buf);
    }
    return TrimW(msg);
}

static std::wstring HrFullW(HRESULT hr)
{
    std::wstringstream ss;
    ss << L"0x" << std::hex << std::uppercase << std::setw(8) << std::setfill(L'0')
       << static_cast<std::uint32_t>(hr);
    if (const wchar_t* name = HrName(hr))
        ss << L" (" << name << L")";
    const std::wstring msg = HrMessageW(hr);
    if (!msg.empty())
        ss << L": " << msg;
    return ss.str();
}

static std::wstring AdapterSummaryW(ID3D11Device* device)
{
    if (!device)
        return {};

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))))
        return {};

    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(&adapter)) || !adapter)
        return {};

    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter1;
    (void)adapter.As(&adapter1);

    if (adapter1)
    {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter1->GetDesc1(&desc)))
        {
            std::wstringstream ss;
            ss << desc.Description
               << L" (VendorId=0x" << std::hex << std::uppercase << desc.VendorId
               << L", DeviceId=0x" << desc.DeviceId
               << L", DedicatedVRAM=" << std::dec << (desc.DedicatedVideoMemory / (1024ull * 1024ull)) << L"MB)";
            return ss.str();
        }
    }

    DXGI_ADAPTER_DESC desc{};
    if (SUCCEEDED(adapter->GetDesc(&desc)))
    {
        std::wstringstream ss;
        ss << desc.Description
           << L" (VendorId=0x" << std::hex << std::uppercase << desc.VendorId
           << L", DeviceId=0x" << desc.DeviceId
           << L", DedicatedVRAM=" << std::dec << (desc.DedicatedVideoMemory / (1024ull * 1024ull)) << L"MB)";
        return ss.str();
    }

    return {};
}

static void LogDeviceLost(const wchar_t* stage, HRESULT triggeringHr, HRESULT removedReason,
                          ID3D11Device* device,
                          UINT width, UINT height,
                          const DxDeviceOptions& opt,
                          bool allowTearing, UINT swapchainFlags, bool waitable)
{
    std::wstringstream ss;
    ss << L"[DxDevice] Device lost (" << (stage ? stage : L"?") << L") "
       << L"triggeringHr=" << HrFullW(triggeringHr) << L" "
       << L"removedReason=" << HrFullW(removedReason);

    const std::wstring adapter = AdapterSummaryW(device);
    if (!adapter.empty())
        ss << L" adapter=\"" << adapter << L"\"";

    ss << L" size=" << width << L"x" << height
       << L" maxFrameLatency=" << opt.maxFrameLatency
       << L" scaling=" << static_cast<int>(opt.scaling)
       << L" allowTearing=" << (allowTearing ? L"true" : L"false")
       << L" swapchainFlags=0x" << std::hex << std::uppercase << swapchainFlags
       << L" waitable=" << (waitable ? L"true" : L"false");

    WriteLog(LauncherLog(), ss.str());
}


static UINT ClampFrameLatency(UINT v) noexcept
{
    if (v < 1u)  return 1u;
    if (v > 16u) return 16u;
    return v;
}

static double QpcToMs(LONGLONG ticks, const LARGE_INTEGER& freq) noexcept
{
    return (freq.QuadPart > 0) ? (double(ticks) * 1000.0 / double(freq.QuadPart)) : 0.0;
}

// Small helper: check whether the OS/driver combo supports variable-refresh / tearing.
// Requires DXGI 1.5+ and flip-model swapchains.
static bool CheckTearing(IDXGIFactory6* factory)
{
    BOOL allow = FALSE;

    Microsoft::WRL::ComPtr<IDXGIFactory5> f5;
    if (factory && SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&f5))))
    {
        if (FAILED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow))))
            allow = FALSE;
    }

    return allow == TRUE;
}

static HRESULT CreateD3D11Device(UINT flags,
                                D3D_DRIVER_TYPE driverType,
                                Microsoft::WRL::ComPtr<ID3D11Device>& outDevice,
                                Microsoft::WRL::ComPtr<ID3D11DeviceContext>& outCtx)
{
    // Ask for the best available feature level down to 10.0.
    static constexpr D3D_FEATURE_LEVEL kLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    D3D_FEATURE_LEVEL created = D3D_FEATURE_LEVEL_11_0;

    return D3D11CreateDevice(
        nullptr,
        driverType,
        nullptr,
        flags,
        kLevels,
        static_cast<UINT>(std::size(kLevels)),
        D3D11_SDK_VERSION,
        outDevice.GetAddressOf(),
        &created,
        outCtx.GetAddressOf());
}

} // namespace

bool DxDevice::Init(HWND hwnd, UINT width, UINT height, const DxDeviceOptions& opt)
{
    // Allow re-init (e.g. after device removed/reset).
    Shutdown();

    m_hwnd = hwnd;
    m_width = width;
    m_height = height;
    m_opt = opt;
    m_opt.maxFrameLatency = ClampFrameLatency(m_opt.maxFrameLatency);

    auto fail = [&]() -> bool {
        Shutdown();
        return false;
    };

    // -------------------------------------------------------------------------
    // Create D3D11 device (with robust fallbacks)
    // -------------------------------------------------------------------------
    UINT flags = 0;

#if defined(_DEBUG)
    // The debug layer is optional and not always installed. We try it first and
    // fall back to a non-debug device if creation fails.
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;

    HRESULT hr = CreateD3D11Device(flags, D3D_DRIVER_TYPE_HARDWARE, dev, ctx);

#if defined(_DEBUG)
    if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG))
    {
        // Retry without the debug layer (common on machines without Graphics Tools).
        const UINT noDbg = flags & ~D3D11_CREATE_DEVICE_DEBUG;
        hr = CreateD3D11Device(noDbg, D3D_DRIVER_TYPE_HARDWARE, dev, ctx);
    }
#endif

    if (FAILED(hr))
    {
        // Hardware device creation can fail on some VMs/remote sessions. WARP is
        // slower but keeps the app runnable.
        hr = CreateD3D11Device(0, D3D_DRIVER_TYPE_WARP, dev, ctx);
        if (FAILED(hr))
            return fail();
    }

    m_device = dev;
    m_ctx = ctx;

    // -------------------------------------------------------------------------
    // Get the DXGI factory used by this device (for swapchain + tearing support)
    // -------------------------------------------------------------------------
    ComPtr<IDXGIDevice> dxgiDev;
    if (FAILED(m_device.As(&dxgiDev)))
        return fail();

    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDev->GetAdapter(adapter.GetAddressOf())))
        return fail();

    ComPtr<IDXGIFactory6> factory;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()))))
        return fail();

    m_factory = factory;

    // Disable DXGI's default Alt+Enter fullscreen handling; AppWindow manages it.
    if (m_factory)
        (void)m_factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);

    m_allowTearing = CheckTearing(m_factory.Get());

    if (!CreateSwapchain(width, height))
        return fail();

    return true;
}

void DxDevice::CloseFrameLatencyHandle() noexcept
{
    if (m_frameLatencyWaitable)
    {
        ::CloseHandle(m_frameLatencyWaitable);
        m_frameLatencyWaitable = nullptr;
    }
}

void DxDevice::ApplyFrameLatencyIfPossible() noexcept
{
    const UINT latency = ClampFrameLatency(m_opt.maxFrameLatency);

    // Per-swapchain path (best). This also provides the waitable object handle.
    if (m_swap)
    {
        ComPtr<IDXGISwapChain2> sc2;
        if (SUCCEEDED(m_swap.As(&sc2)))
        {
            (void)sc2->SetMaximumFrameLatency(latency);

            if (m_opt.enableWaitableObject && m_createdWithWaitableFlag)
            {
                // Requires DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT.
                HANDLE h = sc2->GetFrameLatencyWaitableObject();
                if (h)
                    m_frameLatencyWaitable = h;
            }
        }
    }

    // Best-effort device-wide fallback (helps on older swapchains/paths).
    if (m_device)
    {
        ComPtr<IDXGIDevice1> dev1;
        if (SUCCEEDED(m_device.As(&dev1)))
            (void)dev1->SetMaximumFrameLatency(latency);
    }
}

void DxDevice::SetMaxFrameLatency(UINT v) noexcept
{
    const UINT clamped = ClampFrameLatency(v);
    if (m_opt.maxFrameLatency == clamped)
        return;

    m_opt.maxFrameLatency = clamped;

    // If a swapchain already exists, apply immediately.
    CloseFrameLatencyHandle();
    ApplyFrameLatencyIfPossible();
}

bool DxDevice::CreateSwapchain(UINT width, UINT height)
{
    if (!m_factory || !m_device)
        return false;

    DestroyRTV();
    CloseFrameLatencyHandle();

    m_createdWithWaitableFlag = false;
    m_swapchainFlags = 0;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Format = m_backbufferFormat;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

    // 2 buffers keeps latency low; frame latency limiting is handled separately.
    desc.BufferCount = 2;
    desc.SampleDesc = { 1, 0 };
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;  // flip-model

    // Scaling mode matters for modern borderless fullscreen:
    //  - NONE is preferred when buffers match the client size (better independent flip odds)
    //  - STRETCH can hide mismatch but may prevent some presentation optimizations
    desc.Scaling = m_opt.scaling;

    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    // Allow tearing only when supported and only when presenting with syncInterval==0.
    const UINT baseFlags = m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;

    const bool wantWaitable = m_opt.enableWaitableObject;
    const UINT waitableFlag = wantWaitable ? DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT : 0u;

    // Try to create a waitable swapchain first (biggest latency win on Windows).
    struct Attempt { UINT flags; bool waitable; };
    const Attempt attempts[] = {
        { baseFlags | waitableFlag, true },
        { baseFlags, false },
    };

    ComPtr<IDXGISwapChain1> swap;
    HRESULT hr = E_FAIL;
    bool createdWaitable = false;

    for (const auto& a : attempts)
    {
        if (!wantWaitable && a.waitable)
            continue;

        desc.Flags = a.flags;
        swap.Reset();

        hr = m_factory->CreateSwapChainForHwnd(
            m_device.Get(),
            m_hwnd,
            &desc,
            nullptr,
            nullptr,
            swap.GetAddressOf());

        if (SUCCEEDED(hr))
        {
            createdWaitable = a.waitable && wantWaitable;
            m_swapchainFlags = a.flags;
            break;
        }
    }

    if (FAILED(hr) || !swap)
        return false;

    m_swap = swap;
    m_createdWithWaitableFlag = createdWaitable;

    // Cache swapchain properties for diagnostics.
    m_swapchainBufferCount = desc.BufferCount;
    if (m_swap)
    {
        DXGI_SWAP_CHAIN_DESC1 got{};
        if (SUCCEEDED(m_swap->GetDesc1(&got)))
            m_swapchainBufferCount = got.BufferCount;
    }

    // Apply latency caps + retrieve waitable object handle (if available).
    ApplyFrameLatencyIfPossible();

    CreateRTV();
    return m_rtv != nullptr;
}

void DxDevice::CreateRTV()
{
    DestroyRTV();

    if (!m_swap || !m_device)
        return;

    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(m_swap->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()))))
        return;

    (void)m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_rtv.GetAddressOf());
}

void DxDevice::DestroyRTV()
{
    if (m_rtv)
        m_rtv.Reset();
}

void DxDevice::Resize(UINT width, UINT height)
{
    m_width = width;
    m_height = height;

    if (!m_swap || width == 0 || height == 0)
        return;

    DestroyRTV();

    const HRESULT hr = m_swap->ResizeBuffers(
        0,
        width,
        height,
        m_backbufferFormat,
        m_swapchainFlags);

    if (FAILED(hr))
    {
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
            (void)HandleDeviceLost(hr, L"ResizeBuffers");
        return;
    }

    // Some drivers keep the same handle across ResizeBuffers, but re-querying is cheap
    // and keeps us correct if a resize causes the handle to change.
    CloseFrameLatencyHandle();
    ApplyFrameLatencyIfPossible();

    CreateRTV();
}

void DxDevice::BeginFrame()
{
    if (!m_ctx || !m_rtv)
        return;

    // Ensure viewport/scissor match the current swapchain size.
    // (Clear doesn't require this, but any real drawing will.)
    if (m_width > 0 && m_height > 0)
    {
        D3D11_VIEWPORT vp{};
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width    = static_cast<float>(m_width);
        vp.Height   = static_cast<float>(m_height);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        m_ctx->RSSetViewports(1, &vp);

        const D3D11_RECT sc{ 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
        m_ctx->RSSetScissorRects(1, &sc);
    }

    const float clear[4] = { 0.08f, 0.10f, 0.12f, 1.0f };
    m_ctx->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);
    m_ctx->ClearRenderTargetView(m_rtv.Get(), clear);
}

DxRenderStats DxDevice::EndFrame(bool vsync)
{
    DxRenderStats stats{};
    if (!m_swap)
        return stats;

    const UINT syncInterval = vsync ? 1u : 0u;

    UINT presentFlags = 0;
    if (!vsync && m_allowTearing)
        presentFlags |= DXGI_PRESENT_ALLOW_TEARING;

    // Cache for diagnostics (title/overlay).
    m_lastPresentSyncInterval = syncInterval;
    m_lastPresentFlags = presentFlags;

    static LARGE_INTEGER freq{};
    static bool freqInit = false;
    if (!freqInit)
    {
        ::QueryPerformanceFrequency(&freq);
        freqInit = true;
    }

    LARGE_INTEGER t0{};
    LARGE_INTEGER t1{};
    ::QueryPerformanceCounter(&t0);

    const HRESULT hr = m_swap->Present(syncInterval, presentFlags);

    ::QueryPerformanceCounter(&t1);

    stats.presentHr = hr;
    stats.presentMs = QpcToMs(t1.QuadPart - t0.QuadPart, freq);
    stats.occluded = (hr == DXGI_STATUS_OCCLUDED);

    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
    {
        const bool recreated = HandleDeviceLost(hr, L"Present");
        m_deviceRecreated = m_deviceRecreated || recreated;
    }

    return stats;
}

DxRenderStats DxDevice::Render(bool vsync)
{
    BeginFrame();
    return EndFrame(vsync);
}

void DxDevice::Shutdown()
{
    DestroyRTV();
    CloseFrameLatencyHandle();

    if (m_swap)    m_swap.Reset();
    if (m_ctx)     m_ctx.Reset();
    if (m_device)  m_device.Reset();
    if (m_factory) m_factory.Reset();

    m_hwnd = nullptr;
    m_allowTearing = false;
    m_width = 0;
    m_height = 0;
    m_createdWithWaitableFlag = false;
    m_swapchainFlags = 0;
    m_swapchainBufferCount = 0;
    m_lastPresentFlags = 0;
    m_lastPresentSyncInterval = 0;
}

bool DxDevice::HandleDeviceLost(HRESULT triggeringHr, const wchar_t* stage)
{
    // Device removed/reset can occur due to TDR, a driver update, or the adapter
    // changing (e.g. docking/undocking, remote sessions, etc.).
    //
    // We do a best-effort full recreation to keep the prototype running.
    const HWND hwnd = m_hwnd;
    const UINT w = m_width;
    const UINT h = m_height;
    const DxDeviceOptions opt = m_opt;

    HRESULT reason = DXGI_ERROR_DEVICE_REMOVED;
    if (m_device)
        reason = m_device->GetDeviceRemovedReason();

    LogDeviceLost(stage, triggeringHr, reason, m_device.Get(), w, h, opt, m_allowTearing, m_swapchainFlags, m_createdWithWaitableFlag);

    Shutdown();

    const bool ok = Init(hwnd, w, h, opt);
    if (ok)
        m_deviceRecreated = true;
    WriteLog(LauncherLog(), ok ? L"[DxDevice] Device recreation succeeded after device loss."
                          : L"[DxDevice] Device recreation FAILED after device loss.");
    return ok;
}
