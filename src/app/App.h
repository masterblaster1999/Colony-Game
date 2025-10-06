#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <memory>
#include "../render/DeviceD3D11.h"
#include "../terrain/ThermalErosion.h"
#include "../core/FixedTimestep.h"

class App
{
public:
    App();
    int Run(HINSTANCE hInstance);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    bool CreateMainWindow(HINSTANCE, int nCmdShow);
    void OnResize(UINT w, UINT h);
    void Update(double dt);
    void Render();

private:
    HWND                m_hwnd = nullptr;
    render::DeviceD3D11 m_gfx;
    terrain::ThermalErosion m_erosion;
    core::FixedTimestep m_step{ 60.0 };

    UINT m_width = 1280, m_height = 720;
};
