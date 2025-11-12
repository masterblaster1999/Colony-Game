# cmake/CGShaders.cmake
# Windows-only HLSL build glue (DXC-first with FXC fallback). Minimal, robust, and CI-friendly.
# Requires CMake >= 3.20 for generator expressions in OUTPUT.
# Docs: add_custom_command/add_custom_target and generator expressions.
# - https://cmake.org/cmake/help/latest/command/add_custom_command.html
# - https://cmake.org/cmake/help/latest/release/3.20.html
#
# Compiler notes:
#  * FXC (SM2..5.1): /Zi for debug, /Fd for PDB, /O{d|1..3} for optimization.    [MS Docs]
#  * DXC (SM6+):      -Zi for debug, -Fd for PDB, -O{0|1|2|3}, -Qembed_debug/-Qstrip_debug. [DXC]
#  * PIX: For DXBC paths, /Zi + /Zss and /Fd <dir>\ can help automatic PDB resolution. You can also name a file explicitly. [PIX]
#
# (See project CMakeLists for how this module is used.)

include_guard(GLOBAL)
include(CMakeParseArguments)

# ------------------------------------------------------------------------------
# No-ops on non-Windows so include() remains harmless on other hosts.
# ------------------------------------------------------------------------------
if(NOT WIN32)
  message(STATUS "CGShaders.cmake: non-Windows host; shader build helpers are inert.")
  function(cg_compile_hlsl)
  endfunction()
  function(cg_link_shaders_to_target)
  endfunction()
  function(cg_set_hlsl_properties)
  endfunction()
  return()
endif()

# ------------------------------------------------------------------------------
# Options
# ------------------------------------------------------------------------------
option(CG_SHADERS_WARNINGS_AS_ERRORS "Treat shader compiler warnings as errors" OFF)
# Additional flags are passed through to whichever compiler is used (FXC or DXC).
set(CG_SHADERS_ADDITIONAL_FLAGS "" CACHE STRING "Additional flags passed to the shader compiler (semicolon-separated)")
set(CG_SHADER_OUTPUT_EXT "cso" CACHE STRING "Compiled shader extension (usually 'cso')")
set(CG_SHADERS_RUNTIME_SUBDIR "renderer/Shaders" CACHE STRING "Subfolder next to the EXE for compiled shaders")

# Manual overrides for tool locations:
set(CG_FXC_PATH "" CACHE FILEPATH "Full path to fxc.exe (optional override)")
set(CG_DXC_PATH "" CACHE FILEPATH "Full path to dxc.exe (optional override)")

# Choose compiler strategy:
set(COLONY_HLSL_COMPILER "AUTO" CACHE STRING "HLSL compiler: AUTO (prefer DXC), DXC (force), FXC (force)")
set_property(CACHE COLONY_HLSL_COMPILER PROPERTY STRINGS "AUTO" "DXC" "FXC")

