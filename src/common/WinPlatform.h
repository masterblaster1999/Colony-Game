#pragma once

// Windows target + lean headers
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif

// Unicode everywhere
#ifndef UNICODE
#  define UNICODE
#endif
#ifndef _UNICODE
#  define _UNICODE
#endif

// Windows and COM/WRL
#include <Windows.h>
#include <wrl/client.h>    // Microsoft::WRL::ComPtr
#include <combaseapi.h>

// DirectX 11 + DXGI + Direct2D/DirectWrite + WIC
#include <d3d11.h>
#include <dxgi.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wincodec.h>

// Link libs for MSVC builds (if not already in your .vcxproj)
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

// Wide literal helpers (fixes previous incorrect L#x usage)
#define CG_WIDEN2(x) L##x
#define CG_WIDEN(x)  CG_WIDEN2(x)
