#pragma once
#include <string>
#include <filesystem>
#include <windows.h>

std::filesystem::path GetExecutableDir();
std::wstring          GetLastErrorMessage(DWORD err = GetLastError());

// Best effort: restrict DLL search dirs if API is available on this OS.
void TryHardenDllSearch();
