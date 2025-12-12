// src/ui/ImGuiLayer.cpp
//
// Colony Game - Dear ImGui Layer (Win32 + D3D11)
//
// Surgical improvements included:
//  - Fix: WideToUtf8 buffer sizing (was 1-byte overflow risk)
//  - DPI: deferred font rebuild on WM_DPICHANGED + style scaling
//  - Docking: automatic fullscreen dockspace host + menu bar (no background passthrough)
//  - Input routing: optional compile-time blocking based on io.WantCapture* (off by default)
//  - Font atlas: optional font-from-disk (res/fonts/*) with safe fallback to default font
//  - Debug panels: menu toggles for Demo/Metrics/About/Style + Font Atlas inspector
//
// Notes:
//  - Designed to not break existing integration by keeping the default WndProc return behavior.
//  - If your WndProc uses handleWndProc() solely to decide whether to return early to Windows,
//    keep CG_IMGUI_LAYER_USE_WANT_CAPTURE_FLAGS at 0 (default).
//  - If your event system wants to block game input while UI is active, you may set it to 1.

#include "ImGuiLayer.h"

// Pull Windows.h with proper guards (WIN32_LEAN_AND_MEAN/NOMINMAX) once.
#include "platform/win/WinCommon.h"

// ---------- Dear ImGui: core + robust Win32/DX11 backend includes ----------
#include <imgui.h>

#ifndef __has_include
  #define __has_include(x) 0
#endif

// Try common layouts in order:
//  1) <imgui/backends/...>   (include path points at parent of 'imgui')
//  2) <backends/...>         (include path points at the 'imgui' folder itself)
//  3) <imgui_impl_*.h>       (flat include path containing the backends)
#if __has_include(<imgui/backends/imgui_impl_win32.h>)
  #include <imgui/backends/imgui_impl_win32.h>
#elif __has_include(<backends/imgui_impl_win32.h>)
  #include <backends/imgui_impl_win32.h>
#elif __has_include(<imgui_impl_win32.h>)
  #include <imgui_impl_win32.h>
#else
  #error "imgui_impl_win32.h not found. Ensure Dear ImGui 'backends' are present and on the include path."
#endif

#if __has_include(<imgui/backends/imgui_impl_dx11.h>)
  #include <imgui/backends/imgui_impl_dx11.h>
#elif __has_include(<backends/imgui_impl_dx11.h>)
  #include <backends/imgui_impl_dx11.h>
#elif __has_include(<imgui_impl_dx11.h>)
  #include <imgui_impl_dx11.h>
#else
  #error "imgui_impl_dx11.h not found. Ensure Dear ImGui 'backends' are present and on the include path."
#endif
// ---------------------------------------------------------------------------

#include <cassert>
#include <chrono>
#include <cmath>
#include <d3d11.h>        // for state backup constants (safe even if already included)
#include <filesystem>
#include <knownfolders.h>
#include <shlobj.h>
#include <string>
#include <system_error>
#include <vector>

#pragma comment(lib, "Shell32.lib") // SHGetKnownFolderPath
#pragma comment(lib, "Ole32.lib")   // CoTaskMemFree

// --------------------------------------------------------------------------------------
// Compile-time knobs (safe defaults).
// --------------------------------------------------------------------------------------

// If 1, handleWndProc() will ALSO return true for mouse/keyboard/text messages when
// ImGui IO says it wants to capture them. This is useful if your input system relies on
// handleWndProc()'s return to block game input.
//
// Keep 0 if your Win32 WndProc returns early to DefWindowProc() when this returns true.
#ifndef CG_IMGUI_LAYER_USE_WANT_CAPTURE_FLAGS
  #define CG_IMGUI_LAYER_USE_WANT_CAPTURE_FLAGS 0
#endif

// If 1, calls ImGui_ImplWin32_EnableAlphaCompositing() after init.
// Leave 0 if you aren't using transparent OS windows / don't need this.
#ifndef CG_IMGUI_LAYER_ENABLE_ALPHA_COMPOSITING
  #define CG_IMGUI_LAYER_ENABLE_ALPHA_COMPOSITING 0
#endif

// Base font size at 96 DPI.
#ifndef CG_IMGUI_LAYER_BASE_FONT_PX
  #define CG_IMGUI_LAYER_BASE_FONT_PX 13.0f
#endif

using namespace cg::ui;

