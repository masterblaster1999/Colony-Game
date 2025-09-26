# Frontend selection
if(FRONTEND STREQUAL "sdl")
  # SDL2 is provided by vcpkg ("sdl2" port)
  find_package(SDL2 CONFIG REQUIRED)
endif()

# ---------------------------------------------------------------------------
# ImGui: use the vcpkg-provided target (NO vendored sources, NO local paths)
# ---------------------------------------------------------------------------
# IMPORTANT:
#   - Enable the appropriate imgui port features in vcpkg.json:
#       * Win32 + D3D11 frontend:  [core, docking-experimental, win32-binding, dx11-binding]
#       * SDL2 + D3D11 frontend:   [core, docking-experimental, sdl2-binding, dx11-binding]
#
#   - We deliberately DO NOT compile any files from third_party/imgui/*.
#   - To keep existing CMake link lines working, we create a lightweight "imgui"
#     INTERFACE target that forwards to the real vcpkg target imgui::imgui.
#
if(ENABLE_IMGUI)
  find_package(imgui CONFIG REQUIRED)           # from vcpkg; exports imgui::imgui
  # Backwards-compatible target name "imgui" (was a STATIC lib before), now an interface alias.
  if(TARGET imgui)
    # If some outer code already defined a conflicting target named "imgui", fail loudly.
    message(FATAL_ERROR "A target named 'imgui' already exists. Remove any vendored/legacy ImGui target.")
  endif()
  add_library(imgui INTERFACE)
  target_link_libraries(imgui INTERFACE imgui::imgui)
  # No third_party/imgui sources, no backends compiled here — the vcpkg port handles that via features.
endif()

# ---------------------------------------------------------------------------
# Tracy (quiet & robust):
#   1) Prefer vcpkg's exported target: Tracy::TracyClient
#   2) Else, FetchContent the official repo with the MODERN API
#   3) Fallback: build client from public/TracyClient.cpp only
# ---------------------------------------------------------------------------
if(ENABLE_TRACY)
  # Allow the user to pick a specific revision when fetching Tracy
  if(NOT DEFINED TRACY_TAG OR TRACY_TAG STREQUAL "")
    set(TRACY_TAG "v0.11.1" CACHE STRING "Tracy tag/commit to fetch when vcpkg is unavailable")
  endif()

  # First try vcpkg
  find_package(Tracy CONFIG QUIET)

  if(TARGET Tracy::TracyClient)
    # Make a friendly local name that existing code can link to
    add_library(tracy_client ALIAS Tracy::TracyClient)
  else()
    # Optionally fetch Tracy (default ON)
    option(TRACY_FETCH "Fetch Tracy sources with FetchContent (modern API)" ON)

    if(TRACY_FETCH)
      include(FetchContent)
      # Stable CI: do not update once fetched unless asked
      set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
      FetchContent_Declare(tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG        ${TRACY_TAG}
        GIT_SHALLOW    TRUE
      )
      # Modern, non-deprecated way (avoids CMP0169 warnings)
      FetchContent_MakeAvailable(tracy)

      # If the project exports a namespaced target, use it; otherwise compile client-only
      if(TARGET Tracy::TracyClient)
        add_library(tracy_client ALIAS Tracy::TracyClient)
      elseif(EXISTS "${tracy_SOURCE_DIR}/public/TracyClient.cpp")
        add_library(tracy_client STATIC "${tracy_SOURCE_DIR}/public/TracyClient.cpp")
        target_include_directories(tracy_client SYSTEM PUBLIC "${tracy_SOURCE_DIR}/public")
      else()
        message(FATAL_ERROR
          "Fetched Tracy, but couldn't locate Tracy::TracyClient or public/TracyClient.cpp at: ${tracy_SOURCE_DIR}")
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
          "Set TRACY_ROOT to a valid checkout or enable TRACY_FETCH=ON.")
      endif()
    endif()
  endif()

  # Useful runtime policy toggles (apply to whichever tracy_client we built/aliased)
  option(TRACY_ON_DEMAND      "Profiler connects on demand"         OFF)
  option(TRACY_ONLY_LOCALHOST "Accept only localhost connections"   OFF)
  option(TRACY_NO_BROADCAST   "Disable UDP discovery broadcast"     OFF)

  if(TARGET tracy_client)
    target_compile_definitions(tracy_client PUBLIC
      TRACY_ENABLE
      $<$<BOOL:${TRACY_ON_DEMAND}>:TRACY_ON_DEMAND>
      $<$<BOOL:${TRACY_ONLY_LOCALHOST}>:TRACY_ONLY_LOCALHOST>
      $<$<BOOL:${TRACY_NO_BROADCAST}>:TRACY_NO_BROADCAST>
    )
    if(WIN32)
      target_link_libraries(tracy_client PUBLIC ws2_32)
    endif()
  endif()
endif()
