#pragma once
#include <windows.h>

// Dear ImGui (core only; backends are included in the .cpp)
#include <imgui.h>

// Forward declarations to avoid leaking renderer headers from the public interface
struct ID3D11Device;
struct ID3D11DeviceContext;

namespace cg::ui
{
    class ImGuiLayer
    {
    public:
        bool initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context);
        void shutdown();

        // Call once per frame (before you draw any ImGui widgets)
        void newFrame();

        // Call once per frame (after you've built your UI)
        void render();

        // Route Win32 messages to ImGui backend; return true if it consumed the message.
        bool handleWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

        // Input gating flags (so you can stop camera/game input when UI is active).
        bool wantsMouse() const { return ImGui::GetIO().WantCaptureMouse; }
        bool wantsKeyboard() const { return ImGui::GetIO().WantCaptureKeyboard; }

        // Optional: show/hide the whole HUD
        bool enabled = true;

    private:
        HWND m_hwnd = nullptr;
        ID3D11Device* m_device = nullptr;
        ID3D11DeviceContext* m_context = nullptr;
        bool m_initialized = false;
    };
}

// You must define this in your Win32 translation unit (or include ImGui_ImplWin32.h):
// extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
