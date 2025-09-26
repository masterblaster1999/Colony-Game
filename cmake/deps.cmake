# cmake/deps.cmake
include_guard(GLOBAL)

if(NOT WIN32)
  message(FATAL_ERROR "Colony-Game is Windows-only.")
endif()

# ---------------------------
# Optional frontend: SDL2
# ---------------------------
if(FRONTEND STREQUAL "sdl")
  # SDL2 via vcpkg ("sdl2" port)
  find_package(SDL2 CONFIG REQUIRED)
endif()

# ---------------------------
# Core third-party via vcpkg
# ---------------------------
find_package(directx-headers CONFIG REQUIRED)       # Microsoft::DirectX-Headers
find_package(directxtex      CONFIG REQUIRED)       # Microsoft::DirectXTex
find_package(directx-dxc     CONFIG REQUIRED)       # Microsoft::DirectXShaderCompiler (+ DIRECTX_DXC_TOOL var)
find_package(winpixevent     CONFIG QUIET)          # Microsoft::WinPixEventRuntime (optional)
find_package(EnTT            CONFIG REQUIRED)       # EnTT::EnTT
find_package(fmt             CONFIG REQUIRED)       # fmt::fmt
find_package(spdlog          CONFIG REQUIRED)       # spdlog::spdlog
find_package(Taskflow        CONFIG QUIET)          # Taskflow::Taskflow (optional)

# ---------------------------
# ImGui: use vcpkg target
# ---------------------------
# IMPORTANT: vcpkg features should be enabled in vcpkg.json:
#   * Win32 + D3D11: [core, docking-experimental, win32-binding, dx11-binding]
#   * SDL2  + D3D11: [core, docking-experimental, sdl2-binding, dx11-binding]
if(ENABLE_IMGUI)
  find_package(imgui CONFIG REQUIRED)  # provides imgui::imgui
  # Make a friendly wrapper target named "imgui" that you can link conditionally.
  # This is an INTERFACE lib so we can safely modify it; it's NOT an ALIAS.
  if(TARGET imgui)
    # If a conflicting target named 'imgui' already exists, force the user to remove it.
    message(FATAL_ERROR "Target 'imgui' already exists. Remove any vendored/legacy ImGui target.")
  endif()
  add_library(imgui INTERFACE)
  target_link_libraries(imgui INTERFACE imgui::imgui)
  # No sources from third_party/imgui are compiled here.
endif()

# ---------------------------
# Tracy profiler (quiet & robust)
# We always expose a real target named 'tracy_client' that is modifiable.
#   1) Prefer vcpkg's Tracy::TracyClient -> wrap with INTERFACE
#   2) Else FetchContent (modern API)    -> use exported target or build client file
#   3) Else local checkout in third_party/tracy -> build client file
# ---------------------------
if(ENABLE_TRACY)
  # Allow choosing a revision when fetching
  if(NOT DEFINED TRACY_TAG OR TRACY_TAG STREQUAL "")
    set(TRACY_TAG "v0.11.1" CACHE STRING "Tracy tag/commit when fetching (e.g., v0.11.1 or a SHA)")
  endif()

  # First try vcpkg
  find_package(Tracy CONFIG QUIET)

  if(TARGET Tracy::TracyClient)
    # Wrap the imported target with a modifiable INTERFACE target
    add_library(tracy_client INTERFACE)
    target_link_libraries(tracy_client INTERFACE Tracy::TracyClient)
  else()
    option(TRACY_FETCH "Fetch Tracy with FetchContent (modern API)" ON)
    if(TRACY_FETCH)
      include(FetchContent)
      set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
      FetchContent_Declare(tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG        ${TRACY_TAG}
        GIT_SHALLOW    TRUE
      )
      # Modern, non-deprecated; avoids CMP0169 warnings
      FetchContent_MakeAvailable(tracy)

      if(TARGET Tracy::TracyClient)
        add_library(tracy_client INTERFACE)
        target_link_libraries(tracy_client INTERFACE Tracy::TracyClient)
      elseif(EXISTS "${tracy_SOURCE_DIR}/public/TracyClient.cpp")
        add_library(tracy_client STATIC "${tracy_SOURCE_DIR}/public/TracyClient.cpp")
        target_include_directories(tracy_client SYSTEM PUBLIC "${tracy_SOURCE_DIR}/public")
      else()
        message(FATAL_ERROR
          "Fetched Tracy, but couldn't find Tracy::TracyClient or public/TracyClient.cpp at: ${tracy_SOURCE_DIR}")
      endif()
    else()
      # No fetch: expect a local checkout
      if(NOT DEFINED TRACY_ROOT OR TRACY_ROOT STREQUAL "")
        set(TRACY_ROOT "${CMAKE_SOURCE_DIR}/third_party/tracy" CACHE PATH "Path to local Tracy checkout")
      endif()
      if(EXISTS "${TRACY_ROOT}/public/TracyClient.cpp")
        add_library(tracy_client STATIC "${TRACY_ROOT}/public/TracyClient.cpp")
        target_include_directories(tracy_client SYSTEM PUBLIC "${TRACY_ROOT}/public")
      else()
        message(FATAL_ERROR
          "ENABLE_TRACY=ON but Tracy sources not found at: ${TRACY_ROOT}. "
          "Set TRACY_ROOT to a checkout or enable TRACY_FETCH=ON.")
      endif()
    endif()
  endif()

  # Apply compile defs / extra link flags to 'tracy_client' (safe for both INTERFACE and STATIC)
  if(TARGET tracy_client)
    option(TRACY_ON_DEMAND      "Profiler connects on demand"       OFF)
    option(TRACY_ONLY_LOCALHOST "Accept only localhost connections"  OFF)
    option(TRACY_NO_BROADCAST   "Disable UDP discovery broadcast"    OFF)

    # INTERFACE here ensures consumers inherit the flags even if tracy_client is only an interface wrapper
    target_compile_definitions(tracy_client INTERFACE
      TRACY_ENABLE
      $<$<BOOL:${TRACY_ON_DEMAND}>:TRACY_ON_DEMAND>
      $<$<BOOL:${TRACY_ONLY_LOCALHOST}>:TRACY_ONLY_LOCALHOST>
      $<$<BOOL:${TRACY_NO_BROADCAST}>:TRACY_NO_BROADCAST>
    )
    if(WIN32)
      target_link_libraries(tracy_client INTERFACE ws2_32)
    endif()
  endif()
endif()
