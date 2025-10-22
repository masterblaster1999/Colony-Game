// src/ui/ImGuiLayer.cpp
#include "ImGuiLayer.h"

// ✅ Added core + Win32/DX11 backend headers (fixes C3861 errors)
#include <imgui.h>
#include <imgui/backends/imgui_impl_win32.h>
#include <imgui/backends/imgui_impl_dx11.h>

#include <cassert>

using namespace cg::ui;

bool ImGuiLayer::initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context)
{
    m_hwnd   = hwnd;
    m_device = device;
    m_context = context;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    // -------------------------------
    // Style & Configuration
    // -------------------------------
    ImGui::StyleColorsDark();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // 🚀 enable docking support
    io.IniFilename = "imgui.ini"; // saved next to exe

    // -------------------------------
    // DPI awareness (Win32 helper)
    // -------------------------------
    ImGui_ImplWin32_EnableDpiAwareness();

    // -------------------------------
    // Backends: Win32 + D3D11
    // -------------------------------
    if (!ImGui_ImplWin32_Init(m_hwnd))
        return false;
    if (!ImGui_ImplDX11_Init(m_device, m_context))
        return false;

    m_initialized = true;
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
    if (!enabled)
        return;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::render()
{
    if (!enabled)
        return;

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    // Optional: multi-viewport (future expansion)
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

bool ImGuiLayer::handleWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam) != 0;
}
