# cmake/CGOptionalDeps.cmake
#
# Optional dependency wiring (Windows-focused):
# - ImGui (prefer vcpkg target imgui::imgui, else FetchContent fallback)
# - Tracy (prefer vcpkg target Tracy::TracyClient, else FetchContent fallback)
#
# Call:
#   cg_setup_optional_deps(
#     CORE_TARGET colony_core
#     ENABLE_IMGUI ${ENABLE_IMGUI}
#     ENABLE_TRACY ${ENABLE_TRACY}
#   )

include_guard(GLOBAL)

include(FetchContent)

function(cg_setup_optional_deps)
  cmake_parse_arguments(ARG "" "CORE_TARGET;ENABLE_IMGUI;ENABLE_TRACY" "" ${ARGN})

  if(NOT ARG_CORE_TARGET)
    message(FATAL_ERROR "cg_setup_optional_deps: CORE_TARGET is required.")
  endif()

  set(_core "${ARG_CORE_TARGET}")

  # ------------------------------- ImGui (opt) -------------------------------
  if(WIN32 AND ARG_ENABLE_IMGUI)
    # Propagate so EXE-owned sources also see it.
    target_compile_definitions("${_core}" PUBLIC CG_ENABLE_IMGUI=1)

    # Prefer packaged target (vcpkg typically provides imgui::imgui)
    if(NOT TARGET imgui::imgui)
      find_package(imgui CONFIG QUIET)
    endif()

    if(TARGET imgui::imgui)
      # Use packaged target
      target_link_libraries("${_core}" PUBLIC imgui::imgui)
    else()
      message(STATUS "[ImGui] CMake package target imgui::imgui not found; using FetchContent fallback.")

      # First-to-record wins if parent already declared "imgui"
      FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.90.5
      )
      FetchContent_MakeAvailable(imgui)

      if(NOT TARGET imgui_backend)
        add_library(imgui_backend STATIC
          ${imgui_SOURCE_DIR}/imgui.cpp
          ${imgui_SOURCE_DIR}/imgui_draw.cpp
          ${imgui_SOURCE_DIR}/imgui_tables.cpp
          ${imgui_SOURCE_DIR}/imgui_widgets.cpp
          ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
          ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
        )
        target_include_directories(imgui_backend PUBLIC
          ${imgui_SOURCE_DIR}
          ${imgui_SOURCE_DIR}/backends
        )
        target_compile_definitions(imgui_backend PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
        target_link_libraries(imgui_backend PUBLIC d3d11 dxgi)
      endif()

      target_link_libraries("${_core}" PUBLIC imgui_backend)
    endif()
  endif()

  # ------------------------------- Tracy (opt) -------------------------------
  if(WIN32 AND ARG_ENABLE_TRACY)
    # Propagate so EXE-owned sources also see it.
    target_compile_definitions("${_core}" PUBLIC TRACY_ENABLE=1)

    # Prefer packaged target (vcpkg typically provides Tracy::TracyClient)
    if(NOT TARGET Tracy::TracyClient)
      find_package(Tracy CONFIG QUIET)
    endif()

    if(NOT TARGET Tracy::TracyClient)
      message(STATUS "[Tracy] CMake package target Tracy::TracyClient not found; using FetchContent fallback.")

      # First-to-record wins if parent already declared "tracy"
      FetchContent_Declare(tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG        v0.11.0
      )
      FetchContent_MakeAvailable(tracy)

      if(NOT TARGET tracy_client)
        add_library(tracy_client STATIC ${tracy_SOURCE_DIR}/public/TracyClient.cpp)
        target_include_directories(tracy_client PUBLIC
          ${tracy_SOURCE_DIR}/public
          ${tracy_SOURCE_DIR}
        )
        if(MSVC)
          target_link_libraries(tracy_client PUBLIC ws2_32 dbghelp)
        endif()
        add_library(Tracy::TracyClient ALIAS tracy_client)
      endif()
    endif()

    target_link_libraries("${_core}" PUBLIC Tracy::TracyClient)
  endif()

  unset(_core)
endfunction()