// ---------------------------------------------------------------------------
// Forward declare the Win32 backend WndProc handler with the exact signature.
// (Backends often intentionally comment it out in header to avoid <windows.h> types.)
// ---------------------------------------------------------------------------
extern IMGUI_IMPL_API LRESULT
ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Optional Win32 helpers provided by the backend (present in modern docking/master builds).
extern IMGUI_IMPL_API void  ImGui_ImplWin32_EnableDpiAwareness();
extern IMGUI_IMPL_API float ImGui_ImplWin32_GetDpiScaleForHwnd(void* hwnd);
extern IMGUI_IMPL_API void  ImGui_ImplWin32_EnableAlphaCompositing(void* hwnd);

namespace {

struct DebugUIState
{
    bool show_demo        = false;
    bool show_metrics     = false;
    bool show_about       = false;
    bool show_style       = false;
    bool show_font_atlas  = false;
    bool show_imgui_info  = false;

    // Dockspace host
    bool enable_dockspace = true;
};

static DebugUIState& DebugState()
{
    static DebugUIState s;
    return s;
}

static void ConfigureStyleForViewports()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
}

// Safe wide->UTF8 conversion (NO NUL terminator included).
static std::string WideToUtf8(std::wstring_view ws)
{
    if (ws.empty())
        return {};

    const int needed = ::WideCharToMultiByte(
        CP_UTF8, 0,
        ws.data(), static_cast<int>(ws.size()),
        nullptr, 0, nullptr, nullptr);

    if (needed <= 0)
        return {};

    std::string out(static_cast<size_t>(needed), '\0');
    const int written = ::WideCharToMultiByte(
        CP_UTF8, 0,
        ws.data(), static_cast<int>(ws.size()),
        out.data(), needed,
        nullptr, nullptr);

    if (written != needed)
        out.clear();

    return out;
}

static std::filesystem::path GetExecutableDir()
{
    std::vector<wchar_t> buf(1024);
    DWORD n = 0;
    for (;;)
    {
        n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0)
            return {};
        if (n < buf.size())
            break;
        buf.resize(buf.size() * 2);
    }
    std::filesystem::path exe(buf.data(), buf.data() + n);
    return exe.parent_path();
}

// Write imgui.ini and imgui_log.txt to %LOCALAPPDATA%\ColonyGame\
static void SetImGuiIniAndLogToLocalAppData()
{
    ImGuiIO& io = ImGui::GetIO();

    PWSTR wpath = nullptr;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &wpath)))
    {
        std::filesystem::path base(wpath);
        ::CoTaskMemFree(wpath);

        std::filesystem::path dir     = base / L"ColonyGame";
        std::filesystem::path iniFile = dir / L"imgui.ini";
        std::filesystem::path logFile = dir / L"imgui_log.txt";

        std::error_code ec;
        std::filesystem::create_directories(dir, ec); // best-effort

        static std::string s_iniUtf8;
        static std::string s_logUtf8;

        s_iniUtf8 = WideToUtf8(iniFile.wstring());
        s_logUtf8 = WideToUtf8(logFile.wstring());

        if (!s_iniUtf8.empty())
            io.IniFilename = s_iniUtf8.c_str();
        if (!s_logUtf8.empty())
            io.LogFilename = s_logUtf8.c_str();
    }
}

// --- DPI/font rebuild machinery (deferred) ---
static bool  g_dpiRebuildRequested = false;
static float g_dpiRebuildScale     = 0.0f;

static void RequestFontRebuild(float scale /* 0 => auto */)
{
    g_dpiRebuildRequested = true;
    g_dpiRebuildScale     = scale;
}

static float GetDpiScaleForHwnd(HWND hwnd)
{
    float scale = 1.0f;
    if (hwnd)
    {
        // Backend helper: uses per-monitor DPI when available.
        scale = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
    }
    if (scale <= 0.0f)
        scale = 1.0f;
    return scale;
}

static std::filesystem::path FindDefaultFontOnDisk()
{
    const std::filesystem::path exeDir = GetExecutableDir();
    if (exeDir.empty())
        return {};

    // Candidate list (safe: we check existence).
    const std::filesystem::path candidates[] = {
        exeDir / L"res" / L"fonts" / L"Inter-Regular.ttf",
        exeDir / L"res" / L"fonts" / L"Inter.ttf",
        exeDir / L"res" / L"fonts" / L"Roboto-Regular.ttf",
        exeDir / L"res" / L"fonts" / L"Roboto-Medium.ttf",
        exeDir / L"res" / L"fonts" / L"SegoeUI.ttf",
    };

    for (const auto& p : candidates)
    {
        std::error_code ec;
        if (std::filesystem::exists(p, ec) && !ec)
            return p;
    }
    return {};
}

