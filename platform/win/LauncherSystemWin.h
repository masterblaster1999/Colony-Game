#pragma once

#include <string>
#include <windows.h>

// These are just declarations of functions that currently live
// in WinLauncher.cpp. We’ll move their bodies to the .cpp file.

std::wstring LastErrorMessage(DWORD err = GetLastError());

void MsgBox(const std::wstring& title,
            const std::wstring& text,
            UINT flags = MB_ICONERROR | MB_OK);

void EnableHeapTerminationOnCorruption();
void EnableSafeDllSearch();
void EnableHighDpiAwareness();
void DisablePowerThrottling();
