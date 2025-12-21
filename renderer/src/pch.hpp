// renderer/src/pch.hpp
//
// Precompiled header for the renderer module.
// Keep this stable: avoid including frequently-changing project headers.

#pragma once

// Keep Windows headers lean & prevent std::min/max macro clashes.
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
  #define NOMINMAX
#endif

#include <Windows.h>

// ---- STL (commonly used, stable) ----
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

// ---- DirectX / Windows graphics headers ----
// (Your renderer obviously depends on the Windows SDK, so these should exist
// in your Windows toolchain. If they don't, you'd fail later anyway.)
#include <dxgi1_6.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <DirectXMath.h>

// Optional: DXC API (Windows SDK usually provides this on modern setups)
#if defined(__has_include)
  #if __has_include(<dxcapi.h>)
    #include <dxcapi.h>
  #endif
#endif
