// platform/win/LauncherEmbeddedSafeModeWin.cpp

#include "platform/win/LauncherEmbeddedSafeModeWin.h"

#ifdef COLONY_EMBED_GAME_LOOP

#ifndef UNICODE
#   define UNICODE
#endif
#ifndef _UNICODE
#   define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#   define NOMINMAX
#endif

#include <windows.h>
#include <sstream>
#include <iomanip>

#include "platform/win/LauncherSystemWin.h"
#include "platform/win/LauncherLoggingWin.h"
#include "platform/win/DpiMessagesWin.h"

#include "colony/world/World.h"
#include "colony/loop/GameLoop.h"

namespace
{
    struct EmbeddedState
    {
        colony::RenderSnapshot snapshot;
    };

    EmbeddedState    g_state;
    windpi::DpiState g_embedded_dpi; // per-window DPI state for the embedded GDI view

    LRESULT CALLBACK EmbeddedWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        // Handle per-monitor DPI changes (WM_DPICHANGED).
        // Keeps physical window size consistent when moved between monitors,
        // and gives us a live DPI scale for drawing.
        LRESULT dpiResult = 0;
        if (windpi::TryHandleMessage(hwnd, msg, wParam, lParam, g_embedded_dpi, dpiResult))
        {
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return dpiResult;
        }

        switch (msg)
        {
            case WM_PAINT:
            {
                PAINTSTRUCT ps{};
                HDC         dc = ::BeginPaint(hwnd, &ps);

                RECT rc{};
                ::GetClientRect(hwnd, &rc);

                // Background
                HBRUSH bg = ::CreateSolidBrush(RGB(32, 32, 48));
                ::FillRect(dc, &rc, bg);
                ::DeleteObject(bg);

                ::SetBkMode(dc, TRANSPARENT);
                ::SetTextColor(dc, RGB(220, 220, 230));

                HFONT font    = static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
                HFONT oldFont = static_cast<HFONT>(::SelectObject(dc, font));

                const int   w     = rc.right - rc.left;
                const int   h     = rc.bottom - rc.top;
                const float scale = 60.0f * g_embedded_dpi.scale; // DPI-aware scale
                const float cx    = w * 0.5f;
                const float cy    = h * 0.5f;

                // Agents
                HBRUSH agentBrush = ::CreateSolidBrush(RGB(80, 200, 255));
                HBRUSH oldBrush   = static_cast<HBRUSH>(::SelectObject(dc, agentBrush));

                HPEN pen    = ::CreatePen(PS_SOLID, 1, RGB(20, 120, 180));
                HPEN oldPen = static_cast<HPEN>(::SelectObject(dc, pen));

                for (const auto& p : g_state.snapshot.agent_positions)
                {
                    const int x = static_cast<int>(cx + static_cast<float>(p.x) * scale);
                    const int y = static_cast<int>(cy - static_cast<float>(p.y) * scale);
                    const int r = static_cast<int>(6.0f * g_embedded_dpi.scale); // DPI-aware radius

                    ::Ellipse(dc, x - r, y - r, x + r, y + r);
                }

                ::SelectObject(dc, oldPen);
                ::DeleteObject(pen);

                ::SelectObject(dc, oldBrush);
                ::DeleteObject(agentBrush);

                // HUD
                std::wstringstream hud;
                hud << L"Embedded Safe Mode | sim_step=" << g_state.snapshot.sim_step
                    << L"  sim_time=" << std::fixed << std::setprecision(2)
                    << g_state.snapshot.sim_time;

                const std::wstring hudText = hud.str();

                // Keep HUD padding roughly constant in physical size.
                const int pad = windpi::DipToPx(8, g_embedded_dpi.dpi);

                ::TextOutW(dc, pad, pad, hudText.c_str(),
                           static_cast<int>(hudText.size()));

                ::SelectObject(dc, oldFont);
                ::EndPaint(hwnd, &ps);
                return 0;
            }

            case WM_DESTROY:
                ::PostQuitMessage(0);
                return 0;

            default:
                return ::DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    }
}

namespace winlaunch
{
    int RunEmbeddedGameLoop(std::wostream& log)
    {
        // 1) Simple Win32 window (no D3D, just GDI).
        HINSTANCE      hInst  = ::GetModuleHandleW(nullptr);
        const wchar_t* kClass = L"ColonyEmbeddedGameWindow";

        WNDCLASSW wc{};
        wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc   = &EmbeddedWndProc;
        wc.hInstance     = hInst;
        wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = kClass;

        if (!::RegisterClassW(&wc))
        {
            MsgBox(L"Colony Game", L"Failed to register embedded window class.");
            return 10;
        }

        HWND hwnd = ::CreateWindowExW(
            0,
            kClass,
            L"Colony Game (Embedded Safe Mode)",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT,
            1024, 768,
            nullptr,
            nullptr,
            hInst,
            nullptr
        );

        if (!hwnd)
        {
            MsgBox(L"Colony Game", L"Failed to create embedded window.");
            return 11;
        }

        // Initialize DPI state immediately so drawing scale is correct from frame 1.
        windpi::InitFromHwnd(hwnd, g_embedded_dpi);

        // 2) Build the world and run a fixed-timestep loop.
        colony::World          world;
        colony::GameLoopConfig cfg{};
        cfg.fixed_dt              = 1.0 / 60.0;
        cfg.max_frame_time        = 0.25;
        cfg.max_updates_per_frame = 5;
        cfg.run_when_minimized    = false;

        auto render = [&](const colony::World& w, float alpha)
        {
            g_state.snapshot = w.snapshot(alpha);
            ::InvalidateRect(hwnd, nullptr, FALSE);
        };

        WriteLog(log, L"[Embedded] Running fixed-timestep loop.");

        const int exitCode = colony::RunGameLoop(world, render, hwnd, cfg);

        ::DestroyWindow(hwnd);
        ::UnregisterClassW(kClass, hInst);

        return exitCode;
    }
}

#endif // COLONY_EMBED_GAME_LOOP