// Build fonts sized to the chosen DPI scale for crisp text, and scale style sizes.
// Recreates the DX11 font atlas on the GPU.
static void RebuildFontsForScale(float scale)
{
    if (scale <= 0.0f)
        scale = 1.0f;

    static float s_lastScale = 0.0f;

    ImGuiIO& io = ImGui::GetIO();
    if (io.Fonts && io.Fonts->IsBuilt() && std::fabs(scale - s_lastScale) < 0.01f)
        return;

    // Scale style sizes proportionally (do this before rebuilding fonts).
    {
        ImGuiStyle& style = ImGui::GetStyle();
        const float ratio = (s_lastScale <= 0.0f) ? scale : (scale / s_lastScale);
        if (ratio > 0.0f && std::fabs(ratio - 1.0f) > 0.01f)
            style.ScaleAllSizes(ratio);
    }

    s_lastScale = scale;

    io.Fonts->Clear();

    ImFontConfig cfg;
    cfg.SizePixels = CG_IMGUI_LAYER_BASE_FONT_PX * scale;

    // Prefer a real TTF in res/fonts if available (better hinting than default bitmap font).
    ImFont* font = nullptr;
    const std::filesystem::path fontPath = FindDefaultFontOnDisk();
    if (!fontPath.empty())
    {
        const std::string fontUtf8 = WideToUtf8(fontPath.wstring());
        if (!fontUtf8.empty())
        {
            font = io.Fonts->AddFontFromFileTTF(fontUtf8.c_str(), cfg.SizePixels, &cfg, io.Fonts->GetGlyphRangesDefault());
        }
    }

    if (!font)
    {
        io.Fonts->AddFontDefault(&cfg);
    }

    // Recreate the font atlas GPU resources immediately.
    ImGui_ImplDX11_InvalidateDeviceObjects();
    (void)ImGui_ImplDX11_CreateDeviceObjects();
}

static void ApplyPendingDpiRebuild(HWND hwnd)
{
    if (!g_dpiRebuildRequested)
        return;

    const float scale = (g_dpiRebuildScale > 0.0f) ? g_dpiRebuildScale : GetDpiScaleForHwnd(hwnd);
    g_dpiRebuildRequested = false;
    g_dpiRebuildScale     = 0.0f;

    RebuildFontsForScale(scale);
}

// --- timing ---
static float ComputeDeltaTimeSeconds()
{
    using clock = std::chrono::steady_clock;
    static clock::time_point s_last = clock::now();

    const clock::time_point now = clock::now();
    const std::chrono::duration<float> dt = now - s_last;
    s_last = now;

    float seconds = dt.count();
    if (seconds <= 0.0f || seconds > 0.25f) // clamp: avoid huge spikes (breakpoints / window drag)
        seconds = 1.0f / 60.0f;
    return seconds;
}

// --- optional input routing helpers ---
static bool IsMouseMessage(UINT msg)
{
    switch (msg)
    {
        case WM_MOUSEMOVE:
        case WM_MOUSELEAVE:
        case WM_NCMOUSEMOVE:
        case WM_NCMOUSELEAVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_XBUTTONDBLCLK:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        case WM_SETCURSOR:
            return true;
        default:
            return false;
    }
}

static bool IsKeyboardMessage(UINT msg)
{
    switch (msg)
    {
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
            return true;
        default:
            return false;
    }
}

static bool IsTextInputMessage(UINT msg)
{
    switch (msg)
    {
        case WM_CHAR:
        case WM_UNICHAR:
        case WM_IME_CHAR:
        case WM_IME_COMPOSITION:
            return true;
        default:
            return false;
    }
}

// --- dockspace + debug UI ---
static void DrawFontAtlasWindow(HWND hwnd)
{
    DebugUIState& s = DebugState();
    if (!s.show_font_atlas)
        return;

    if (ImGui::Begin("Font Atlas", &s.show_font_atlas))
    {
        ImGuiIO& io = ImGui::GetIO();
        ImFontAtlas* atlas = io.Fonts;

        ImGui::Text("DPI scale (hwnd): %.2f", GetDpiScaleForHwnd(hwnd));
        if (atlas)
        {
            ImGui::Text("Atlas: %d x %d", atlas->TexWidth, atlas->TexHeight);

            if (ImGui::Button("Rebuild fonts for current DPI"))
            {
                RequestFontRebuild(GetDpiScaleForHwnd(hwnd));
            }

            // Display atlas texture (DX11 backend sets TexID to ID3D11ShaderResourceView*)
            if (atlas->TexID != nullptr && atlas->TexWidth > 0 && atlas->TexHeight > 0)
            {
                const float avail = ImGui::GetContentRegionAvail().x;
                const float scale = (avail > 0.0f) ? (avail / static_cast<float>(atlas->TexWidth)) : 1.0f;
                const float w = static_cast<float>(atlas->TexWidth) * scale;
                const float h = static_cast<float>(atlas->TexHeight) * scale;
                ImGui::Image(atlas->TexID, ImVec2(w, h));
            }
            else
            {
                ImGui::TextDisabled("Atlas texture not available yet (TexID == nullptr).");
            }
        }
    }
    ImGui::End();
}

