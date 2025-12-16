// src/tools/ColonyMapViewerMain.cpp
//
// Colony-Game Map Viewer (Windows-only)
// ------------------------------------
// Standalone debug tool: Win32 + DirectX11 + Dear ImGui (Docking)
// - Loads a simple JSON heightmap/tilemap into a GPU texture and displays it
// - Zoom + scroll (pan) + optional grid overlay
// - Hover to inspect tile coords/value, click to select
// - File -> Open... (Win32 Open File dialog) and Drag & Drop onto window
//
// This is intentionally self-contained so it does NOT depend on DxDevice/AppWindow
// (DxDevice currently doesn't expose its D3D device/context for custom drawing).

#if !defined(_WIN32)
#error "ColonyMapViewer is Windows-only."
#endif

// Project-wide Windows header policy (defines WIN32_LEAN_AND_MEAN, NOMINMAX, UNICODE, etc.)
#include "platform/win/WinCommon.h"

#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <commdlg.h>  // GetOpenFileNameW
#include <shellapi.h> // DragAcceptFiles, CommandLineToArgvW

// Dear ImGui
#include <imgui.h>

// vcpkg's imgui port may install backend headers in different include layouts.
// Be defensive so this compiles regardless of the install layout.
#if __has_include(<imgui_impl_win32.h>)
#include <imgui_impl_win32.h>
#elif __has_include(<backends/imgui_impl_win32.h>)
#include <backends/imgui_impl_win32.h>
#elif __has_include(<imgui/backends/imgui_impl_win32.h>)
#include <imgui/backends/imgui_impl_win32.h>
#else
#error "Could not find imgui Win32 backend header (imgui_impl_win32.h)."
#endif

#if __has_include(<imgui_impl_dx11.h>)
#include <imgui_impl_dx11.h>
#elif __has_include(<backends/imgui_impl_dx11.h>)
#include <backends/imgui_impl_dx11.h>
#elif __has_include(<imgui/backends/imgui_impl_dx11.h>)
#include <imgui/backends/imgui_impl_dx11.h>
#else
#error "Could not find imgui DX11 backend header (imgui_impl_dx11.h)."
#endif

// JSON (vcpkg: nlohmann-json)
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

// Link system libs (works even if CMake forgets to add them for this tool target).
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

using Microsoft::WRL::ComPtr;

namespace
{
	// -------------------------------
	// Minimal Win32 helpers
	// -------------------------------
	void HardenDllSearch()
	{
		// Windows 8+ / KB2533623: restrict DLL search path to safe locations.
		// If not present, call fails; we fail-open.
		using SetDefaultDllDirectories_t = BOOL(WINAPI*)(DWORD);
		if (HMODULE k32 = ::GetModuleHandleW(L"kernel32.dll"))
		{
			auto pSetDefaultDllDirectories = reinterpret_cast<SetDefaultDllDirectories_t>(
				::GetProcAddress(k32, "SetDefaultDllDirectories"));
			if (pSetDefaultDllDirectories)
			{
				// 0x00001000 == LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
				(void)pSetDefaultDllDirectories(0x00001000);
			}
		}
	}

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
#endif

