# cmake/CGCoreTarget.cmake
#
# Owns the "colony_core" target configuration:
# - compile features
# - include directories
# - PCH setup
# - linking DirectXTex + third-party libs + build options
# - Taskflow fallback include-dir if package not found
# - Optional ImGui/Tracy wiring via CGOptionalDeps.cmake
# - Toolchain defaults (warnings/sanitizers) via CGToolchainWin.cmake
#
# Patch notes (applied):
# - Filter out src-gamesingletu.cpp by default (prevents ODR/link duplicate symbol issues).
#   Pass SINGLE_TU to keep it.
# - Filter out compiled Windows platform implementation sources (*/platform/win/*.c|*.cpp)
#   from colony_core so platform code is owned by colony_platform_win.
# - Ensure CrashDumpWin.cpp (if it ends up in colony_core sources) is built with /EHa and
#   skipped from PCH + Unity to avoid MSVC C4652 PCH mismatch and unity/ODR edge cases.

include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/CGToolchainWin.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CGOptionalDeps.cmake")

function(cg_setup_core_target)
  cmake_parse_arguments(ARG
    "SINGLE_TU"
    "TARGET;ROOT_DIR;FRONTEND;UNITY_BUILD;USE_PCH;PCH_HEADER;ENABLE_IMGUI;ENABLE_TRACY;WERROR"
    "SOURCES;PUBLIC_INCLUDE_DIRS;THIRDPARTY_LIBS"
    ${ARGN}
  )

  if(NOT ARG_TARGET)
    set(ARG_TARGET colony_core)
  endif()

  if(NOT ARG_ROOT_DIR)
    get_filename_component(ARG_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
  endif()

  if(NOT DEFINED ARG_FRONTEND)
    set(ARG_FRONTEND "")
  endif()

  set(_tgt "${ARG_TARGET}")

  if(NOT TARGET "${_tgt}")
    add_library("${_tgt}" STATIC)
  endif()

  # ----------------------------
  # Sources (with safety filters)
  # ----------------------------
  if(ARG_SOURCES)
    set(_srcs_filtered "")
    set(_excluded_platform 0)
    set(_excluded_entry    0)
    set(_excluded_single   0)

    foreach(_s IN LISTS ARG_SOURCES)
      set(_s_norm "${_s}")
      string(REPLACE "\\" "/" _s_norm "${_s_norm}")

      get_filename_component(_bn  "${_s_norm}" NAME)
      get_filename_component(_ext "${_s_norm}" EXT)
      string(TOLOWER "${_ext}" _ext_l)

      # 1) Exclude the single-TU jumbo file by default (it commonly causes ODR/link dupes).
      if(NOT ARG_SINGLE_TU AND _bn STREQUAL "src-gamesingletu.cpp")
        math(EXPR _excluded_single "${_excluded_single} + 1")
        continue()
      endif()

      # 2) Keep entrypoints and GPU-export TUs out of the core static library.
      if(_ext_l MATCHES "^\\.(c|cc|cxx|cpp)$")
        if(_bn MATCHES "^(EntryWinMain|WinLauncher|AppMain|win_entry)\\.(c|cc|cxx|cpp)$")
          math(EXPR _excluded_entry "${_excluded_entry} + 1")
          continue()
        endif()
        if(_bn STREQUAL "HighPerfGPU.cpp")
          math(EXPR _excluded_entry "${_excluded_entry} + 1")
          continue()
        endif()

        # 3) Do not compile platform implementation sources into colony_core.
        #    These should belong to colony_platform_win (compiled once).
        if(_s_norm MATCHES "/platform/win/")
          math(EXPR _excluded_platform "${_excluded_platform} + 1")
          continue()
        endif()
      endif()

      list(APPEND _srcs_filtered "${_s}")
    endforeach()

    if(_excluded_single GREATER 0 OR _excluded_entry GREATER 0 OR _excluded_platform GREATER 0)
      set(_msg "cg_setup_core_target(${_tgt}): filtered")
      if(_excluded_single GREATER 0)
        set(_msg "${_msg} ${_excluded_single} single-TU")
      endif()
      if(_excluded_entry GREATER 0)
        set(_msg "${_msg} ${_excluded_entry} entrypoint/GPU")
      endif()
      if(_excluded_platform GREATER 0)
        set(_msg "${_msg} ${_excluded_platform} platform/win")
      endif()
      message(STATUS "${_msg} source(s) from colony_core")
    endif()

    if(_srcs_filtered)
      target_sources("${_tgt}" PRIVATE ${_srcs_filtered})
    endif()

    unset(_srcs_filtered)
    unset(_excluded_platform)
    unset(_excluded_entry)
    unset(_excluded_single)
  endif()

  # Unity
  if(ARG_UNITY_BUILD)
    set_target_properties("${_tgt}" PROPERTIES UNITY_BUILD ON)
  else()
    set_target_properties("${_tgt}" PROPERTIES UNITY_BUILD OFF)
  endif()

  if(WIN32)
    target_compile_definitions("${_tgt}" PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  endif()

  if(CMAKE_VERSION VERSION_LESS 3.21)
    target_compile_features("${_tgt}" PUBLIC cxx_std_20)
  else()
    target_compile_features("${_tgt}" PUBLIC cxx_std_23)
  endif()

  if(ARG_PUBLIC_INCLUDE_DIRS)
    target_include_directories("${_tgt}" PUBLIC ${ARG_PUBLIC_INCLUDE_DIRS})
  else()
    target_include_directories("${_tgt}"
      PUBLIC
        "${ARG_ROOT_DIR}/src"
        "${ARG_ROOT_DIR}/include"
        "${ARG_ROOT_DIR}"
        "${CMAKE_BINARY_DIR}/generated"
    )
  endif()

  target_compile_definitions("${_tgt}"
    PUBLIC
      $<$<STREQUAL:${ARG_FRONTEND},sdl>:COLONY_USE_SDL=1>
  )

  if(WIN32)
    target_link_libraries("${_tgt}" PUBLIC shell32 ole32)
  endif()

  # Apply toolchain defaults (warnings + optional compile /WX + sanitizers)
  set(_werror OFF)
  if(NOT "${ARG_WERROR}" STREQUAL "")
    set(_werror "${ARG_WERROR}")
  elseif(DEFINED COLONY_WERROR)
    set(_werror "${COLONY_WERROR}")
  endif()

  cg_toolchain_win_setup_target("${_tgt}"
    WERROR  "${_werror}"
    IS_EXE  OFF
  )
  unset(_werror)

  # Ensure DirectXTex target exists (vcpkg: Microsoft::DirectXTex)
  if(NOT TARGET Microsoft::DirectXTex)
    find_package(directxtex CONFIG REQUIRED)
  endif()

  if(NOT TARGET colony_build_options)
    add_library(colony_build_options INTERFACE)
  endif()

  # Third-party libs list
  set(_tp_libs "")
  if(ARG_THIRDPARTY_LIBS)
    set(_tp_libs ${ARG_THIRDPARTY_LIBS})
  elseif(DEFINED COLONY_THIRDPARTY_LIBS)
    set(_tp_libs ${COLONY_THIRDPARTY_LIBS})
  endif()

  if(NOT ARG_FRONTEND STREQUAL "sdl")
    if(_tp_libs)
      list(FILTER _tp_libs EXCLUDE REGEX "SDL2main|SDL2::SDL2main")
    endif()
  endif()

  if(_tp_libs)
    target_link_libraries("${_tgt}" PUBLIC ${_tp_libs})
  endif()
  target_link_libraries("${_tgt}" PUBLIC colony_build_options Microsoft::DirectXTex)
  unset(_tp_libs)

  # Optional deps (ImGui/Tracy)
  cg_setup_optional_deps(
    CORE_TARGET "${_tgt}"
    ENABLE_IMGUI ${ARG_ENABLE_IMGUI}
    ENABLE_TRACY ${ARG_ENABLE_TRACY}
  )

  # PCH
  if(ARG_USE_PCH)
    set(_pch "")

    if(ARG_PCH_HEADER)
      if(IS_ABSOLUTE "${ARG_PCH_HEADER}")
        set(_pch_candidate "${ARG_PCH_HEADER}")
      else()
        set(_pch_candidate "${ARG_ROOT_DIR}/${ARG_PCH_HEADER}")
      endif()

      if(EXISTS "${_pch_candidate}")
        set(_pch "${_pch_candidate}")
      endif()
      unset(_pch_candidate)
    endif()

    if(NOT _pch)
      if(EXISTS "${ARG_ROOT_DIR}/src/pch.h")
        set(_pch "${ARG_ROOT_DIR}/src/pch.h")
      elseif(EXISTS "${ARG_ROOT_DIR}/src/pch.hpp")
        set(_pch "${ARG_ROOT_DIR}/src/pch.hpp")
      endif()
    endif()

    if(_pch)
      target_precompile_headers("${_tgt}" PRIVATE "${_pch}")
    endif()

    unset(_pch)
  endif()

  # CrashDumpWin.cpp: /EHa + skip PCH + skip unity (avoid MSVC C4652 + unity edge cases).
  # Prefer the shared helper if the root CMake defines it; otherwise do a local best-effort.
  if(COMMAND colony_apply_seh_for_crashdump)
    colony_apply_seh_for_crashdump("${_tgt}")
  else()
    if(MSVC AND (NOT DEFINED COLONY_SEH_CRASHDUMP OR COLONY_SEH_CRASHDUMP))
      get_target_property(_core_srcs "${_tgt}" SOURCES)
      if(_core_srcs)
        get_target_property(_tgt_dir "${_tgt}" SOURCE_DIR)
        if(NOT _tgt_dir)
          set(_tgt_dir "${CMAKE_SOURCE_DIR}")
        endif()

        foreach(_src IN LISTS _core_srcs)
          if(_src MATCHES "CrashDumpWin\\.cpp$")
            if(IS_ABSOLUTE "${_src}")
              set(_abs_candidate "${_src}")
            else()
              set(_abs_candidate "${_tgt_dir}/${_src}")
            endif()

            if(EXISTS "${_abs_candidate}")
              get_filename_component(_abs "${_abs_candidate}" REALPATH)
            else()
              set(_abs "${_abs_candidate}")
            endif()

            set_source_files_properties("${_abs}"
              TARGET_DIRECTORY "${_tgt}"
              PROPERTIES
                COMPILE_OPTIONS "/EHa"
                SKIP_PRECOMPILE_HEADERS ON
                SKIP_UNITY_BUILD_INCLUSION ON
            )
          endif()
        endforeach()
      endif()
    endif()
  endif()

  # Taskflow fallback
  if(NOT DEFINED taskflow_FOUND)
    find_package(taskflow CONFIG QUIET)
  endif()

  if(NOT taskflow_FOUND)
    find_path(TASKFLOW_INCLUDE_DIR taskflow/taskflow.hpp)
    if(NOT TASKFLOW_INCLUDE_DIR)
      message(FATAL_ERROR "Taskflow not found. Install via vcpkg (taskflow) or set TASKFLOW_INCLUDE_DIR.")
    endif()
    target_include_directories("${_tgt}" PUBLIC "${TASKFLOW_INCLUDE_DIR}")
  endif()
endfunction()
