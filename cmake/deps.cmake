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
  target_include_directories(imgui PUBLIC third_party/imgui)
endif()

# Tracy (robust FetchContent + Windows linkage)
if(ENABLE_TRACY)
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
    FetchContent_MakeAvailable(tracy)
  else()
    # If not fetching, allow users to point at a local checkout of Tracy.
    # By default, look for third_party/tracy in the source tree.
    if(NOT DEFINED TRACY_ROOT OR TRACY_ROOT STREQUAL "")
      set(TRACY_ROOT "${CMAKE_SOURCE_DIR}/third_party/tracy" CACHE PATH "Path to local Tracy checkout")
    endif()
    set(tracy_SOURCE_DIR "${TRACY_ROOT}")
  endif()

  # Build the client we embed in the game; GUI/server not needed.
  if(EXISTS "${tracy_SOURCE_DIR}/public/TracyClient.cpp")
    add_library(tracy_client STATIC "${tracy_SOURCE_DIR}/public/TracyClient.cpp")
    target_include_directories(tracy_client PUBLIC "${tracy_SOURCE_DIR}/public")
    target_compile_definitions(tracy_client PUBLIC TRACY_ENABLE)
    if(WIN32)
      target_link_libraries(tracy_client PUBLIC ws2_32)
    endif()
  else()
    message(FATAL_ERROR
      "ENABLE_TRACY=ON but Tracy sources were not found. "
      "Either enable TRACY_FETCH=ON or set TRACY_ROOT to a local Tracy checkout.")
  endif()
endif()
