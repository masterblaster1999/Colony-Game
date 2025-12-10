// src/app/EntryWinMain.cpp
#include <windows.h>
int GameMain(HINSTANCE, PWSTR, int); // forward

int WINAPI wWinMain(HINSTANCE h, HINSTANCE, PWSTR cmd, int show)
{
    return GameMain(h, cmd, show);
}
