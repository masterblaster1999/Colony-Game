#pragma once
#include <windows.h>
#include "DxDevice.h"

class AppWindow {
public:
    bool Create(HINSTANCE hInst, int nCmdShow, int width, int height);
    int  MessageLoop();
private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMsg(HWND, UINT, WPARAM, LPARAM);

    void RegisterRawMouse(HWND hwnd);

    HWND m_hwnd = nullptr;
    DxDevice m_gfx;
    bool m_vsync = false;
    UINT m_width = 1280, m_height = 720;
};