static void DrawImGuiDebugWindows(HWND hwnd)
{
    DebugUIState& s = DebugState();

    if (s.show_demo)
        ImGui::ShowDemoWindow(&s.show_demo);

    if (s.show_metrics)
        ImGui::ShowMetricsWindow(&s.show_metrics);

    if (s.show_about)
        ImGui::ShowAboutWindow(&s.show_about);

    if (s.show_style)
    {
        if (ImGui::Begin("Style Editor", &s.show_style))
            ImGui::ShowStyleEditor();
        ImGui::End();
    }

    if (s.show_imgui_info)
    {
        if (ImGui::Begin("ImGui Info", &s.show_imgui_info))
        {
            ImGuiIO& io = ImGui::GetIO();
            ImGui::Text("ImGui Version: %s", ImGui::GetVersion());
            ImGui::Separator();
            ImGui::Text("DeltaTime: %.4f", io.DeltaTime);
            ImGui::Text("Framerate: %.1f FPS", io.Framerate);
            ImGui::Text("DisplaySize: %.0f x %.0f", io.DisplaySize.x, io.DisplaySize.y);
            ImGui::Text("DPI scale (hwnd): %.2f", GetDpiScaleForHwnd(hwnd));
            ImGui::Separator();
            ImGui::Text("WantCaptureMouse: %s", io.WantCaptureMouse ? "true" : "false");
            ImGui::Text("WantCaptureKeyboard: %s", io.WantCaptureKeyboard ? "true" : "false");
            ImGui::Text("WantTextInput: %s", io.WantTextInput ? "true" : "false");
        }
        ImGui::End();
    }

    DrawFontAtlasWindow(hwnd);
}

static void DrawDockspaceAndMenuBar(HWND hwnd)
{
    ImGuiIO& io = ImGui::GetIO();
    DebugUIState& s = DebugState();

    if (!(io.ConfigFlags & ImGuiConfigFlags_DockingEnable))
        return;
    if (!s.enable_dockspace)
        return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_MenuBar |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    // Allow the game/rendering behind the dockspace to show through.
    ImGuiDockNodeFlags dock_flags = ImGuiDockNodeFlags_PassthruCentralNode;
    window_flags |= ImGuiWindowFlags_NoBackground;

    ImGui::Begin("DockSpace##ColonyGame", nullptr, window_flags);
    ImGui::PopStyleVar(3);

    // Dockspace
    const ImGuiID dockspace_id = ImGui::GetID("DockSpaceID##ColonyGame");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dock_flags);

    // Menu bar
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("ImGui"))
        {
            ImGui::MenuItem("Demo Window",    nullptr, &s.show_demo);
            ImGui::MenuItem("Metrics",        nullptr, &s.show_metrics);
            ImGui::MenuItem("Style Editor",   nullptr, &s.show_style);
            ImGui::MenuItem("About",          nullptr, &s.show_about);
            ImGui::MenuItem("Font Atlas",     nullptr, &s.show_font_atlas);
            ImGui::MenuItem("ImGui Info",     nullptr, &s.show_imgui_info);

            ImGui::Separator();

            if (ImGui::MenuItem("Rebuild fonts for current DPI"))
                RequestFontRebuild(GetDpiScaleForHwnd(hwnd));

            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

    ImGui::End();
}

// RAII guard to preserve OM render targets and viewports during platform window rendering.
// This prevents ImGui multi-viewport path from leaving your state altered.
struct D3D11StateGuard
{
    ID3D11DeviceContext* ctx = nullptr;

    // OM
    ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    ID3D11DepthStencilView* dsv = nullptr;

    // RS
    UINT numViewports = 0;
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};

    // Scissors
    UINT numScissors = 0;
    D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};

    explicit D3D11StateGuard(ID3D11DeviceContext* c) : ctx(c)
    {
        if (!ctx) return;

        ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsv);

        UINT vpCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        ctx->RSGetViewports(&vpCount, viewports);
        numViewports = vpCount;

        UINT scCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        ctx->RSGetScissorRects(&scCount, scissors);
        numScissors = scCount;
    }

    ~D3D11StateGuard()
    {
        if (!ctx) return;

        ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, dsv);
        ctx->RSSetViewports(numViewports, viewports);
        ctx->RSSetScissorRects(numScissors, scissors);

        for (auto*& r : rtvs) { if (r) { r->Release(); r = nullptr; } }
        if (dsv) { dsv->Release(); dsv = nullptr; }
    }
};

} // namespace

