# cmake/ColonyShaders.cmake
#
# Windows-only helper to compile HLSL with DXC (fallback to FXC if DXC not found),
# without using cross-directory `add_custom_command(TARGET …)` which would fail.
# See: CMake docs — add_custom_command TARGET must be in same directory. 
#
# USAGE EXAMPLES:
#   # Simple: one profile/entry for all sources
#   colony_compile_hlsl(ColonyGame
#     OUTPUT_DIR  "${CMAKE_CURRENT_BINARY_DIR}/shaders"
#     PROFILE     "ps_5_0"
#     ENTRY       "main"
#     SOURCES     "${CMAKE_SOURCE_DIR}/shaders/composite_ps.hlsl"
#     DEFINES     "FOO=1" "BAR=2"
#     INCLUDES    "${CMAKE_SOURCE_DIR}/shaders/includes"
#   )
#
#   # Per-file override: specify FILES items as "path;profile;entry"
#   colony_compile_hlsl(ColonyGame
#     OUTPUT_DIR  "${CMAKE_CURRENT_BINARY_DIR}/shaders"
#     FILES
#       "${CMAKE_SOURCE_DIR}/shaders/fullscreen_vs.hlsl;vs_5_0;main"
#       "${CMAKE_SOURCE_DIR}/shaders/composite_ps.hlsl;ps_5_0;main"
#   )
#
#   # Force compiler:
#   #   COMPILER auto|dxc|fxc   (default: auto -> DXC if found, else FXC)
#
# NOTES:
# - This helper adds two build-time artifacts:
#     <target>_shaders           : builds all .cso files (added to ALL)
#     copy_shaders_<target>      : stages compiled shaders next to the exe
#   When called from the same directory where <target> is defined, it will also
#   attach a POST_BUILD copy directly to <target>.
#
# - We intentionally avoid cross-directory `add_custom_command(TARGET …)`. If you
#   need POST_BUILD, invoke this function in the same CMakeLists.txt that defines
#   the executable/library target.

