// src/ui/ImGuiLayer.cpp
#include "ImGuiLayer.h"

// Pull Windows.h with proper guards (WIN32_LEAN_AND_MEAN/NOMINMAX) once.
#include "platform/win/WinCommon.h"

// Use shared Windows path helpers (writable dir, res dir, exe dir, etc.)
#include "platform/win/PathUtilWin.h"

// ---------- Dear ImGui: core + robust Win32/DX11 backend includes ----------
#include <imgui.h>

#ifndef __has_include
  #define __has_include(x) 0
#endif

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
#include <cmath>
#include <cstdint>
#include <d3d11.h>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

using namespace cg::ui;

// --------------------------------------------------------------------------------------
// Compile-time knobs (safe defaults).
// --------------------------------------------------------------------------------------

// If 1, handleWndProc() will ALSO return true for mouse/keyboard/text messages when
// ImGui IO says it wants to capture them. Useful if your input system uses handleWndProc()
// to block game input.
//
// Keep 0 if your Win32 WndProc returns early to DefWindowProc() when this returns true.
#ifndef CG_IMGUI_LAYER_USE_WANT_CAPTURE_FLAGS
  #define CG_IMGUI_LAYER_USE_WANT_CAPTURE_FLAGS 0
#endif

// Base font size at 96 DPI.
#ifndef CG_IMGUI_LAYER_BASE_FONT_PX
  #define CG_IMGUI_LAYER_BASE_FONT_PX 13.0f
#endif

// ImGui may or may not define this depending on version; provide fallback.
#ifndef ImTextureID_Invalid
  #define ImTextureID_Invalid ((ImTextureID)0)
#endif

