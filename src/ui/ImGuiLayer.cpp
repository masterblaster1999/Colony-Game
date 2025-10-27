// src/ui/ImGuiLayer.cpp
#include "ImGuiLayer.h"

// ✅ Core + Win32/DX11 backend headers
#include <imgui.h>
#include <imgui/backends/imgui_impl_win32.h>
#include <imgui/backends/imgui_impl_dx11.h>

#include <cassert>

using namespace cg::ui;

namespace {

// When multi-viewport is enabled, ImGui suggests tweaking style so platform windows
// look identical to the main window. (Matches the official examples.)
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

// Build fonts sized to the monitor DPI for crisp text.
// Recreates the DX11 font atlas on the GPU.
static void RebuildFontsForDpi(HWND hwnd)
{
    // Backend helper is declared by imgui_impl_win32.h
    float scale = 1.0f;
    // If the backend DPI functions are available, use them.
    scale = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
    if (scale <= 0.0f) scale = 1.0f;

    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    ImFontConfig cfg;
    cfg.SizePixels = 13.0f * scale; // default ImGui font is ~13px at 96 DPI
    io.Fonts->AddFontDefault(&cfg);

    // Recreate the font atlas GPU resources immediately.
    ImGui_ImplDX11_InvalidateDeviceObjects();
    ImGui_ImplDX11_CreateDeviceObjects();
}

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
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // 🚀 docking support
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // 🚀 multi-viewport OS windows
    io.IniFilename = "imgui.ini";                       // saved next to exe

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
}

void ImGuiLayer::newFrame()
{
    if (!m_initialized || !enabled)
        return;

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
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

bool ImGuiLayer::handleWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Forward to ImGui first. If it handles the message, we can consume it.
    const bool consumed = ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam) != 0;

    // Handle DPI changes to keep fonts sharp when moving across monitors.
    // (The backend already adjusts scaling; we rebuild the atlas to match.)
    if (msg == WM_DPICHANGED && m_initialized)
    {
        RebuildFontsForDpi(hWnd);
    }

    return consumed;
}
