if(FRONTEND STREQUAL "sdl")
  find_package(SDL2 CONFIG REQUIRED)         # via vcpkg.json
endif()

# ImGui: either local sources or vcpkg (choose one strategy)
if(ENABLE_IMGUI)
  # Example: use bundled imgui in third_party/imgui (if you have it)
  add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_dx11.cpp
    $<$<STREQUAL:${FRONTEND},win32>:third_party/imgui/backends/imgui_impl_win32.cpp>
    $<$<STREQUAL:${FRONTEND},sdl>:third_party/imgui/backends/imgui_impl_sdl2.cpp>
  )
  target_include_directories(imgui SYSTEM PUBLIC third_party/imgui)
endif()

# Tracy (quiet, robust fetch; build client only)
if(ENABLE_TRACY)
  # Default to fetching Tracy; allow overriding with -DTRACY_FETCH=OFF
  option(TRACY_FETCH "Fetch Tracy sources with FetchContent" ON)

  # Allow configuring the exact Tracy revision at configure time:
  #   -DTRACY_TAG=v0.11.1   OR   -DTRACY_TAG=v0.12.2   OR a full commit SHA.
  if(NOT DEFINED TRACY_TAG OR TRACY_TAG STREQUAL "")
    set(TRACY_TAG "v0.12.2" CACHE STRING "Tracy tag or commit (e.g., v0.11.1 or a SHA)")
  endif()

  if(TRACY_FETCH)
    include(FetchContent)

    # Make CI/offline more stable and avoid unnecessary updates.
    set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

    FetchContent_Declare(tracy
      GIT_REPOSITORY https://github.com/wolfpld/tracy.git
      GIT_TAG        ${TRACY_TAG}    # Must be a valid tag or commit (e.g., v0.11.1 or v0.12.2)
      GIT_SHALLOW    TRUE
    )
    # Download only; do not pull in Tracy's full CMake project (keeps Windows configure quiet)
    FetchContent_GetProperties(tracy)
    if(NOT tracy_POPULATED)
      FetchContent_Populate(tracy)
    endif()
    set(_TRACY_SRC "${tracy_SOURCE_DIR}")
  else()
    # If not fetching, allow users to point at a local checkout of Tracy.
    # By default, look for third_party/tracy in the source tree.
    if(NOT DEFINED TRACY_ROOT OR TRACY_ROOT STREQUAL "")
      set(TRACY_ROOT "${CMAKE_SOURCE_DIR}/third_party/tracy" CACHE PATH "Path to local Tracy checkout")
    endif()
    set(_TRACY_SRC "${TRACY_ROOT}")
  endif()

  # Build the client we embed in the game; GUI/server not needed.
  if(EXISTS "${_TRACY_SRC}/public/TracyClient.cpp")
    add_library(tracy_client STATIC "${_TRACY_SRC}/public/TracyClient.cpp")
    target_include_directories(tracy_client SYSTEM PUBLIC "${_TRACY_SRC}/public")

    # Useful runtime policy toggles
    option(TRACY_ON_DEMAND      "Profiler connects on demand"          OFF)
    option(TRACY_ONLY_LOCALHOST "Accept only localhost connections"     OFF)
    option(TRACY_NO_BROADCAST   "Disable UDP discovery broadcast"       OFF)

    target_compile_definitions(tracy_client PUBLIC
      TRACY_ENABLE
      $<$<BOOL:${TRACY_ON_DEMAND}>:TRACY_ON_DEMAND>
      $<$<BOOL:${TRACY_ONLY_LOCALHOST}>:TRACY_ONLY_LOCALHOST>
      $<$<BOOL:${TRACY_NO_BROADCAST}>:TRACY_NO_BROADCAST>
    )
    if(WIN32)
      target_link_libraries(tracy_client PUBLIC ws2_32)
    endif()
  else()
    message(FATAL_ERROR
      "ENABLE_TRACY=ON but Tracy sources were not found at: ${_TRACY_SRC}. "
      "Either enable TRACY_FETCH=ON or set TRACY_ROOT to a local Tracy checkout.")
  endif()
endif()