function(colony_compile_hlsl target)
  set(options NO_COPY)  # if set, skip all copy-to-runtime staging
  set(oneValueArgs OUTPUT_DIR PROFILE ENTRY COMPILER OUT_EXTENSION RUNTIME_SUBDIR)
  set(multiValueArgs SOURCES FILES DEFINES INCLUDES FLAGS)
  cmake_parse_arguments(HLSL "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGV})

  if (NOT WIN32)
    message(FATAL_ERROR "colony_compile_hlsl: Windows-only")
  endif()

  if (NOT TARGET ${target})
    message(FATAL_ERROR "colony_compile_hlsl: target '${target}' does not exist (call this after add_executable/add_library)")
  endif()

  # ---------- Choose compiler: DXC first, FXC fallback (or explicit) ----------
  set(_compiler "${HLSL_COMPILER}")
  if (NOT _compiler)
    set(_compiler "auto")
  endif()

  # Try DXC in PATH or common vcpkg tool location
  find_program(DXC_EXE NAMES dxc
    HINTS "$ENV{VCPKG_ROOT}/installed/x64-windows/tools/dxc"
  )
  # Try FXC from PATH/SDK
  find_program(FXC_EXE NAMES fxc)

  if (_compiler STREQUAL "dxc")
    if (NOT DXC_EXE)
      message(FATAL_ERROR "colony_compile_hlsl: COMPILER=dxc requested but 'dxc' not found")
    endif()
    set(_use_dxc ON)
  elseif(_compiler STREQUAL "fxc")
    if (NOT FXC_EXE)
      message(FATAL_ERROR "colony_compile_hlsl: COMPILER=fxc requested but 'fxc' not found")
    endif()
    set(_use_dxc OFF)
  else()
    if (DXC_EXE)
      set(_use_dxc ON)
    elseif(FXC_EXE)
      set(_use_dxc OFF)
    else()
      message(FATAL_ERROR "colony_compile_hlsl: neither 'dxc' nor 'fxc' found; install DirectXShaderCompiler or Windows SDK tools")
    endif()
  endif()

  # ---------- Output & runtime dirs ----------
  if (HLSL_OUTPUT_DIR)
    set(_outdir "${HLSL_OUTPUT_DIR}")
  else()
    # Keep this a plain path (no generator expressions) so it can be used in OUTPUT
    set(_outdir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
  endif()
  if (HLSL_RUNTIME_SUBDIR)
    set(_runtime_subdir "${HLSL_RUNTIME_SUBDIR}")
  else()
    set(_runtime_subdir "shaders")
  endif()
  if (HLSL_OUT_EXTENSION)
    set(_outext "${HLSL_OUT_EXTENSION}")
  else()
    set(_outext ".cso")
  endif()

  # We cannot mkdir a generator-expression path at configure time; use -E at build time.
  file(MAKE_DIRECTORY "${_outdir}")

  # ---------- Collect compilation units ----------
  set(_units)

  # 1) Structured per-file overrides: "path;profile;entry"
  foreach(_spec IN LISTS HLSL_FILES)
    string(REPLACE ";" "|" _tmp "${_spec}")
    string(REPLACE "|" ";" _tmp "${_tmp}")
    list(LENGTH _tmp _len)
    if (_len LESS 3)
      message(FATAL_ERROR "colony_compile_hlsl: FILES entry must be 'path;profile;entry' (got: ${_spec})")
    endif()
    list(GET _tmp 0 _src)
    list(GET _tmp 1 _prof)
    list(GET _tmp 2 _entry)
    list(APPEND _units "${_src};${_prof};${_entry}")
  endforeach()

  # 2) Flat SOURCES using global PROFILE/ENTRY
  if (HLSL_SOURCES)
    if (NOT HLSL_PROFILE OR NOT HLSL_ENTRY)
      message(FATAL_ERROR "colony_compile_hlsl: when using SOURCES you must also set PROFILE and ENTRY")
    endif()
    foreach(_src IN LISTS HLSL_SOURCES)
      list(APPEND _units "${_src};${HLSL_PROFILE};${HLSL_ENTRY}")
    endforeach()
  endif()

  if (NOT _units)
    # Nothing to do.
    return()
  endif()

  # ---------- Per-unit custom commands (OUTPUT signature) ----------
  set(_outputs)
  foreach(_u IN LISTS _units)
    string(REPLACE ";" "|" _t "${_u}")
    string(REPLACE "|" ";" _t "${_t}")
    list(GET _t 0 _src)
    list(GET _t 1 _profile) # e.g. vs_5_0 / ps_5_0 / cs_6_7 etc.
    list(GET _t 2 _entry)

    get_filename_component(_name "${_src}" NAME_WE)
    set(_out "${_outdir}/${_name}${_outext}")

    # Build command line
    if (_use_dxc)
      set(_cmd "${DXC_EXE}" -nologo -T "${_profile}" -E "${_entry}" -Fo "${_out}" "${_src}")
      foreach(_def IN LISTS HLSL_DEFINES)
        list(APPEND _cmd -D "${_def}")
      endforeach()
      foreach(_inc IN LISTS HLSL_INCLUDES)
        list(APPEND _cmd -I "${_inc}")
      endforeach()
      foreach(_f IN LISTS HLSL_FLAGS)
        list(APPEND _cmd "${_f}")
      endforeach()
    else()
      # FXC (SM 5.x)
      set(_cmd "${FXC_EXE}" /nologo /T "${_profile}" /E "${_entry}" /Fo "${_out}" "${_src}")
      foreach(_def IN LISTS HLSL_DEFINES)
        list(APPEND _cmd /D "${_def}")
      endforeach()
      foreach(_inc IN LISTS HLSL_INCLUDES)
        list(APPEND _cmd /I "${_inc}")
      endforeach()
      foreach(_f IN LISTS HLSL_FLAGS)
        list(APPEND _cmd "${_f}")
      endforeach()
    endif()

    # The OUTPUT signature is safe to call from any directory (no TARGET coupling).
    add_custom_command(
      OUTPUT  "${_out}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_outdir}"
      COMMAND ${_cmd}
      DEPENDS "${_src}"
      COMMENT "Compiling HLSL: ${_src} -> ${_out}"
      VERBATIM
    )

    list(APPEND _outputs "${_out}")
  endforeach()

  # ---------- Aggregate build target for shaders ----------
  # Make the name unique and identifier-safe
  string(MAKE_C_IDENTIFIER "${target}" _tgt_id)
  set(_shader_tgt "${_tgt_id}_shaders")

  if (NOT TARGET ${_shader_tgt})
    add_custom_target(${_shader_tgt} ALL DEPENDS ${_outputs})
  else()
    # In case user calls function multiple times for same target,
    # append the dependencies to the existing aggregate target.
    add_custom_target(${_shader_tgt}_append DEPENDS ${_outputs})
    add_dependencies(${_shader_tgt} ${_shader_tgt}_append)
  endif()

  # Ensure building the game builds shaders first.
  add_dependencies(${target} ${_shader_tgt})

  # ---------- Stage shaders next to the executable ----------
  if (NOT HLSL_NO_COPY)
    # If invoked from the same directory where the target was defined, we can
    # safely attach a POST_BUILD step directly to <target> (best UX).
    get_target_property(_tgt_srcdir ${target} SOURCE_DIR)  # requires CMake ≥ 3.4
    if (_tgt_srcdir AND _tgt_srcdir STREQUAL CMAKE_CURRENT_SOURCE_DIR)
      add_custom_command(
        TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/${_runtime_subdir}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_outdir}" "$<TARGET_FILE_DIR:${target}>/${_runtime_subdir}"
        COMMENT "Copying shaders to $<TARGET_FILE_DIR:${target}>/${_runtime_subdir}"
        VERBATIM
      )
    else()
      # Different directory: avoid TARGET signature to prevent CMake error.
      # Create a separate copy target built by default and dependent on shader outputs.
      set(_copy_tgt "copy_shaders_${_tgt_id}")
      if (NOT TARGET ${_copy_tgt})
        add_custom_target(${_copy_tgt} ALL
          COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/${_runtime_subdir}"
          COMMAND ${CMAKE_COMMAND} -E copy_directory "${_outdir}" "$<TARGET_FILE_DIR:${target}>/${_runtime_subdir}"
          COMMENT "Copying shaders to $<TARGET_FILE_DIR:${target}>/${_runtime_subdir}"
          VERBATIM
          DEPENDS ${_outputs}
        )
        # NOTE: We intentionally do NOT add ${_copy_tgt} as a dependency of ${target}
        # because using $<TARGET_FILE_DIR:${target}> in COMMAND would auto-add a
        # dependency *from* the copy target *to* ${target} (CMP0112), creating a cycle.
        # Build 'ALL_BUILD' (or the copy target) to stage shaders when working cross-dir.
      endif()
    endif()
  endif()
endfunction()
