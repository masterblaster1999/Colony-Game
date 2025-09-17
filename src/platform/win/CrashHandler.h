#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void InitCrashHandler(const wchar_t* dumpDir);
LONG WINAPI TopLevelExceptionHandler(struct _EXCEPTION_POINTERS* pInfo);
