# cmake/deps.cmake — third-party dependencies via vcpkg

if(FRONTEND STREQUAL "sdl")
  find_package(SDL2 CONFIG REQUIRED)
endif()

find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)

find_package(directx-headers CONFIG REQUIRED)          # Microsoft::DirectX-Headers
find_package(directx-dxc     CONFIG REQUIRED)          # Microsoft::DirectXShaderCompiler
find_package(directxtex      CONFIG REQUIRED)          # Microsoft::DirectXTex

if(ENABLE_IMGUI)
  find_package(imgui CONFIG REQUIRED)                  # imgui::imgui
endif()

if(ENABLE_TRACY)
  find_package(Tracy CONFIG REQUIRED)                  # Tracy::TracyClient
endif()

find_package(Taskflow CONFIG REQUIRED)                 # Taskflow::Taskflow
find_package(winpixevent CONFIG REQUIRED)              # Microsoft::WinPixEventRuntime

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

if(MSVC AND NOT TARGET colony_win32_libs)
  add_library(colony_win32_libs INTERFACE)
  target_link_libraries(colony_win32_libs INTERFACE d3d11 dxgi d3dcompiler)
endif()