	void ApplyDpiAwareness()
	{
		// Prefer Per-Monitor V2 (Win10+). Fallback: system DPI aware (Vista+).
		if (HMODULE user32 = ::GetModuleHandleW(L"user32.dll"))
		{
			using SetProcessDpiAwarenessContext_t = BOOL(WINAPI*)(HANDLE);
			auto pSetProcessDpiAwarenessContext = reinterpret_cast<SetProcessDpiAwarenessContext_t>(
				::GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
			if (pSetProcessDpiAwarenessContext)
			{
				if (pSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
					return;
			}
		}
		::SetProcessDPIAware();
	}

	void DebugOut(std::wstring_view s)
	{
		::OutputDebugStringW(std::wstring(s).append(L"\n").c_str());
	}

	std::wstring ToWStringFromUtf8(std::string_view s)
	{
		if (s.empty())
			return {};
		const int len = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
		if (len <= 0)
			return {};
		std::wstring out(static_cast<size_t>(len), L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
		return out;
	}

	std::string ToUtf8FromWString(std::wstring_view ws)
	{
		if (ws.empty())
			return {};
		const int len = ::WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
		if (len <= 0)
			return {};
		std::string out(static_cast<size_t>(len), '\0');
		::WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), out.data(), len, nullptr, nullptr);
		return out;
	}

	// -------------------------------
	// Map data model
	// -------------------------------
	struct MapData
	{
		int width = 0;
		int height = 0;
		std::vector<uint8_t> values; // width*height, [0..255]
	};

	static MapData GenerateTestMap(int w, int h, uint32_t seed)
	{
		MapData m;
		m.width = std::max(1, w);
		m.height = std::max(1, h);
		m.values.resize(static_cast<size_t>(m.width) * static_cast<size_t>(m.height));

		std::mt19937 rng(seed);
		std::uniform_real_distribution<float> noise(0.0f, 1.0f);

		// Simple procedural: smooth-ish height field using sin/cos + some noise
		for (int y = 0; y < m.height; ++y)
		{
			for (int x = 0; x < m.width; ++x)
			{
				const float fx = static_cast<float>(x) / static_cast<float>(m.width);
				const float fy = static_cast<float>(y) / static_cast<float>(m.height);

				float v = 0.0f;
				v += 0.55f * (0.5f + 0.5f * std::sin(fx * 10.0f));
				v += 0.35f * (0.5f + 0.5f * std::cos(fy * 12.0f));
				v += 0.20f * noise(rng);

				v = std::clamp(v / 1.10f, 0.0f, 1.0f);
				const uint8_t b = static_cast<uint8_t>(std::lround(v * 255.0f));
				m.values[static_cast<size_t>(y) * static_cast<size_t>(m.width) + static_cast<size_t>(x)] = b;
			}
		}
		return m;
	}

	static bool LoadMapFromJson(const std::filesystem::path& filePath, MapData& outMap, std::wstring& outError)
	{
		outError.clear();

		std::ifstream in(filePath, std::ios::binary);
		if (!in)
		{
			outError = L"Failed to open file.";
			return false;
		}

		nlohmann::json j;
		try
		{
			in >> j;
		}
		catch (const std::exception& e)
		{
			outError = L"JSON parse error: " + ToWStringFromUtf8(e.what());
			return false;
		}

		auto clampByte = [](int v) -> uint8_t
		{
			return static_cast<uint8_t>(std::clamp(v, 0, 255));
		};

		// Accept formats:
		// 1) { "width": W, "height": H, "data": [..] } (flat array length W*H)
		// 2) { "data": [[..],[..],..] } (2D array; width inferred)
		// 3) [[..],[..],..] (2D array)
		int w = 0;
		int h = 0;

		nlohmann::json data;

		if (j.is_object())
		{
			w = j.value("width", 0);
			h = j.value("height", 0);

			if (j.contains("data"))
				data = j["data"];
			else if (j.contains("tiles"))
				data = j["tiles"];
			else if (j.contains("cells"))
				data = j["cells"];
			else if (j.contains("heightmap"))
				data = j["heightmap"];
			else
			{
				outError = L"JSON object must contain one of: data/tiles/cells/heightmap.";
				return false;
			}
		}
		else
		{
			data = j;
		}

		if (!data.is_array())
		{
			outError = L"Map data must be an array (flat or 2D).";
			return false;
		}

		// 2D array form
		if (!data.empty() && data[0].is_array())
		{
			h = static_cast<int>(data.size());
			w = static_cast<int>(data[0].size());

			if (w <= 0 || h <= 0)
			{
				outError = L"2D map array has invalid dimensions.";
				return false;
			}

			outMap.width = w;
			outMap.height = h;
			outMap.values.assign(static_cast<size_t>(w) * static_cast<size_t>(h), 0);

			for (int y = 0; y < h; ++y)
			{
				if (!data[y].is_array())
				{
					outError = L"2D map array rows must all be arrays.";
					return false;
				}
				if (static_cast<int>(data[y].size()) != w)
				{
					outError = L"2D map array rows must all be the same width.";
					return false;
				}

				for (int x = 0; x < w; ++x)
				{
					const int v = data[y][x].get<int>();
					outMap.values[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = clampByte(v);
				}
			}
			return true;
		}

		// Flat array form
		if (w <= 0 || h <= 0)
		{
			outError = L"Flat map array requires width and height fields.";
			return false;
		}

		const auto expected = static_cast<size_t>(w) * static_cast<size_t>(h);
		if (data.size() != expected)
		{
			outError = L"Flat map array size does not match width*height.";
			return false;
		}

		outMap.width = w;
		outMap.height = h;
		outMap.values.assign(expected, 0);

		for (size_t i = 0; i < expected; ++i)
		{
			const int v = data[i].get<int>();
			outMap.values[i] = clampByte(v);
		}

		return true;
	}

	static void BuildRgbaPixels(const MapData& map, bool colorize, std::vector<uint8_t>& outRGBA)
	{
		outRGBA.clear();
		if (map.width <= 0 || map.height <= 0)
			return;

		outRGBA.resize(static_cast<size_t>(map.width) * static_cast<size_t>(map.height) * 4u);

		for (int y = 0; y < map.height; ++y)
		{
			for (int x = 0; x < map.width; ++x)
			{
				const uint8_t v = map.values[static_cast<size_t>(y) * static_cast<size_t>(map.width) + static_cast<size_t>(x)];

				uint8_t r = v, g = v, b = v;
				if (colorize)
				{
					// Simple palette:
					// 0..84   : water
					// 85..169 : grass
					// 170..255: rock/snow
					if (v < 85)
					{
						r = static_cast<uint8_t>(20);
						g = static_cast<uint8_t>(90);
						b = static_cast<uint8_t>(170);
					}
					else if (v < 170)
					{
						r = static_cast<uint8_t>(40);
						g = static_cast<uint8_t>(160);
						b = static_cast<uint8_t>(60);
					}
					else
					{
						r = static_cast<uint8_t>(190);
						g = static_cast<uint8_t>(190);
						b = static_cast<uint8_t>(190);
					}
				}

				const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(map.width) + static_cast<size_t>(x)) * 4u;
				outRGBA[idx + 0] = r;
				outRGBA[idx + 1] = g;
				outRGBA[idx + 2] = b;
				outRGBA[idx + 3] = 255;
			}
		}
	}

	// -------------------------------
	// D3D11 + ImGui state
	// -------------------------------
	ComPtr<ID3D11Device> g_pd3dDevice;
	ComPtr<ID3D11DeviceContext> g_pd3dDeviceContext;
	ComPtr<IDXGISwapChain1> g_pSwapChain;
	ComPtr<ID3D11RenderTargetView> g_mainRenderTargetView;

	ComPtr<ID3D11Texture2D> g_mapTexture;
	ComPtr<ID3D11ShaderResourceView> g_mapSRV;

	bool g_allowTearing = false;
	HWND g_hwnd = nullptr;

	std::optional<std::filesystem::path> g_pendingOpenPath; // from drag/drop

	static void CleanupRenderTarget()
	{
		if (g_mainRenderTargetView)
			g_mainRenderTargetView.Reset();
	}

	static void CreateRenderTarget()
	{
		CleanupRenderTarget();

		ComPtr<ID3D11Texture2D> backBuffer;
		if (FAILED(g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()))))
			return;

