#pragma once
#include <windows.h>

class AppWindow; // forward declaration

LRESULT CALLBACK ColonyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool CreateMainWindow(HINSTANCE   hInstance,
                      int         nCmdShow,
                      AppWindow&  appWindow,
                      HWND&       outHwnd);
