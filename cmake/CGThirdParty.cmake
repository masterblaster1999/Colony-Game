# cmake/CGThirdParty.cmake
include_guard(GLOBAL)

set(COLONY_THIRDPARTY_LIBS         "" CACHE INTERNAL "")
set(COLONY_THIRDPARTY_INCLUDE_DIRS "" CACHE INTERNAL "")

# (Optional) DXC runtime library if you use the DXC C++ API at runtime.
find_package(directx-dxc CONFIG QUIET)            # vcpkg port: directx-dxc
if(TARGET Microsoft::DirectXShaderCompiler)
  list(APPEND COLONY_THIRDPARTY_LIBS Microsoft::DirectXShaderCompiler)
endif()

# SDL2 (optional, only when requested)
if(FRONTEND STREQUAL "sdl")
  find_package(SDL2 CONFIG REQUIRED)
  add_compile_definitions(COLONY_WITH_SDL2=1 COLONY_WITH_SDL=1 SDL_MAIN_HANDLED=1)
  if(TARGET SDL2::SDL2main)
    list(APPEND COLONY_THIRDPARTY_LIBS SDL2::SDL2 SDL2::SDL2main)
  else()
    list(APPEND COLONY_THIRDPARTY_LIBS SDL2::SDL2)
  endif()
endif()

# ImGui (optional)
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

# Tracy (optional)
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
      target_include_directories(tracy_client PUBLIC "${tracy_SOURCE_DIR}/public" "${tracy_SOURCE_DIR}/public/tracy")
      target_compile_definitions(tracy_client PUBLIC TRACY_ENABLE=1)
    endif()
    list(APPEND COLONY_THIRDPARTY_LIBS tracy_client)
    list(APPEND COLONY_THIRDPARTY_INCLUDE_DIRS "${tracy_SOURCE_DIR}/public" "${tracy_SOURCE_DIR}/public/tracy")
  endif()
endif()

if(COLONY_THIRDPARTY_INCLUDE_DIRS)
  include_directories(${COLONY_THIRDPARTY_INCLUDE_DIRS})
endif()
