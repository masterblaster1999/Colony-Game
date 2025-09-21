// dpi_aware_test.cpp
// Small console utility to inspect DPI awareness & per-monitor DPI on Windows 10/11.

#define WIN32_LEAN_AND_MEAN
#ifndef WINVER
#  define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <shellscalingapi.h>   // MONITOR_DPI_TYPE, GetDpiForMonitor, GetScaleFactorForMonitor
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "Shcore.lib")

// Safe loaders for newer APIs (work on older OS versions too).
static BOOL TrySetPerMonitorV2()
{
    auto hUser = ::GetModuleHandleW(L"user32.dll");
    if (!hUser) return FALSE;

    using PFN_SetProcessDpiAwarenessContext = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
    auto pSetProcessDpiAwarenessContext =
        reinterpret_cast<PFN_SetProcessDpiAwarenessContext>(
            ::GetProcAddress(hUser, "SetProcessDpiAwarenessContext"));

    if (pSetProcessDpiAwarenessContext)
    {
        // Best practice: Per Monitor V2 when available.
        if (pSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
            return TRUE;
    }

    // Fallback to PROCESS_PER_MONITOR_DPI_AWARE (Win 8.1+)
    auto hShcore = ::LoadLibraryW(L"Shcore.dll");
    if (hShcore)
    {
        using PFN_SetProcessDpiAwareness = HRESULT (WINAPI*)(PROCESS_DPI_AWARENESS);
        auto pSetProcessDpiAwareness = reinterpret_cast<PFN_SetProcessDpiAwareness>(
            ::GetProcAddress(hShcore, "SetProcessDpiAwareness"));
        if (pSetProcessDpiAwareness)
        {
            if (SUCCEEDED(pSetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE)))
                return TRUE;
        }
    }

    // Last resort: system DPI aware (Vista+)
    using PFN_SetProcessDPIAware = BOOL (WINAPI*)(void);
    auto pSetProcessDPIAware = reinterpret_cast<PFN_SetProcessDPIAware>(
        ::GetProcAddress(hUser, "SetProcessDPIAware"));
    return pSetProcessDPIAware ? pSetProcessDPIAware() : FALSE;
}

static const char* AwarenessToString(DPI_AWARENESS a)
{
    switch (a)
    {
    case DPI_AWARENESS_INVALID:          return "Invalid";
    case DPI_AWARENESS_UNAWARE:          return "Unaware";
    case DPI_AWARENESS_SYSTEM_AWARE:     return "System-aware";
    case DPI_AWARENESS_PER_MONITOR_AWARE:return "Per-Monitor (V1/V2)";
    default:                             return "?";
    }
}

struct MonitorInfo {
    HMONITOR hmon{};
    MONITORINFOEXW mi{};
    UINT dpiX{ 96 }, dpiY{ 96 };
    int  scalePct{ 100 }; // derived or via GetScaleFactorForMonitor
};

static BOOL CALLBACK EnumProc(HMONITOR hMon, HDC, LPRECT, LPARAM lParam)
{
    auto list = reinterpret_cast<std::vector<MonitorInfo>*>(lParam);
    MonitorInfo m{};
    m.hmon = hMon;
    m.mi.cbSize = sizeof(m.mi);
    if (!::GetMonitorInfoW(hMon, &m.mi)) return TRUE;

    // Try GetDpiForMonitor (Win 8.1+)
    auto hShcore = ::GetModuleHandleW(L"Shcore.dll");
    if (hShcore)
    {
        using PFN_GetDpiForMonitor = HRESULT (WINAPI*)(HMONITOR, MONITOR_DPI_TYPE, UINT*, UINT*);
        auto pGetDpiForMonitor = reinterpret_cast<PFN_GetDpiForMonitor>(
            ::GetProcAddress(hShcore, "GetDpiForMonitor"));
        if (pGetDpiForMonitor)
        {
            if (SUCCEEDED(pGetDpiForMonitor(hMon, MDT_EFFECTIVE_DPI, &m.dpiX, &m.dpiY)))
            {
                m.scalePct = static_cast<int>((m.dpiX * 100 + 48) / 96); // rounded
            }
        }

        using PFN_GetScaleFactorForMonitor = HRESULT (WINAPI*)(HMONITOR, DEVICE_SCALE_FACTOR*);
        auto pGetScaleFactorForMonitor = reinterpret_cast<PFN_GetScaleFactorForMonitor>(
            ::GetProcAddress(hShcore, "GetScaleFactorForMonitor"));
        if (pGetScaleFactorForMonitor)
        {
            DEVICE_SCALE_FACTOR sf{};
            if (SUCCEEDED(pGetScaleFactorForMonitor(hMon, &sf)))
            {
                // Map enum values like SCALE_100_PERCENT (100), 125, 150, etc.
                m.scalePct = static_cast<int>(sf);
            }
        }
    }

    // If SHCore APIs unavailable, approximate via a DC for that display device.
    if (m.scalePct == 100 && m.dpiX == 96)
    {
        HDC hdc = ::CreateDCW(L"DISPLAY", m.mi.szDevice, nullptr, nullptr);
        if (hdc)
        {
            m.dpiX = static_cast<UINT>(::GetDeviceCaps(hdc, LOGPIXELSX));
            m.dpiY = static_cast<UINT>(::GetDeviceCaps(hdc, LOGPIXELSY));
            m.scalePct = static_cast<int>((m.dpiX * 100 + 48) / 96);
            ::DeleteDC(hdc);
        }
    }

    list->push_back(m);
    return TRUE;
}

