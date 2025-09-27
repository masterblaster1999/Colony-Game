# Find vcpkg-provided packages (Windows only here)
if(WIN32)
  find_package(fmt CONFIG REQUIRED)
  find_package(spdlog CONFIG REQUIRED)
  find_package(EnTT CONFIG REQUIRED)
  find_package(directx-headers CONFIG REQUIRED)
  find_package(directxtex CONFIG REQUIRED)    # Microsoft::DirectXTex
  find_package(directx-dxc CONFIG REQUIRED)   # Microsoft::DirectXShaderCompiler
  find_package(winpixevent CONFIG QUIET)      # Microsoft::WinPixEventRuntime (optional)
  find_package(Taskflow CONFIG QUIET)         # Taskflow::Taskflow (header-only)
  if(ENABLE_IMGUI)
    find_package(imgui CONFIG REQUIRED)       # imgui::imgui (static lib in vcpkg)
  endif()
endif()

# Group Windows system libs you normally link against
set(colony_win32_libs d3d11 dxgi d3dcompiler)
if(winpixevent_FOUND)
  list(APPEND colony_win32_libs Microsoft::WinPixEventRuntime)
endif()

# Tracy client (either via FetchContent or vcpkg CONFIG)
if(ENABLE_TRACY)
  if(TRACY_FETCH)
    include(FetchContent)
    set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
    FetchContent_Declare(tracy
      GIT_REPOSITORY https://github.com/wolfpld/tracy.git
      GIT_TAG        v0.12.2
      GIT_SHALLOW    TRUE
    )
    FetchContent_GetProperties(tracy)
    if(NOT tracy_POPULATED)
      FetchContent_Populate(tracy)
    endif()
    if(EXISTS "${tracy_SOURCE_DIR}/public/TracyClient.cpp")
      add_library(tracy_client STATIC "${tracy_SOURCE_DIR}/public/TracyClient.cpp")
      target_include_directories(tracy_client SYSTEM PUBLIC "${tracy_SOURCE_DIR}/public")
      target_compile_definitions(tracy_client PUBLIC TRACY_ENABLE)
      if(WIN32)
        target_link_libraries(tracy_client PUBLIC ws2_32)
      endif()
      set(COLONY_TRACY_LIB tracy_client)
    else()
      message(FATAL_ERROR "Tracy sources missing at ${tracy_SOURCE_DIR}")
    endif()
  else()
    find_package(Tracy CONFIG REQUIRED)
    set(COLONY_TRACY_LIB Tracy::TracyClient)
  endif()
endif()

# Aggregate third-party libs you typically link to the game
set(COLONY_THIRDPARTY_LIBS
  fmt::fmt
  spdlog::spdlog
  EnTT::EnTT
  Microsoft::DirectXTex
  Microsoft::DirectXShaderCompiler
)

if(TARGET Taskflow::Taskflow)
  list(APPEND COLONY_THIRDPARTY_LIBS Taskflow::Taskflow)
endif()
if(ENABLE_IMGUI)
  list(APPEND COLONY_THIRDPARTY_LIBS imgui::imgui)
endif()
if(ENABLE_TRACY)
  list(APPEND COLONY_THIRDPARTY_LIBS ${COLONY_TRACY_LIB})
endif()

# Export for outer CMakeLists
set(colony_win32_libs "${colony_win32_libs}" PARENT_SCOPE)
set(COLONY_THIRDPARTY_LIBS "${COLONY_THIRDPARTY_LIBS}" PARENT_SCOPE)
