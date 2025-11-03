// src/ui/ImGuiLayer.cpp
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
#include <cmath>
#include <d3d11.h>        // for state backup constants (safe even if already included)
#include <ShlObj.h>       // SHGetKnownFolderPath
#include <KnownFolders.h> // FOLDERID_LocalAppData
#include <filesystem>
#include <string>

#pragma comment(lib, "Shell32.lib") // SHGetKnownFolderPath

using namespace cg::ui;

// ---------------------------------------------------------------------------
// FIX for C3861: Dear ImGui Win32 backend asks you to forward-declare this.
// Official examples do exactly this before using it in WndProc.            // 
// Ref: "Forward declare message handler from imgui_impl_win32.cpp"         //
//      (see Dear ImGui examples and backend notes).                         //
// ---------------------------------------------------------------------------
extern IMGUI_IMPL_API LRESULT
ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
// (Alternative if you want to pass IO explicitly)
// extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandlerEx(HWND,UINT,WPARAM,LPARAM,ImGuiIO&);
// ---------------------------------------------------------------------------

namespace {

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

// Convert wide path to UTF-8 (for ImGui::IO::IniFilename).
static std::string WideToUtf8(const std::wstring& ws)
{
    if (ws.empty()) return {};
    int len = ::WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out;
    if (len > 1)
    {
        out.resize(static_cast<size_t>(len - 1));
        ::WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, out.data(), len, nullptr, nullptr);
    }
    return out;
}

// Write imgui.ini to %LOCALAPPDATA%\ColonyGame\imgui.ini
// Uses SHGetKnownFolderPath(FOLDERID_LocalAppData) to respect Windows storage guidance.
static void SetImGuiIniToLocalAppData()
{
    ImGuiIO& io = ImGui::GetIO();

    PWSTR wpath = nullptr;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &wpath)))
    {
        std::filesystem::path base(wpath);
        ::CoTaskMemFree(wpath);

        std::filesystem::path dir  = base / L"ColonyGame";
        std::filesystem::path file = dir / L"imgui.ini";

        std::error_code ec;
        std::filesystem::create_directories(dir, ec); // best-effort

        static std::string s_iniUtf8;
        s_iniUtf8 = WideToUtf8(file.wstring());
        if (!s_iniUtf8.empty())
            io.IniFilename = s_iniUtf8.c_str();
        // else: ImGui falls back to the default "imgui.ini" next to the exe.
    }
}

// Build fonts sized to the monitor DPI for crisp text, and scale style sizes.
// Recreates the DX11 font atlas on the GPU (no-op if scale didn't change).
static void RebuildFontsForDpi(HWND hwnd)
{
    float scale = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
    if (scale <= 0.0f) scale = 1.0f;

    static float s_lastScale = 0.0f;
    ImGuiIO& io = ImGui::GetIO();

    // Skip work if we already built for an equivalent scale and an atlas exists.
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
    cfg.SizePixels = 13.0f * scale; // default ImGui font is ~13px at 96 DPI
    io.Fonts->AddFontDefault(&cfg);

    // Recreate the font atlas GPU resources immediately.
    ImGui_ImplDX11_InvalidateDeviceObjects();
    (void)ImGui_ImplDX11_CreateDeviceObjects(); // ignore return; backend will lazily retry if device is lost
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

    // (Optional) Keep scissor rects as well for safety.
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

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    // -------------------------------
    // Style & Configuration
    // -------------------------------
    ImGui::StyleColorsDark();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // enable gamepad nav by default
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;      // 🚀 docking support
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;    // 🚀 multi-viewport OS windows
    io.ConfigWindowsMoveFromTitleBarOnly = true;           // small UX improvement

    // Persist settings under %LOCALAPPDATA%\ColonyGame\imgui.ini (Windows-friendly location).
    SetImGuiIniToLocalAppData(); // uses SHGetKnownFolderPath(FOLDERID_LocalAppData)  // MS docs: SHGetKnownFolderPath. :contentReference[oaicite:4]{index=4}

    // -------------------------------
    // DPI awareness (Win32 helper)
    // -------------------------------
    // Safe to call once per process; backend will do the right thing.
    ImGui_ImplWin32_EnableDpiAwareness();

    // -------------------------------
    // Backends: Win32 + D3D11
    // -------------------------------
    if (!ImGui_ImplWin32_Init(m_hwnd))
        return false;
    if (!ImGui_ImplDX11_Init(m_device, m_context))
        return false;

    // Optional: enable per‑pixel alpha for popped‑out OS windows (nice with transparent clear)
    // Note: this is safe to call even if viewports are disabled.
    ImGui_ImplWin32_EnableAlphaCompositing(m_hwnd);

    // Match style to viewport setting and build a DPI-appropriate font atlas.
    ConfigureStyleForViewports();
    RebuildFontsForDpi(m_hwnd);

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

    // Order matches examples; either order typically works.
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::render()
{
    if (!m_initialized || !enabled)
        return;

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    // Optional: multi-viewport (lets ImGui windows pop out as native OS windows)
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        // Preserve your RT/DSV + viewport/scissor bindings while backend renders platform windows.
        D3D11StateGuard guard(m_context);
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        // guard dtor restores state here
    }
}

bool ImGuiLayer::handleWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Forward to ImGui first. If it handles the message, we can consume it.
    // FIX for C2737: initialize const at declaration.
    const bool consumed = (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam) != 0); // MSVC C2737 requires init. :contentReference[oaicite:5]{index=5}

    // Handle DPI changes to keep fonts sharp when moving across monitors.
    // (The backend already adjusts scaling; we rebuild the atlas and scale style sizes to match.)
    if (msg == WM_DPICHANGED && m_initialized)
    {
        RebuildFontsForDpi(hWnd);
    }

    return consumed;
}

