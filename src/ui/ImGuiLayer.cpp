#include "ImGuiLayer.h"

using namespace cg::ui;

bool ImGuiLayer::initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context)
{
    m_hwnd = hwnd; m_device = device; m_context = context;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    // Style
    ImGui::StyleColorsDark();

    // Backends: Win32 + D3D11
    if (!ImGui_ImplWin32_Init(m_hwnd)) return false;
    if (!ImGui_ImplDX11_Init(m_device, m_context)) return false;

    // A few sensible defaults for desktop HUDs
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = "imgui.ini"; // will be created next to exe

    m_initialized = true;
    return true;
}

void ImGuiLayer::shutdown()
{
    if (!m_initialized) return;
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    m_initialized = false;
}

void ImGuiLayer::newFrame()
{
    if (!enabled) return;
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::render()
{
    if (!enabled) return;
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

bool ImGuiLayer::handleWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam) != 0;
}