# ------------------------------------------------------------------------------
# Locate DXC (DirectXShaderCompiler) — prefer vcpkg tool locations if present
# ------------------------------------------------------------------------------
function(_cg_find_dxc OUT_EXE)
  if(CG_DXC_PATH AND EXISTS "${CG_DXC_PATH}")
    set(${OUT_EXE} "${CG_DXC_PATH}" PARENT_SCOPE)
    return()
  endif()

  set(_hints "")

  # vcpkg common layout(s)
  foreach(_root IN ITEMS "$ENV{VCPKG_ROOT}" "$ENV{VCPKG_INSTALLATION_ROOT}")
    if(NOT "${_root}" STREQUAL "")
      list(APPEND _hints
        "${_root}/installed/x64-windows/tools/directx-dxc"
        "${_root}/installed/x86-windows/tools/directx-dxc"
        "${_root}/installed/arm64-windows/tools/directx-dxc")
    endif()
  endforeach()

  # Generic PATH search last
  find_program(DXC_EXE NAMES dxc dxc.exe HINTS ${_hints})
  if(NOT DXC_EXE)
    # Still optional; we'll fall back to FXC.
    set(${OUT_EXE} "" PARENT_SCOPE)
    return()
  endif()

  message(STATUS "CGShaders: DXC = ${DXC_EXE}")
  set(${OUT_EXE} "${DXC_EXE}" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# Locate FXC (Windows SDK) — robust on VS & Ninja generators
# ------------------------------------------------------------------------------
function(_cg_find_fxc OUT_EXE)
  if(CG_FXC_PATH AND EXISTS "${CG_FXC_PATH}")
    set(${OUT_EXE} "${CG_FXC_PATH}" PARENT_SCOPE)
    return()
  endif()

  set(_fxc_hints "")

  # Prefer WindowsSdkDir if VS toolchain initialized it
  if(DEFINED ENV{WindowsSdkDir} AND NOT "$ENV{WindowsSdkDir}" STREQUAL "")
    if(DEFINED CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION AND
       NOT CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION STREQUAL "")
      list(APPEND _fxc_hints
        "$ENV{WindowsSdkDir}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64")
    endif()
    list(APPEND _fxc_hints "$ENV{WindowsSdkDir}/bin/x64")
  endif()

  # Program Files (x86) — escape parentheses inside $ENV{...}
  if(DEFINED ENV{ProgramFiles\(x86\)} AND NOT "$ENV{ProgramFiles\(x86\)}" STREQUAL "")
    set(_PF86 "$ENV{ProgramFiles\(x86\)}")
    if(DEFINED CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION AND
       NOT CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION STREQUAL "")
      list(APPEND _fxc_hints
        "${_PF86}/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
        "${_PF86}/Windows Kits/11/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64")
    endif()
    list(APPEND _fxc_hints
      "${_PF86}/Windows Kits/10/bin/x64"
      "${_PF86}/Windows Kits/11/bin/x64")
  endif()

  # Program Files (no parens) — extra fallback
  if(DEFINED ENV{ProgramFiles} AND NOT "$ENV{ProgramFiles}" STREQUAL "")
    set(_PF "$ENV{ProgramFiles}")
    list(APPEND _fxc_hints
      "${_PF}/Windows Kits/10/bin/x64"
      "${_PF}/Windows Kits/11/bin/x64")
  endif()

  # Historical env sometimes present on CI agents
  if(DEFINED ENV{WindowsSdkVerBinPath} AND NOT "$ENV{WindowsSdkVerBinPath}" STREQUAL "")
    list(APPEND _fxc_hints "$ENV{WindowsSdkVerBinPath}/x64")
  endif()

  # Ask CMake to locate fxc.exe with our hints, then fall back to PATH
  find_program(FXC_EXE NAMES fxc fxc.exe HINTS ${_fxc_hints} PATH_SUFFIXES x64)
  if(NOT FXC_EXE)
    find_program(FXC_EXE NAMES fxc fxc.exe)
  endif()
  if(NOT FXC_EXE)
    message(FATAL_ERROR
      "fxc.exe not found.\n"
      "Hints searched:\n  ${_fxc_hints}\n"
      "Install the Windows 10/11 SDK or set CG_FXC_PATH to fxc.exe.")
  endif()

  message(STATUS "CGShaders: FXC = ${FXC_EXE}")
  set(${OUT_EXE} "${FXC_EXE}" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# Utilities
# ------------------------------------------------------------------------------
# Robust, case-insensitive stage inference covering:
#   *.vs.hlsl, *.ps.hlsl, *.cs.hlsl, *.compute.hlsl,
#   tokens/prefixes/suffixes: vs_, _vs, ...VS, etc.
function(_cg_infer_profile SHADER_PATH OUT_PROFILE)
  # Allow per-file override via source property first
  get_source_file_property(_p "${SHADER_PATH}" HLSL_PROFILE)
  if(NOT _p STREQUAL "NOTFOUND" AND _p)
    set(${OUT_PROFILE} "${_p}" PARENT_SCOPE)
    return()
  endif()

  get_filename_component(_name "${SHADER_PATH}" NAME)       # full file name
  get_filename_component(_stem "${SHADER_PATH}" NAME_WE)     # without extension
  string(TOLOWER "${_name}" _n)
  string(TOLOWER "${_stem}" _s)

  set(_stage "")

  # Strongest match: explicit double extensions (e.g., ToneMap.ps.hlsl)
  if(_n MATCHES "\\.vs\\.hlsl$")
    set(_stage "vs")
  elseif(_n MATCHES "\\.ps\\.hlsl$")
    set(_stage "ps")
  elseif(_n MATCHES "\\.(cs|compute)\\.hlsl$")
    set(_stage "cs")
  elseif(_n MATCHES "\\.gs\\.hlsl$")
    set(_stage "gs")
  elseif(_n MATCHES "\\.hs\\.hlsl$")
    set(_stage "hs")
  elseif(_n MATCHES "\\.ds\\.hlsl$")
    set(_stage "ds")
  endif()

  # Next: common prefixes/suffixes and tokenized names
  if(_stage STREQUAL "")
    if(_s MATCHES "^(vs)[_-]" OR _s MATCHES "([._-])vs([._-])" OR _s MATCHES "vs$")
      set(_stage "vs")
    elseif(_s MATCHES "^(ps)[_-]" OR _s MATCHES "([._-])ps([._-])" OR _s MATCHES "(fragment|frag)" OR _s MATCHES "ps$")
      set(_stage "ps")
    elseif(_s MATCHES "^(cs)[_-]" OR _s MATCHES "([._-])cs([._-])" OR _s MATCHES "(compute)" OR _s MATCHES "cs$")
      set(_stage "cs")
    elseif(_s MATCHES "^(gs)[_-]" OR _s MATCHES "([._-])gs([._-])" OR _s MATCHES "(geometry)" OR _s MATCHES "gs$")
      set(_stage "gs")
    elseif(_s MATCHES "^(hs)[_-]" OR _s MATCHES "([._-])hs([._-])" OR _s MATCHES "(hull)" OR _s MATCHES "hs$")
      set(_stage "hs")
    elseif(_s MATCHES "^(ds)[_-]" OR _s MATCHES "([._-])ds([._-])" OR _s MATCHES "(domain)" OR _s MATCHES "ds$")
      set(_stage "ds")
    endif()
  endif()

  if(_stage STREQUAL "")
    # Conservative default when nothing matches
    set(_stage "ps")
  endif()

  # Shader Model 5.x profiles for D3D11 (FXC/DXBC) by default
  set(${OUT_PROFILE} "${_stage}_5_0" PARENT_SCOPE)
endfunction()

# Accumulate FXC-style args (/I, /D, /WX)
function(_cg_accumulate_args PREFIX OUT_LIST)
  set(_res "")
  foreach(inc IN LISTS ${PREFIX}_INCLUDE_DIRS)
    if(inc)
      list(APPEND _res "/I" "${inc}")
    endif()
  endforeach()
  foreach(def IN LISTS ${PREFIX}_DEFINES)
    if(def)
      list(APPEND _res "/D" "${def}")
    endif()
  endforeach()
  if(CG_SHADERS_WARNINGS_AS_ERRORS)
    list(APPEND _res "/WX")
  endif()
  if(CG_SHADERS_ADDITIONAL_FLAGS)
    separate_arguments(_extra_flags NATIVE_COMMAND "${CG_SHADERS_ADDITIONAL_FLAGS}")
    list(APPEND _res ${_extra_flags})
  endif()
  set(${OUT_LIST} "${_res}" PARENT_SCOPE)
endfunction()

# Accumulate DXC-style args (-I, -D, -WX)
function(_cg_accumulate_args_dxc PREFIX OUT_LIST)
  set(_res "")
  foreach(inc IN LISTS ${PREFIX}_INCLUDE_DIRS)
    if(inc)
      list(APPEND _res "-I" "${inc}")
    endif()
  endforeach()
  foreach(def IN LISTS ${PREFIX}_DEFINES)
    if(def)
      list(APPEND _res "-D" "${def}")
    endif()
  endforeach()
  if(CG_SHADERS_WARNINGS_AS_ERRORS)
    list(APPEND _res "-WX")
  endif()
  if(CG_SHADERS_ADDITIONAL_FLAGS)
    separate_arguments(_extra_flags NATIVE_COMMAND "${CG_SHADERS_ADDITIONAL_FLAGS}")
    list(APPEND _res ${_extra_flags})
  endif()
  set(${OUT_LIST} "${_res}" PARENT_SCOPE)
endfunction()

function(_cg_collect_header_deps OUT_LIST)
  set(_deps "")
  foreach(inc IN LISTS ARGN)
    if(inc AND EXISTS "${inc}")
      file(GLOB_RECURSE _hdrs CONFIGURE_DEPENDS
        "${inc}/*.hlsli" "${inc}/*.fxh" "${inc}/*.hlslinc" "${inc}/*.h")
      if(_hdrs)
        list(REMOVE_DUPLICATES _hdrs)
        list(APPEND _deps ${_hdrs})
      endif()
    endif()
  endforeach()
  set(${OUT_LIST} "${_deps}" PARENT_SCOPE)
endfunction()

function(cg_set_hlsl_properties FILE)
  set(oneValueArgs ENTRY PROFILE)
  set(multiValueArgs DEFINES INCLUDE_DIRS FLAGS)
  cmake_parse_arguments(H "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
  if(H_ENTRY)
    set_source_files_properties(${FILE} PROPERTIES HLSL_ENTRY "${H_ENTRY}")
  endif()
  if(H_PROFILE)
    set_source_files_properties(${FILE} PROPERTIES HLSL_PROFILE "${H_PROFILE}")
  endif()
  if(H_DEFINES)
    set_source_files_properties(${FILE} PROPERTIES HLSL_DEFINES "${H_DEFINES}")
  endif()
  if(H_INCLUDE_DIRS)
    set_source_files_properties(${FILE} PROPERTIES HLSL_INCLUDE_DIRS "${H_INCLUDE_DIRS}")
  endif()
  if(H_FLAGS)
    set_source_files_properties(${FILE} PROPERTIES HLSL_FLAGS "${H_FLAGS}")
  endif()
endfunction()

# ------------------------------------------------------------------------------
# Public API
# ------------------------------------------------------------------------------
# cg_compile_hlsl(TargetName
#   SHADERS a.hlsl b.hlsl ...
#   [INCLUDE_DIRS ...]
#   [DEFINES ...]
#   [OUTPUT_DIR <dir>]        # default: ${CMAKE_BINARY_DIR}/shaders (per-config at build time)
#   [EMBED]                   # also generate .h via cmake/_BinaryToHeader.cmake
# )
function(cg_compile_hlsl TARGET_NAME)
  set(options EMBED)
  set(oneValueArgs OUTPUT_DIR)
  set(multiValueArgs SHADERS INCLUDE_DIRS DEFINES)
  cmake_parse_arguments(CG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT CG_SHADERS)
    message(FATAL_ERROR "cg_compile_hlsl(${TARGET_NAME}): SHADERS list is required.")
  endif()

  # Pick compilers based on user setting; prefer DXC when available.
  set(_use_dxc FALSE)
  if(COLONY_HLSL_COMPILER STREQUAL "DXC")
    set(_use_dxc TRUE)
  elseif(COLONY_HLSL_COMPILER STREQUAL "AUTO")
    _cg_find_dxc(DXC_EXE)
    if(DXC_EXE)
      set(_use_dxc TRUE)
    endif()
  endif()
  if(_use_dxc)
    if(NOT DEFINED DXC_EXE OR DXC_EXE STREQUAL "")
      _cg_find_dxc(DXC_EXE)
    endif()
    if(NOT DXC_EXE)
      message(STATUS "CGShaders: DXC not found; falling back to FXC.")
      set(_use_dxc FALSE)
    endif()
  endif()
  if(NOT _use_dxc)
    _cg_find_fxc(FXC_EXE)
  endif()

  # Configure-time base output directory WITHOUT generator expressions.
  if(NOT CG_OUTPUT_DIR)
    set(CG_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
  endif()
  file(MAKE_DIRECTORY "${CG_OUTPUT_DIR}")

  _cg_collect_header_deps(_approx_deps ${CG_INCLUDE_DIRS} "${CMAKE_CURRENT_SOURCE_DIR}")
  _cg_accumulate_args(CG _extra_args_fxc)
  _cg_accumulate_args_dxc(CG _extra_args_dxc)

  list(LENGTH CG_SHADERS _hlsl_count)
  message(STATUS "CGShaders: compiling ${_hlsl_count} HLSL file(s) -> ${CG_OUTPUT_DIR}/$<CONFIG> (compiler: $<IF:$<_use_dxc>,DXC,FXC>)")

  set(_outputs "")
  foreach(_src IN LISTS CG_SHADERS)
    if(_src MATCHES "\\.(hlsli|fxh|hlslinc)$")
      continue()
    endif()

    get_filename_component(_src_abs "${_src}" ABSOLUTE)
    get_filename_component(_base "${_src}" NAME_WE)

    # Per-file overrides (optional)
    get_source_file_property(_entry "${_src}" HLSL_ENTRY)
    if(_entry STREQUAL "NOTFOUND" OR NOT _entry)
      set(_entry "main")
    endif()
    _cg_infer_profile("${_src}" _profile)

    get_source_file_property(_src_defines  "${_src}" HLSL_DEFINES)
    if(_src_defines STREQUAL "NOTFOUND")
      set(_src_defines "")
    endif()
    get_source_file_property(_src_includes "${_src}" HLSL_INCLUDE_DIRS)
    if(_src_includes STREQUAL "NOTFOUND")
      set(_src_includes "")
    endif()
    get_source_file_property(_src_flags "${_src}" HLSL_FLAGS)
    if(_src_flags STREQUAL "NOTFOUND")
      set(_src_flags "")
    endif()

    # Per-source args (both FXC and DXC style)
    set(_per_src_args_fxc "")
    foreach(inc IN LISTS _src_includes)
      if(inc) list(APPEND _per_src_args_fxc "/I" "${inc}") endif()
    endforeach()
    foreach(def IN LISTS _src_defines)
      if(def) list(APPEND _per_src_args_fxc "/D" "${def}") endif()
    endforeach()
    if(_src_flags)
      separate_arguments(_src_flags_list NATIVE_COMMAND "${_src_flags}")
      list(APPEND _per_src_args_fxc ${_src_flags_list})
    endif()

    set(_per_src_args_dxc "")
    foreach(inc IN LISTS _src_includes)
      if(inc) list(APPEND _per_src_args_dxc "-I" "${inc}") endif()
    endforeach()
    foreach(def IN LISTS _src_defines)
      if(def) list(APPEND _per_src_args_dxc "-D" "${def}") endif()
    endforeach()
    if(_src_flags)
      separate_arguments(_src_flags_list2 NATIVE_COMMAND "${_src_flags}")
      list(APPEND _per_src_args_dxc ${_src_flags_list2})
    endif()

    # Per-config output file (genex allowed in OUTPUT since 3.20)
    if(CMAKE_CONFIGURATION_TYPES)
      set(_out_dir "${CG_OUTPUT_DIR}/$<CONFIG>")
    else()
      set(_out_dir "${CG_OUTPUT_DIR}")
    endif()
    set(_out "${_out_dir}/${_base}.${CG_SHADER_OUTPUT_EXT}")

    if(_use_dxc)
      # -------------------------
      # DXC command (SM 6.x recommended; SM 5.x is accepted by dxc too)
      # Debug & RelWithDebInfo: -Zi + -Qembed_debug (+ optional PDB via -Fd)
      # Release/MinSizeRel:     -O3 + -Qstrip_debug
      # -------------------------
      set(_cmd
        "${DXC_EXE}"
          -nologo
          -T "${_profile}"
          -E "${_entry}"
          -Fo "${_out}"
          $<$<CONFIG:Debug>:-O0;-Zi;-Qembed_debug>
          $<$<CONFIG:RelWithDebInfo>:-O3;-Zi;-Qembed_debug>
          $<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:-O3;-Qstrip_debug>
          ${_extra_args_dxc}
          ${_per_src_args_dxc}
          "${_src_abs}"
      )
      # Emit a PDB only for configs that want debug info
      list(APPEND _cmd
        $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:-Fd>
        $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:${_out}.pdb>
      )

      add_custom_command(
        OUTPUT  "${_out}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_out_dir}"
        COMMAND ${_cmd}
        MAIN_DEPENDENCY "${_src_abs}"
        DEPENDS "${_src_abs}" ${_approx_deps}
        COMMENT "DXC ${_profile} ${_base}.hlsl -> ${_out}"
        VERBATIM
        COMMAND_EXPAND_LISTS
      )
    else()
      # -------------------------
      # FXC command (SM 5.x, DXBC)
      # Debug:            /Zi /Od (+ PDB)
      # RelWithDebInfo:   /Zi /O3 (+ PDB)
      # Release/MinSize:  /O3
      # -------------------------
      set(_cmd
        "${FXC_EXE}"
          /nologo
          /T "${_profile}"
          /E "${_entry}"
          /Fo "${_out}"
          $<$<CONFIG:Debug>:/Zi;/Od>
          $<$<CONFIG:RelWithDebInfo>:/Zi;/O3>
          $<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:/O3>
          ${_extra_args_fxc}
          ${_per_src_args_fxc}
          "${_src_abs}"
      )
      # Prefer hashed PDB naming into a directory per PIX guidance, or just emit to a file path.
      list(APPEND _cmd
        $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:/Zss>
        $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:/Fd>
        $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:${_out_dir}/>
      )

      add_custom_command(
        OUTPUT  "${_out}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_out_dir}"
        COMMAND ${_cmd}
        MAIN_DEPENDENCY "${_src_abs}"
        DEPENDS "${_src_abs}" ${_approx_deps}
        COMMENT "FXC ${_profile} ${_base}.hlsl -> ${_out}"
        VERBATIM
        COMMAND_EXPAND_LISTS
      )
    endif()

    list(APPEND _outputs "${_out}")

    if(CG_EMBED)
      set(_hdr "${_out}.h")
      add_custom_command(
        OUTPUT "${_hdr}"
        COMMAND ${CMAKE_COMMAND}
          -DINPUT="${_out}"
          -DOUTPUT="${_hdr}"
          -P "${CMAKE_CURRENT_LIST_DIR}/_BinaryToHeader.cmake"
        DEPENDS "${_out}" "${CMAKE_CURRENT_LIST_DIR}/_BinaryToHeader.cmake"
        COMMENT "Embed ${_out} -> ${_hdr}"
        VERBATIM
      )
      list(APPEND _outputs "${_hdr}")
    endif()
  endforeach()

  # --- Collision-proof target creation: aggregator + unique sub-target per call
  # Aggregator (only once)
  if(NOT TARGET ${TARGET_NAME})
    add_custom_target(${TARGET_NAME})
    # Store a genex-free base dir so install/copy steps remain valid at configure time.
    set_property(TARGET ${TARGET_NAME} PROPERTY CG_SHADER_OUTPUT_DIR "${CG_OUTPUT_DIR}")
  endif()

  # Unique sub-target (hash by source dir + list of shaders)
  string(SHA1 _cg_hash "${CMAKE_CURRENT_SOURCE_DIR};${CG_SHADERS};${CG_OUTPUT_DIR}")
  string(SUBSTRING "${_cg_hash}" 0 8 _cg_short)
  set(_real_tgt "${TARGET_NAME}__${_cg_short}")

  if(NOT TARGET ${_real_tgt})
    add_custom_target(${_real_tgt} DEPENDS ${_outputs})
  endif()
  add_dependencies(${TARGET_NAME} ${_real_tgt})

  # Accumulate outputs on the aggregator for downstream queries
  get_target_property(_old_outs ${TARGET_NAME} CG_SHADER_OUTPUTS)
  if(_old_outs STREQUAL "CG_SHADER_OUTPUTS-NOTFOUND")
    set(_old_outs "")
  endif()
  list(APPEND _old_outs ${_outputs})
  set_property(TARGET ${TARGET_NAME} PROPERTY CG_SHADER_OUTPUTS "${_old_outs}")
endfunction()

# Copy the compiled blobs next to the exe under /renderer/Shaders by default.
# Uses a standalone custom target instead of POST_BUILD, so it works from any directory.
function(cg_link_shaders_to_target SHADER_TARGET RUNTIME_TARGET)
  if(NOT TARGET ${SHADER_TARGET})
    message(WARNING "cg_link_shaders_to_target: shader target '${SHADER_TARGET}' does not exist; skip linking shaders.")
    return()
  endif()
  if(NOT TARGET ${RUNTIME_TARGET})
    message(WARNING "cg_link_shaders_to_target: runtime target '${RUNTIME_TARGET}' does not exist; skip linking shaders.")
    return()
  endif()

  get_target_property(_outdir ${SHADER_TARGET} CG_SHADER_OUTPUT_DIR)
  if(NOT _outdir)
    message(FATAL_ERROR "cg_link_shaders_to_target: ${SHADER_TARGET} has no CG_SHADER_OUTPUT_DIR")
  endif()

  if(NOT DEFINED CG_SHADERS_RUNTIME_SUBDIR OR CG_SHADERS_RUNTIME_SUBDIR STREQUAL "")
    set(_subdir "renderer/Shaders")
  else()
    set(_subdir "${CG_SHADERS_RUNTIME_SUBDIR}")
  endif()

  set(_dest "$<TARGET_FILE_DIR:${RUNTIME_TARGET}>/${_subdir}")

  # Create a copy target that runs as part of 'all' and depends on both the exe and shaders.
  set(_copy_tgt "copy_shaders__${RUNTIME_TARGET}")
  if(NOT TARGET ${_copy_tgt})
    add_custom_target(${_copy_tgt} ALL
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_dest}"
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${_outdir}" "${_dest}"
      COMMENT "Copying shaders from ${_outdir} to ${_dest}"
      VERBATIM
    )
  endif()
  add_dependencies(${_copy_tgt} ${RUNTIME_TARGET} ${SHADER_TARGET})

  # Optional install step (puts them under bin/) — no generator expressions here.
  install(DIRECTORY "${_outdir}/" DESTINATION "bin/${_subdir}")
endfunction()