namespace {

struct DebugUIState
{
    bool show_demo        = false;
    bool show_metrics     = false;
    bool show_about       = false;
    bool show_style       = false;
    bool show_font_atlas  = false;
    bool show_imgui_info  = false;

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

// Wide -> UTF-8 (no NUL written). Safe for /W4 (checks int range).
static std::string WideToUtf8(const std::wstring& ws)
{
    if (ws.empty())
        return {};

    if (ws.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
        return {};

    const int wlen = static_cast<int>(ws.size());

    const int needed = ::WideCharToMultiByte(
        CP_UTF8, 0,
        ws.data(), wlen,
        nullptr, 0,
        nullptr, nullptr);

    if (needed <= 0)
        return {};

    std::string out(static_cast<size_t>(needed), '\0');

    const int written = ::WideCharToMultiByte(
        CP_UTF8, 0,
        ws.data(), wlen,
        out.data(), needed,
        nullptr, nullptr);

    if (written != needed)
        out.clear();

    return out;
}

// Persist imgui.ini and imgui_log.txt under the same writable dir the launcher uses.
// (winpath::writable_data_dir() already creates directories best-effort.)
static void SetImGuiIniAndLogToWritableDataDir()
{
    ImGuiIO& io = ImGui::GetIO();

    const std::filesystem::path dir = winpath::writable_data_dir();
    if (dir.empty())
        return;

    const std::filesystem::path iniFile = dir / L"imgui.ini";
    const std::filesystem::path logFile = dir / L"imgui_log.txt";

    static std::string s_iniUtf8;
    static std::string s_logUtf8;

    s_iniUtf8 = WideToUtf8(iniFile.wstring());
    s_logUtf8 = WideToUtf8(logFile.wstring());

    if (!s_iniUtf8.empty())
        io.IniFilename = s_iniUtf8.c_str();
    if (!s_logUtf8.empty())
        io.LogFilename = s_logUtf8.c_str();
}

// --- DPI/font rebuild machinery (deferred so we don’t do GPU work inside WndProc) ---
static bool  g_dpiRebuildRequested = false;
static float g_dpiRebuildScale     = 0.0f;

static float GetDpiScaleForHwnd(HWND hwnd)
{
    float scale = 1.0f;
    if (hwnd)
        scale = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
    if (scale <= 0.0f)
        scale = 1.0f;
    return scale;
}

static void RequestFontRebuild(float scale /* 0 => auto */)
{
    g_dpiRebuildRequested = true;
    g_dpiRebuildScale     = scale;
}

static std::filesystem::path FindDefaultFontOnDisk()
{
    const std::filesystem::path base = winpath::resource_dir() / L"fonts";
    if (base.empty())
        return {};

    const std::filesystem::path candidates[] = {
        base / L"Inter-Regular.ttf",
        base / L"Inter.ttf",
        base / L"Roboto-Regular.ttf",
        base / L"Roboto-Medium.ttf",
        base / L"SegoeUI.ttf",
    };

    for (const auto& p : candidates)
    {
        std::error_code ec;
        if (std::filesystem::exists(p, ec) && !ec)
            return p;
    }
    return {};
}

// Rebuild fonts sized to the chosen DPI scale + scale style sizes.
// Recreates the DX11 font atlas GPU resources.
static void RebuildFontsForScale(float scale)
{
    if (scale <= 0.0f)
        scale = 1.0f;

    static float s_lastScale = 0.0f;

    ImGuiIO& io = ImGui::GetIO();
    if (io.Fonts && io.Fonts->IsBuilt() && std::fabs(scale - s_lastScale) < 0.01f)
        return;

    // Scale style sizes proportionally.
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

    ImFont* font = nullptr;
    const std::filesystem::path fontPath = FindDefaultFontOnDisk();
    if (!fontPath.empty())
    {
        const std::string fontUtf8 = WideToUtf8(fontPath.wstring());
        if (!fontUtf8.empty())
            font = io.Fonts->AddFontFromFileTTF(fontUtf8.c_str(), cfg.SizePixels, &cfg, io.Fonts->GetGlyphRangesDefault());
    }

    if (!font)
        io.Fonts->AddFontDefault(&cfg);

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

// --- Debug UI + Docking host -------------------------------------------------

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

            const bool has_tex =
                (atlas->TexID != ImTextureID_Invalid) &&
                (atlas->TexWidth > 0) &&
                (atlas->TexHeight > 0);

            if (has_tex)
            {
                const float avail = ImGui::GetContentRegionAvail().x;
                const float scale = (avail > 0.0f) ? (avail / static_cast<float>(atlas->TexWidth)) : 1.0f;
                const float w = static_cast<float>(atlas->TexWidth) * scale;
                const float h = static_cast<float>(atlas->TexHeight) * scale;
                ImGui::Image(atlas->TexID, ImVec2(w, h));
            }
            else
            {
                ImGui::TextDisabled("Atlas texture not available yet (TexID == invalid).");
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

    // Allow the game to show through the central node.
    ImGuiDockNodeFlags dock_flags = ImGuiDockNodeFlags_PassthruCentralNode;
    window_flags |= ImGuiWindowFlags_NoBackground;

    ImGui::Begin("DockSpace##ColonyGame", nullptr, window_flags);
    ImGui::PopStyleVar(3);

    const ImGuiID dockspace_id = ImGui::GetID("DockSpaceID##ColonyGame");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dock_flags);

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("ImGui"))
        {
            ImGui::MenuItem("Demo Window",   nullptr, &s.show_demo);
            ImGui::MenuItem("Metrics",       nullptr, &s.show_metrics);
            ImGui::MenuItem("Style Editor",  nullptr, &s.show_style);
            ImGui::MenuItem("About",         nullptr, &s.show_about);
            ImGui::MenuItem("Font Atlas",    nullptr, &s.show_font_atlas);
            ImGui::MenuItem("ImGui Info",    nullptr, &s.show_imgui_info);

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
// Prevents multi-viewport path from leaving your D3D11 state altered.
struct D3D11StateGuard
{
    ID3D11DeviceContext* ctx = nullptr;

    ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    ID3D11DepthStencilView* dsv = nullptr;

    UINT numViewports = 0;
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};

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

#if CG_IMGUI_LAYER_USE_WANT_CAPTURE_FLAGS
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
#endif // CG_IMGUI_LAYER_USE_WANT_CAPTURE_FLAGS

} // namespace

bool ImGuiLayer::initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context)
{
    if (m_initialized)
        return true;

    assert(hwnd && device && context);

    m_hwnd    = hwnd;
    m_device  = device;
    m_context = context;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    ImGui::StyleColorsDark();

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    // Store ImGui settings/logs in app data.
    SetImGuiIniAndLogToWritableDataDir();

    // Win32 backend helpers
    ImGui_ImplWin32_EnableDpiAwareness();

    if (!ImGui_ImplWin32_Init(m_hwnd))
        return false;

    if (!ImGui_ImplDX11_Init(m_device, m_context))
        return false;

    // Optional, but harmless if supported: improves per-pixel alpha behavior for viewport windows.
    ImGui_ImplWin32_EnableAlphaCompositing(m_hwnd);

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
    m_hwnd = nullptr;
    m_device = nullptr;
    m_context = nullptr;
}

void ImGuiLayer::newFrame()
{
    if (!m_initialized || !enabled)
        return;

    // Apply deferred DPI/font rebuild safely at the start of a frame.
    ApplyPendingDpiRebuild(m_hwnd);

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::render()
{
    if (!m_initialized || !enabled)
        return;

    // Dockspace host + debug windows before Render().
    DrawDockspaceAndMenuBar(m_hwnd);
    DrawImGuiDebugWindows(m_hwnd);

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    // Multi-viewport rendering
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

    const bool backend_consumed = (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam) != 0);

    // WM_DPICHANGED: request rebuild; do actual GPU work in newFrame().
    if (msg == WM_DPICHANGED)
    {
        const UINT dpi_x = LOWORD(wParam);
        const float scale = (dpi_x > 0) ? (static_cast<float>(dpi_x) / 96.0f) : 1.0f;
        RequestFontRebuild(scale);
    }

#if CG_IMGUI_LAYER_USE_WANT_CAPTURE_FLAGS
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