		(void)g_pd3dDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, g_mainRenderTargetView.GetAddressOf());
	}

	static bool CheckTearingSupport(IDXGIFactory1* factory1)
	{
		if (!factory1)
			return false;

		ComPtr<IDXGIFactory5> factory5;
		if (FAILED(factory1->QueryInterface(IID_PPV_ARGS(factory5.GetAddressOf()))))
			return false;

		BOOL allow = FALSE;
		if (FAILED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow))))
			return false;

		return allow == TRUE;
	}

	static bool CreateDeviceD3D(HWND hWnd)
	{
		UINT createDeviceFlags = 0;
#if defined(_DEBUG)
		createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

		const D3D_FEATURE_LEVEL featureLevelArray[] = {
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_0,
		};

		D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;

		HRESULT hr = D3D11CreateDevice(
			nullptr,
			D3D_DRIVER_TYPE_HARDWARE,
			nullptr,
			createDeviceFlags,
			featureLevelArray,
			static_cast<UINT>(std::size(featureLevelArray)),
			D3D11_SDK_VERSION,
			g_pd3dDevice.GetAddressOf(),
			&featureLevel,
			g_pd3dDeviceContext.GetAddressOf());

		if (FAILED(hr))
		{
			// Fallback to WARP if hardware fails
			hr = D3D11CreateDevice(
				nullptr,
				D3D_DRIVER_TYPE_WARP,
				nullptr,
				createDeviceFlags,
				featureLevelArray,
				static_cast<UINT>(std::size(featureLevelArray)),
				D3D11_SDK_VERSION,
				g_pd3dDevice.GetAddressOf(),
				&featureLevel,
				g_pd3dDeviceContext.GetAddressOf());
			if (FAILED(hr))
				return false;
		}

		// Create swapchain using the device's DXGI factory
		ComPtr<IDXGIDevice> dxgiDevice;
		if (FAILED(g_pd3dDevice->QueryInterface(IID_PPV_ARGS(dxgiDevice.GetAddressOf()))))
			return false;

		ComPtr<IDXGIAdapter> adapter;
		if (FAILED(dxgiDevice->GetAdapter(adapter.GetAddressOf())))
			return false;

		ComPtr<IDXGIFactory1> factory1;
		if (FAILED(adapter->GetParent(IID_PPV_ARGS(factory1.GetAddressOf()))))
			return false;

		g_allowTearing = CheckTearingSupport(factory1.Get());

		ComPtr<IDXGIFactory2> factory2;
		if (FAILED(factory1.As(&factory2)))
			return false;

		(void)factory2->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);

		DXGI_SWAP_CHAIN_DESC1 sd{};
		sd.Width = 0;  // use window size
		sd.Height = 0; // use window size
		sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.BufferCount = 2;
		sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		sd.Scaling = DXGI_SCALING_STRETCH;
		sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
		sd.Flags = g_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

		ComPtr<IDXGISwapChain1> swapchain1;
		hr = factory2->CreateSwapChainForHwnd(
			g_pd3dDevice.Get(),
			hWnd,
			&sd,
			nullptr,
			nullptr,
			swapchain1.GetAddressOf());

		if (FAILED(hr))
			return false;

		g_pSwapChain = swapchain1;
		CreateRenderTarget();
		return true;
	}

	static void CleanupDeviceD3D()
	{
		g_mapSRV.Reset();
		g_mapTexture.Reset();

		CleanupRenderTarget();
		if (g_pSwapChain)
			g_pSwapChain.Reset();
		if (g_pd3dDeviceContext)
			g_pd3dDeviceContext.Reset();
		if (g_pd3dDevice)
			g_pd3dDevice.Reset();
	}

	static void RebuildMapTexture(const MapData& map, bool colorize)
	{
		g_mapSRV.Reset();
		g_mapTexture.Reset();

		if (!g_pd3dDevice || !g_pd3dDeviceContext)
			return;

		if (map.width <= 0 || map.height <= 0)
			return;

		std::vector<uint8_t> rgba;
		BuildRgbaPixels(map, colorize, rgba);

		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = static_cast<UINT>(map.width);
		desc.Height = static_cast<UINT>(map.height);
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;

		D3D11_SUBRESOURCE_DATA init{};
		init.pSysMem = rgba.data();
		init.SysMemPitch = static_cast<UINT>(map.width) * 4u;

		ComPtr<ID3D11Texture2D> tex;
		if (FAILED(g_pd3dDevice->CreateTexture2D(&desc, &init, tex.GetAddressOf())))
			return;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;

		ComPtr<ID3D11ShaderResourceView> srv;
		if (FAILED(g_pd3dDevice->CreateShaderResourceView(tex.Get(), &srvDesc, srv.GetAddressOf())))
			return;

		g_mapTexture = tex;
		g_mapSRV = srv;
	}

	// -------------------------------
	// Win32 window proc
	// -------------------------------
	extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

	static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
			return 1;

		switch (msg)
		{
		case WM_SIZE:
			if (g_pd3dDevice && g_pSwapChain)
			{
				if (wParam != SIZE_MINIMIZED)
				{
					CleanupRenderTarget();
					const UINT w = LOWORD(lParam);
					const UINT h = HIWORD(lParam);
					const UINT flags = g_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
					(void)g_pSwapChain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, flags);
					CreateRenderTarget();
				}
			}
			return 0;

		case WM_SYSCOMMAND:
			// Disable ALT application menu
			if ((wParam & 0xfff0u) == SC_KEYMENU)
				return 0;
			break;

		case WM_DROPFILES:
		{
			const HDROP hDrop = reinterpret_cast<HDROP>(wParam);
			wchar_t path[MAX_PATH]{};
			if (DragQueryFileW(hDrop, 0, path, static_cast<UINT>(std::size(path))) > 0)
			{
				g_pendingOpenPath = std::filesystem::path(path);
			}
			DragFinish(hDrop);
			return 0;
		}

		case WM_DESTROY:
			::PostQuitMessage(0);
			return 0;

		default:
			break;
		}

		return ::DefWindowProcW(hWnd, msg, wParam, lParam);
	}

	// -------------------------------
	// UI helpers
	// -------------------------------
	static std::optional<std::filesystem::path> OpenMapFileDialog(HWND owner)
	{
		wchar_t fileBuf[MAX_PATH]{};

		OPENFILENAMEW ofn{};
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = owner;
		ofn.lpstrFile = fileBuf;
		ofn.nMaxFile = static_cast<DWORD>(std::size(fileBuf));

		// NOTE: filter string is pairs of display name and pattern, separated by '\0', terminated by '\0\0'
		ofn.lpstrFilter = L"JSON map (*.json)\0*.json\0All files (*.*)\0*.*\0\0";
		ofn.nFilterIndex = 1;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;

		if (::GetOpenFileNameW(&ofn) == TRUE)
			return std::filesystem::path(fileBuf);

		return std::nullopt;
	}

	struct UiState
	{
		bool vsync = true;
		bool showGrid = true;
		bool colorize = true;
		float zoom = 6.0f;

		std::filesystem::path currentFile;
		std::wstring lastError;

		MapData map = GenerateTestMap(256, 256, 1337u);

		int selX = -1;
		int selY = -1;

		int genW = 256;
		int genH = 256;
		int genSeed = 1337;
	};

	static void DrawMapWindow(UiState& st)
	{
		ImGui::Begin("Map");

		// Controls row
		ImGui::Checkbox("VSync", &st.vsync);
		ImGui::SameLine();
		ImGui::Checkbox("Grid", &st.showGrid);
		ImGui::SameLine();
		ImGui::Checkbox("Colorize", &st.colorize);

		ImGui::SliderFloat("Zoom", &st.zoom, 1.0f, 64.0f, "%.1fx", ImGuiSliderFlags_Logarithmic);

		if (!st.currentFile.empty())
			ImGui::Text("File: %s", ToUtf8FromWString(st.currentFile.wstring()).c_str());
		ImGui::Text("Map: %d x %d", st.map.width, st.map.height);

		if (!st.lastError.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 80, 80, 255));
			ImGui::TextWrapped("%s", ToUtf8FromWString(st.lastError).c_str());
			ImGui::PopStyleColor();
		}

		ImGui::Separator();

		// Generation controls
		if (ImGui::CollapsingHeader("Generate", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::InputInt("Width", &st.genW);
			ImGui::InputInt("Height", &st.genH);
			ImGui::InputInt("Seed", &st.genSeed);

			st.genW = std::clamp(st.genW, 1, 4096);
			st.genH = std::clamp(st.genH, 1, 4096);

			if (ImGui::Button("Generate test map"))
			{
				st.map = GenerateTestMap(st.genW, st.genH, static_cast<uint32_t>(st.genSeed));
				RebuildMapTexture(st.map, st.colorize);
				st.currentFile.clear();
				st.lastError.clear();
				st.selX = st.selY = -1;
			}
		}

		ImGui::Separator();

		// Pan via scrollbars; zoom scales the image size.
		ImGui::TextUnformatted("Tip: Use the scrollbars to pan. Hover + mouse wheel to zoom.");
		ImGui::BeginChild("MapCanvas", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

		// Zoom with mouse wheel when hovering this child.
		if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
		{
			const float wheel = ImGui::GetIO().MouseWheel;
			if (wheel != 0.0f)
			{
				// Zoom multiplier, clamp
				const float factor = std::pow(1.12f, wheel);
				st.zoom = std::clamp(st.zoom * factor, 1.0f, 64.0f);
			}
		}

		const ImVec2 imageSize(
			static_cast<float>(st.map.width) * st.zoom,
			static_cast<float>(st.map.height) * st.zoom);

		if (g_mapSRV)
		{
			ImGui::Image(reinterpret_cast<ImTextureID>(g_mapSRV.Get()), imageSize);

			const ImVec2 imgMin = ImGui::GetItemRectMin();
			const ImVec2 imgMax = ImGui::GetItemRectMax();

			ImDrawList* dl = ImGui::GetWindowDrawList();

			// Grid overlay (only when zoomed in enough)
			if (st.showGrid && st.zoom >= 8.0f)
			{
				const int maxLines = 2048; // sanity cap
				const int wLines = std::min(st.map.width + 1, maxLines);
				const int hLines = std::min(st.map.height + 1, maxLines);

				const ImU32 gridCol = IM_COL32(0, 0, 0, 70);

				for (int x = 0; x < wLines; ++x)
				{
					const float xx = imgMin.x + static_cast<float>(x) * st.zoom;
					dl->AddLine(ImVec2(xx, imgMin.y), ImVec2(xx, imgMax.y), gridCol, 1.0f);
				}
				for (int y = 0; y < hLines; ++y)
				{
					const float yy = imgMin.y + static_cast<float>(y) * st.zoom;
					dl->AddLine(ImVec2(imgMin.x, yy), ImVec2(imgMax.x, yy), gridCol, 1.0f);
				}
			}

			// Hover + pick tile
			if (ImGui::IsItemHovered())
			{
				const ImVec2 mouse = ImGui::GetIO().MousePos;
				const float localX = mouse.x - imgMin.x;
				const float localY = mouse.y - imgMin.y;

				const int tx = static_cast<int>(std::floor(localX / st.zoom));
				const int ty = static_cast<int>(std::floor(localY / st.zoom));

				if (tx >= 0 && ty >= 0 && tx < st.map.width && ty < st.map.height)
				{
					const uint8_t v = st.map.values[static_cast<size_t>(ty) * static_cast<size_t>(st.map.width) + static_cast<size_t>(tx)];

					ImGui::BeginTooltip();
					ImGui::Text("Tile: (%d, %d)", tx, ty);
					ImGui::Text("Value: %u", static_cast<unsigned>(v));
					ImGui::EndTooltip();

					// highlight hovered tile
					const ImU32 hoverCol = IM_COL32(255, 255, 255, 90);
					const ImVec2 a(imgMin.x + tx * st.zoom, imgMin.y + ty * st.zoom);
					const ImVec2 b(a.x + st.zoom, a.y + st.zoom);
					dl->AddRect(a, b, hoverCol, 0.0f, 0, 2.0f);

					// select on click
					if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						st.selX = tx;
						st.selY = ty;
					}
				}
			}

			// selection highlight
			if (st.selX >= 0 && st.selY >= 0 && st.selX < st.map.width && st.selY < st.map.height)
			{
				const ImU32 selCol = IM_COL32(255, 200, 40, 200);
				const ImVec2 a(imgMin.x + st.selX * st.zoom, imgMin.y + st.selY * st.zoom);
				const ImVec2 b(a.x + st.zoom, a.y + st.zoom);
				dl->AddRect(a, b, selCol, 0.0f, 0, 3.0f);

				const uint8_t v = st.map.values[static_cast<size_t>(st.selY) * static_cast<size_t>(st.map.width) + static_cast<size_t>(st.selX)];
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);
				ImGui::Text("Selected: (%d, %d) value=%u", st.selX, st.selY, static_cast<unsigned>(v));
			}
		}
		else
		{
			ImGui::TextUnformatted("No map texture (device not ready?).");
		}

		ImGui::EndChild();
		ImGui::End();
	}

	// -------------------------------
	// App bootstrap / main loop
	// -------------------------------
	static int RunMapViewer(const std::vector<std::wstring>& args)
	{
		HardenDllSearch();
		ApplyDpiAwareness();

		// Parse optional file argument (first non-flag argument after exe)
		std::optional<std::filesystem::path> initialMapPath;
		for (size_t i = 1; i < args.size(); ++i)
		{
			const std::wstring& a = args[i];
			if (!a.empty() && a[0] == L'-')
				continue;

			std::filesystem::path p(a);
			if (std::filesystem::exists(p))
			{
				initialMapPath = p;
				break;
			}
		}

		// Create Win32 window
		const wchar_t* kClassName = L"ColonyMapViewerWindowClass";

		WNDCLASSEXW wc{};
		wc.cbSize = sizeof(wc);
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = WndProc;
		wc.hInstance = ::GetModuleHandleW(nullptr);
		wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
		wc.lpszClassName = kClassName;

		if (!::RegisterClassExW(&wc))
		{
			::MessageBoxW(nullptr, L"RegisterClassExW failed.", L"Colony Map Viewer", MB_OK | MB_ICONERROR);
			return 1;
		}

		g_hwnd = ::CreateWindowExW(
			0,
			kClassName,
			L"Colony Map Viewer",
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			1280,
			720,
			nullptr,
			nullptr,
			wc.hInstance,
			nullptr);

		if (!g_hwnd)
		{
			::MessageBoxW(nullptr, L"CreateWindowExW failed.", L"Colony Map Viewer", MB_OK | MB_ICONERROR);
			::UnregisterClassW(kClassName, wc.hInstance);
			return 1;
		}

		::DragAcceptFiles(g_hwnd, TRUE);

		if (!CreateDeviceD3D(g_hwnd))
		{
			CleanupDeviceD3D();
			::DestroyWindow(g_hwnd);
			::UnregisterClassW(kClassName, wc.hInstance);
			::MessageBoxW(nullptr, L"Failed to create D3D11 device/swapchain.", L"Colony Map Viewer", MB_OK | MB_ICONERROR);
			return 1;
		}

		::ShowWindow(g_hwnd, SW_SHOWDEFAULT);
		::UpdateWindow(g_hwnd);

		// Setup Dear ImGui
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

		ImGui::StyleColorsDark();

		ImGui_ImplWin32_Init(g_hwnd);
		ImGui_ImplDX11_Init(g_pd3dDevice.Get(), g_pd3dDeviceContext.Get());

		UiState st;
		RebuildMapTexture(st.map, st.colorize);

		// Load initial file if provided
		if (initialMapPath)
		{
			MapData loaded;
			std::wstring err;
			if (LoadMapFromJson(*initialMapPath, loaded, err))
			{
				st.map = std::move(loaded);
				st.currentFile = *initialMapPath;
				st.lastError.clear();
				RebuildMapTexture(st.map, st.colorize);
			}
			else
			{
				st.lastError = L"Failed to load initial map: " + err;
			}
		}

		bool done = false;
		while (!done)
		{
			// Pump messages
			MSG msg{};
			while (::PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE))
			{
				::TranslateMessage(&msg);
				::DispatchMessageW(&msg);
				if (msg.message == WM_QUIT)
					done = true;
			}
			if (done)
				break;

			// If we got a drag-drop path, load it
			if (g_pendingOpenPath)
			{
				MapData loaded;
				std::wstring err;
				if (LoadMapFromJson(*g_pendingOpenPath, loaded, err))
				{
					st.map = std::move(loaded);
					st.currentFile = *g_pendingOpenPath;
					st.lastError.clear();
					RebuildMapTexture(st.map, st.colorize);
					st.selX = st.selY = -1;
				}
				else
				{
					st.lastError = L"Failed to load dropped map: " + err;
				}
				g_pendingOpenPath.reset();
			}

			// Start ImGui frame
			ImGui_ImplDX11_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();

			// Dockspace
			ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());

			// Menu bar
			if (ImGui::BeginMainMenuBar())
			{
				if (ImGui::BeginMenu("File"))
				{
					if (ImGui::MenuItem("Open..."))
					{
						if (auto p = OpenMapFileDialog(g_hwnd))
						{
							MapData loaded;
							std::wstring err;
							if (LoadMapFromJson(*p, loaded, err))
							{
								st.map = std::move(loaded);
								st.currentFile = *p;
								st.lastError.clear();
								RebuildMapTexture(st.map, st.colorize);
								st.selX = st.selY = -1;
							}
							else
							{
								st.lastError = L"Failed to load map: " + err;
							}
						}
					}

					if (ImGui::MenuItem("Reload", nullptr, false, !st.currentFile.empty()))
					{
						MapData loaded;
						std::wstring err;
						if (LoadMapFromJson(st.currentFile, loaded, err))
						{
							st.map = std::move(loaded);
							st.lastError.clear();
							RebuildMapTexture(st.map, st.colorize);
							st.selX = st.selY = -1;
						}
						else
						{
							st.lastError = L"Failed to reload map: " + err;
						}
					}

					ImGui::Separator();

					if (ImGui::MenuItem("Exit"))
						done = true;

					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu("View"))
				{
					ImGui::MenuItem("VSync", nullptr, &st.vsync);
					ImGui::MenuItem("Grid", nullptr, &st.showGrid);
					ImGui::MenuItem("Colorize", nullptr, &st.colorize);
					ImGui::EndMenu();
				}

				ImGui::EndMainMenuBar();
			}

			// If colorize changed, rebuild texture (cheap enough for a toggle).
			static bool s_lastColorize = st.colorize;
			if (s_lastColorize != st.colorize)
			{
				s_lastColorize = st.colorize;
				RebuildMapTexture(st.map, st.colorize);
			}

			DrawMapWindow(st);

			// Render
			ImGui::Render();

			const float clear[4] = {0.08f, 0.10f, 0.12f, 1.0f};
			g_pd3dDeviceContext->OMSetRenderTargets(1, g_mainRenderTargetView.GetAddressOf(), nullptr);
			g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView.Get(), clear);

			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

			// Present
			UINT presentFlags = 0;
			if (!st.vsync && g_allowTearing)
				presentFlags |= DXGI_PRESENT_ALLOW_TEARING;

			(void)g_pSwapChain->Present(st.vsync ? 1 : 0, presentFlags);
		}

		// Cleanup
		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();

		CleanupDeviceD3D();
		::DestroyWindow(g_hwnd);
		::UnregisterClassW(kClassName, wc.hInstance);

		return 0;
	}

	static std::vector<std::wstring> GetCommandLineArgsW()
	{
		int argc = 0;
		LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
		std::vector<std::wstring> args;
		if (argv && argc > 0)
		{
			args.reserve(static_cast<size_t>(argc));
			for (int i = 0; i < argc; ++i)
				args.emplace_back(argv[i] ? argv[i] : L"");
		}
		if (argv)
			::LocalFree(argv);
		return args;
	}
} // namespace

// Win32 GUI subsystem entry point (if the target is built as WIN32_EXECUTABLE)
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	try
	{
		return RunMapViewer(GetCommandLineArgsW());
	}
	catch (const std::exception& e)
	{
		const std::wstring msg = L"Unhandled exception: " + ToWStringFromUtf8(e.what());
		::MessageBoxW(nullptr, msg.c_str(), L"Colony Map Viewer", MB_OK | MB_ICONERROR);
		return 1;
	}
}

// Console subsystem entry point (if the target is built as a console app)
int main(int, char**)
{
	try
	{
		return RunMapViewer(GetCommandLineArgsW());
	}
	catch (const std::exception& e)
	{
		const std::wstring msg = L"Unhandled exception: " + ToWStringFromUtf8(e.what());
		::MessageBoxW(nullptr, msg.c_str(), L"Colony Map Viewer", MB_OK | MB_ICONERROR);
		return 1;
	}
}
