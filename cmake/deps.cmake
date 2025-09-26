# cmake/deps.cmake — third-party dependencies via vcpkg

# Optional SDL front-end (not used by default)
if(FRONTEND STREQUAL "sdl")
  find_package(SDL2 CONFIG REQUIRED)
endif()

# Core deps
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)

# DirectX toolchain and texture helpers
find_package(directx-headers CONFIG REQUIRED)          # Microsoft::DirectX-Headers
find_package(directx-dxc     CONFIG REQUIRED)          # Microsoft::DirectXShaderCompiler (+ DIRECTX_DXC_TOOL var)
find_package(directxtex      CONFIG REQUIRED)          # Microsoft::DirectXTex

# Optional UI & profiling
if(ENABLE_IMGUI)
  find_package(imgui CONFIG REQUIRED)                  # imgui::imgui
endif()

if(ENABLE_TRACY)
  find_package(Tracy CONFIG REQUIRED)                  # Tracy::TracyClient
endif()

# Task graph
find_package(Taskflow CONFIG REQUIRED)                 # Taskflow::Taskflow

# PIX events (DX profiling markers)
find_package(winpixevent CONFIG REQUIRED)              # Microsoft::WinPixEventRuntime

# Aggregate a list of libs to link from real, non-ALIAS targets only.
set(COLONY_THIRDPARTY_LIBS
  fmt::fmt
  spdlog::spdlog
  EnTT::EnTT
  Microsoft::DirectX-Headers
  Microsoft::DirectXShaderCompiler
  Microsoft::DirectXTex
  Taskflow::Taskflow
  Microsoft::WinPixEventRuntime
)

if(ENABLE_IMGUI)
  list(APPEND COLONY_THIRDPARTY_LIBS imgui::imgui)
endif()

if(ENABLE_TRACY)
  list(APPEND COLONY_THIRDPARTY_LIBS Tracy::TracyClient)
endif()

# Win32 base system libraries (as an INTERFACE imported "bundle" for convenience)
if(MSVC AND NOT TARGET colony_win32_libs)
  add_library(colony_win32_libs INTERFACE)
  target_link_libraries(colony_win32_libs INTERFACE d3d11 dxgi d3dcompiler)
endif()