int main()
{
    // 1) Make the process DPI-aware (Per-Monitor V2 if possible) before any windows.
    TrySetPerMonitorV2();

    // 2) Report process/thread DPI awareness and system DPI.
    auto hUser = ::GetModuleHandleW(L"user32.dll");
    using PFN_GetThreadDpiAwarenessContext = DPI_AWARENESS_CONTEXT (WINAPI*)();
    using PFN_GetAwarenessFromDpiAwarenessContext = DPI_AWARENESS (WINAPI*)(DPI_AWARENESS_CONTEXT);
    using PFN_GetDpiForSystem = UINT (WINAPI*)();

    auto pGetThreadCtx = reinterpret_cast<PFN_GetThreadDpiAwarenessContext>(
        ::GetProcAddress(hUser, "GetThreadDpiAwarenessContext"));
    auto pGetAwareness = reinterpret_cast<PFN_GetAwarenessFromDpiAwarenessContext>(
        ::GetProcAddress(hUser, "GetAwarenessFromDpiAwarenessContext"));
    auto pGetDpiForSystem = reinterpret_cast<PFN_GetDpiForSystem>(
        ::GetProcAddress(hUser, "GetDpiForSystem"));

    DPI_AWARENESS awareness = DPI_AWARENESS_INVALID;
    if (pGetThreadCtx && pGetAwareness)
        awareness = pGetAwareness(pGetThreadCtx());

    UINT sysDpi = 96;
    if (pGetDpiForSystem) sysDpi = pGetDpiForSystem();
    else
    {
        HDC hdc = ::GetDC(nullptr);
        if (hdc) { sysDpi = static_cast<UINT>(::GetDeviceCaps(hdc, LOGPIXELSX)); ::ReleaseDC(nullptr, hdc); }
    }

    std::cout << "DPI awareness: " << AwarenessToString(awareness) << "\n";
    std::cout << "System DPI: " << sysDpi << " (" << (sysDpi * 100 / 96) << "%)\n\n";

    // 3) Enumerate monitors.
    std::vector<MonitorInfo> monitors;
    ::EnumDisplayMonitors(nullptr, nullptr, EnumProc, reinterpret_cast<LPARAM>(&monitors));

    int idx = 0;
    for (const auto& m : monitors)
    {
        const RECT& r = m.mi.rcMonitor;
        const RECT& w = m.mi.rcWork;
        std::wcout << L"Monitor " << ++idx << L" - " << m.mi.szDevice << L"\n";
        std::cout  << "  bounds: (" << r.left << "," << r.top << ") - (" << r.right << "," << r.bottom
                   << ")  size: " << (r.right - r.left) << "x" << (r.bottom - r.top) << "\n";
        std::cout  << "  work  : (" << w.left << "," << w.top << ") - (" << w.right << "," << w.bottom
                   << ")  size: " << (w.right - w.left) << "x" << (w.bottom - w.top) << "\n";
        std::cout  << "  effective DPI: " << m.dpiX << "x" << m.dpiY
                   << "  (approx scale " << m.scalePct << "%)\n\n";
    }

    return 0;
}