bool ImGuiLayer::initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context)
{
    if (m_initialized)
        return true;

    assert(hwnd && device && context);

    m_hwnd    = hwnd;
    m_device  = device;
    m_context = context;

    // Hold references so we don't dangle if the caller releases early.
    if (m_device)  m_device->AddRef();
    if (m_context) m_context->AddRef();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    // -------------------------------
    // Style & Configuration
    // -------------------------------
    ImGui::StyleColorsDark();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // multi-viewport native windows
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    // Persist settings/log under %LOCALAPPDATA%\ColonyGame\...
    SetImGuiIniAndLogToLocalAppData();

    // -------------------------------
    // DPI awareness (backend helper)
    // -------------------------------
    ImGui_ImplWin32_EnableDpiAwareness();

    // -------------------------------
    // Backends: Win32 + D3D11
    // -------------------------------
    if (!ImGui_ImplWin32_Init(m_hwnd))
    {
        ImGui::DestroyContext();
        if (m_context) { m_context->Release(); m_context = nullptr; }
        if (m_device)  { m_device->Release();  m_device  = nullptr; }
        m_hwnd = nullptr;
        return false;
    }

    if (!ImGui_ImplDX11_Init(m_device, m_context))
    {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        if (m_context) { m_context->Release(); m_context = nullptr; }
        if (m_device)  { m_device->Release();  m_device  = nullptr; }
        m_hwnd = nullptr;
        return false;
    }

#if CG_IMGUI_LAYER_ENABLE_ALPHA_COMPOSITING
    // Optional: enable per‑pixel alpha compositing for OS windows (useful for transparent clears / overlays).
    ImGui_ImplWin32_EnableAlphaCompositing((void*)m_hwnd);
#endif

    ConfigureStyleForViewports();

    // Build initial fonts for current monitor DPI.
    RequestFontRebuild(GetDpiScaleForHwnd(m_hwnd));
    ApplyPendingDpiRebuild(m_hwnd);

    m_initialized = true;
    enabled = true;
    return true;
}

void ImGuiLayer::shutdown()
{
    if (!m_initialized)
        return;

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    m_initialized = false;
    enabled = false;

    m_hwnd = nullptr;

    if (m_context) { m_context->Release(); m_context = nullptr; }
    if (m_device)  { m_device->Release();  m_device  = nullptr; }
}

void ImGuiLayer::newFrame()
{
    if (!m_initialized || !enabled)
        return;

    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = ComputeDeltaTimeSeconds();

    // Apply deferred DPI/font rebuild safely at the start of a frame.
    ApplyPendingDpiRebuild(m_hwnd);

    // Order matches examples; either order typically works.
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::render()
{
    if (!m_initialized || !enabled)
        return;

    // Dockspace host + debug windows must be built BEFORE ImGui::Render().
    DrawDockspaceAndMenuBar(m_hwnd);
    DrawImGuiDebugWindows(m_hwnd);

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    // Optional: multi-viewport (native OS windows)
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        D3D11StateGuard guard(m_context);
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

bool ImGuiLayer::handleWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (!m_initialized)
        return false;

    // Always forward input/messages to ImGui (recommended by ImGui FAQ style integration guidance).
    const bool backend_consumed = (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam) != 0);

    // WM_DPICHANGED: request a deferred rebuild (safe & avoids doing device work inside WndProc).
    if (msg == WM_DPICHANGED)
    {
        // wParam: new DPI (LOWORD/HIWORD). 96 DPI => scale 1.0f
        const UINT dpi_x = LOWORD(wParam);
        const float scale = (dpi_x > 0) ? (static_cast<float>(dpi_x) / 96.0f) : 1.0f;
        RequestFontRebuild(scale);
    }

#if CG_IMGUI_LAYER_USE_WANT_CAPTURE_FLAGS
    // Optional input routing enhancement (OFF by default to avoid changing legacy WndProc behavior).
    // If enabled, this returns true when ImGui wants to capture the input category.
    if (ImGui::GetCurrentContext() != nullptr)
    {
        ImGuiIO& io = ImGui::GetIO();

        if ((io.WantCaptureMouse && IsMouseMessage(msg)) ||
            (io.WantCaptureKeyboard && IsKeyboardMessage(msg)) ||
            (io.WantTextInput && IsTextInputMessage(msg)))
        {
            return true;
        }
    }
#endif

    return backend_consumed;
}
