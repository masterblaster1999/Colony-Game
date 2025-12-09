#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstdint>

#include "../render/DeviceD3D11.h"
#include "../terrain/ThermalErosion.h"
#include "../core/FixedTimestep.h"   // defines colony::FixedStepper + FixedSettings

class App {
public:
    App();
    int Run(HINSTANCE hInstance);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    bool CreateMainWindow(HINSTANCE, int nCmdShow);
    void OnResize(UINT w, UINT h);
    void Update(double dt);
    void Render();                    // keep simple Render(), ignore alpha

private:
    HWND                   m_hwnd   = nullptr;
    render::DeviceD3D11    m_gfx;    // D3D11 device/swapchain wrapper
    terrain::ThermalErosion m_erosion;

    // New timing object: 60 Hz fixed ticks, up to 5 catch-up ticks, clamp long frames to 0.25s
    colony::FixedStepper   m_step{ colony::FixedSettings{ 60.0, 5, 0.25 } };

    UINT m_width  = 1280;
    UINT m_height = 720;
};
