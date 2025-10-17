# cmake/CGThirdParty.cmake
include_guard(GLOBAL)

set(COLONY_THIRDPARTY_LIBS         "" CACHE INTERNAL "")
set(COLONY_THIRDPARTY_INCLUDE_DIRS "" CACHE INTERNAL "")

# --------------------------------------------------------------------------------------------------
# DXC runtime (optional) — if you use DirectXShaderCompiler C++ API at runtime.
# --------------------------------------------------------------------------------------------------
find_package(directx-dxc CONFIG QUIET)
if(TARGET Microsoft::DirectXShaderCompiler)
  list(APPEND COLONY_THIRDPARTY_LIBS Microsoft::DirectXShaderCompiler)
endif()

# --------------------------------------------------------------------------------------------------
# Core libraries (required) — these are in your vcpkg.json and used throughout the codebase.
# --------------------------------------------------------------------------------------------------
find_package(fmt CONFIG REQUIRED)
list(APPEND COLONY_THIRDPARTY_LIBS fmt::fmt)

find_package(spdlog CONFIG REQUIRED)
list(APPEND COLONY_THIRDPARTY_LIBS spdlog::spdlog_header_only)

find_package(EnTT CONFIG REQUIRED)
list(APPEND COLONY_THIRDPARTY_LIBS EnTT::EnTT)

find_package(nlohmann_json CONFIG REQUIRED)
list(APPEND COLONY_THIRDPARTY_LIBS nlohmann_json::nlohmann_json)

find_package(directx-headers CONFIG REQUIRED)
list(APPEND COLONY_THIRDPARTY_LIBS Microsoft::DirectX-Guids)

# --------------------------------------------------------------------------------------------------
# SDL2 frontend (optional, only if requested via FRONTEND=sdl)
# --------------------------------------------------------------------------------------------------
if(FRONTEND STREQUAL "sdl")
  find_package(SDL2 CONFIG REQUIRED)
  add_compile_definitions(COLONY_WITH_SDL2=1 COLONY_WITH_SDL=1 SDL_MAIN_HANDLED=1)
  if(TARGET SDL2::SDL2main)
    list(APPEND COLONY_THIRDPARTY_LIBS SDL2::SDL2 SDL2::SDL2main)
  else()
    list(APPEND COLONY_THIRDPARTY_LIBS SDL2::SDL2)
  endif()
endif()

# --------------------------------------------------------------------------------------------------
# ImGui overlay (optional)
# --------------------------------------------------------------------------------------------------
if(ENABLE_IMGUI)
  find_package(imgui CONFIG QUIET)
  if(TARGET imgui::imgui)
    add_compile_definitions(COLONY_WITH_IMGUI=1)
    list(APPEND COLONY_THIRDPARTY_LIBS imgui::imgui)
  elseif(EXISTS "${CMAKE_SOURCE_DIR}/externals/imgui/imgui.cpp")
    add_library(imgui STATIC
      externals/imgui/imgui.cpp
      externals/imgui/imgui_draw.cpp
      externals/imgui/imgui_tables.cpp
      externals/imgui/imgui_widgets.cpp
      externals/imgui/backends/imgui_impl_win32.cpp
      externals/imgui/backends/imgui_impl_dx11.cpp
    )
    target_include_directories(imgui PUBLIC externals/imgui)
    add_compile_definitions(COLONY_WITH_IMGUI=1)
    list(APPEND COLONY_THIRDPARTY_LIBS imgui)
  else()
    message(WARNING "ENABLE_IMGUI=ON but neither imgui::imgui (vcpkg) nor externals/imgui found.")
  endif()
endif()

# --------------------------------------------------------------------------------------------------
# Tracy profiler (optional)
# --------------------------------------------------------------------------------------------------
if(ENABLE_TRACY)
  add_compile_definitions(TRACY_ENABLE=1)
  find_package(Tracy CONFIG QUIET)
  if(TARGET Tracy::TracyClient)
    list(APPEND COLONY_THIRDPARTY_LIBS Tracy::TracyClient)
  elseif(TRACY_FETCH)
    include(FetchContent)
    FetchContent_Declare(tracy
      GIT_REPOSITORY https://github.com/wolfpld/tracy.git
      GIT_TAG v0.11
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(tracy)
    if(NOT TARGET tracy_client)
      add_library(tracy_client STATIC "${tracy_SOURCE_DIR}/public/TracyClient.cpp")
      target_include_directories(tracy_client PUBLIC
        "${tracy_SOURCE_DIR}/public"
        "${tracy_SOURCE_DIR}/public/tracy")
      target_compile_definitions(tracy_client PUBLIC TRACY_ENABLE=1)
    endif()
    list(APPEND COLONY_THIRDPARTY_LIBS tracy_client)
    list(APPEND COLONY_THIRDPARTY_INCLUDE_DIRS
      "${tracy_SOURCE_DIR}/public"
      "${tracy_SOURCE_DIR}/public/tracy")
  endif()
endif()

# --------------------------------------------------------------------------------------------------
# Apply collected include dirs globally (legacy)
# --------------------------------------------------------------------------------------------------
if(COLONY_THIRDPARTY_INCLUDE_DIRS)
  include_directories(${COLONY_THIRDPARTY_INCLUDE_DIRS})
endif()
