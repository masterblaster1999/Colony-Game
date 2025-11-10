# cmake/CGShaders.cmake
# Windows-only HLSL build glue (FXC/SM5.x, optional DXC/SM6.x). Minimal, robust, and CI-friendly.
# Uses COMMAND_EXPAND_LISTS and generator expressions correctly.
# Docs: add_custom_command/VERBATIM+COMMAND_EXPAND_LISTS, generator expressions. 
# (See project docs you referenced.)

include_guard(GLOBAL)
include(CMakeParseArguments)

# ----------------------------------------------------------------------------- #
#  Non-Windows hosts: keep CMake includes inert so configure doesn't break
# ----------------------------------------------------------------------------- #
if(NOT WIN32)
  message(STATUS "CGShaders.cmake: non-Windows host; shader build helpers are inert.")
  function(cg_compile_hlsl)  endfunction()
  function(cg_link_shaders_to_target)  endfunction()
  function(cg_set_hlsl_properties)  endfunction()
  function(cg_compile_hlsl_glob)  endfunction()
  function(cg_install_shaders_from_target)  endfunction()
  return()
endif()

# ----------------------------------------------------------------------------- #
#  Options & knobs
# ----------------------------------------------------------------------------- #
# AUTO: prefer DXC if available, else FXC. Explicit values: DXC or FXC.
set(CG_HLSL_COMPILER "AUTO" CACHE STRING "HLSL compiler: AUTO | DXC | FXC")

option(CG_SHADERS_WARNINGS_AS_ERRORS "Treat shader compiler warnings as errors" OFF)
# Extra flags (semicolon-separated). Example: /Zpr;/Ges  or  -Zpr;-Ges
set(CG_SHADERS_ADDITIONAL_FLAGS "" CACHE STRING "Additional flags passed to the shader compiler (semicolon-separated)")

# File extension of compiled blobs
set(CG_SHADER_OUTPUT_EXT "cso" CACHE STRING "Compiled shader extension (usually 'cso')")

# Where compiled shaders are copied beside the runtime target.
set(CG_SHADERS_RUNTIME_SUBDIR "renderer/Shaders" CACHE STRING
    "Where compiled shaders are copied next to the runtime target")

# Optional manual overrides for compiler locations
set(CG_FXC_PATH "" CACHE FILEPATH "Full path to fxc.exe (optional override)")
set(CG_DXC_PATH "" CACHE FILEPATH "Full path to dxc.exe (optional override)")

# Generate PDBs for debug/relwithdebinfo when supported (dxc/fxc)
option(CG_SHADERS_DEBUG_PDB "Emit PDB for Debug/RelWithDebInfo when supported" ON)

# ----------------------------------------------------------------------------- #
#  Find compilers
# ----------------------------------------------------------------------------- #
function(_cg_find_fxc OUT_EXE)
  if(CG_FXC_PATH AND EXISTS "${CG_FXC_PATH}")
    set(${OUT_EXE} "${CG_FXC_PATH}" PARENT_SCOPE)
    return()
  endif()

  set(_fxc_hints "")
  if(DEFINED CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION AND NOT CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION STREQUAL "")
    list(APPEND _fxc_hints
      "$ENV{WindowsSdkDir}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
      "$ENV{ProgramFiles(x86)}/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
      "$ENV{ProgramFiles(x86)}/Windows Kits/11/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64")
  endif()
  list(APPEND _fxc_hints
    "$ENV{WindowsSdkDir}/bin/x64"
    "$ENV{ProgramFiles(x86)}/Windows Kits/10/bin/x64"
    "$ENV{ProgramFiles(x86)}/Windows Kits/11/bin/x64")

  find_program(FXC_EXE NAMES fxc fxc.exe HINTS ${_fxc_hints})
  if(NOT FXC_EXE)
    find_program(FXC_EXE NAMES fxc fxc.exe)
  endif()
  if(NOT FXC_EXE)
    message(STATUS "CGShaders: fxc.exe not found via hints/Path.")
  else()
    message(STATUS "CGShaders: using FXC at: ${FXC_EXE}")
  endif()
  set(${OUT_EXE} "${FXC_EXE}" PARENT_SCOPE)
endfunction()

function(_cg_find_dxc OUT_EXE)
  if(CG_DXC_PATH AND EXISTS "${CG_DXC_PATH}")
    set(${OUT_EXE} "${CG_DXC_PATH}" PARENT_SCOPE)
    return()
  endif()

  # vcpkg typical location + fallback to PATH
  set(_dxc_hints
    "$ENV{VCPKG_ROOT}/installed/x64-windows/tools/directx-dxc"
    "$ENV{VCPKG_INSTALLATION_ROOT}/installed/x64-windows/tools/directx-dxc")

  find_program(DXC_EXE NAMES dxc dxc.exe HINTS ${_dxc_hints})
  if(NOT DXC_EXE)
    find_program(DXC_EXE NAMES dxc dxc.exe)
  endif()
  if(NOT DXC_EXE)
    message(STATUS "CGShaders: dxc.exe not found via hints/Path.")
  else()
    message(STATUS "CGShaders: using DXC at: ${DXC_EXE}")
  endif()
  set(${OUT_EXE} "${DXC_EXE}" PARENT_SCOPE)
endfunction()

# ----------------------------------------------------------------------------- #
#  Utilities
# ----------------------------------------------------------------------------- #
# Infer profile by suffix; allow per-file override via HLSL_PROFILE.
# If using DXC (SM6+), the default goes to *_6_0, else *_5_0.
function(_cg_infer_profile SHADER_PATH OUT_PROFILE)
  get_source_file_property(_p "${SHADER_PATH}" HLSL_PROFILE)
  if(NOT _p STREQUAL "NOTFOUND" AND _p)
    set(${OUT_PROFILE} "${_p}" PARENT_SCOPE)
    return()
  endif()

  # Read preferred default SM from caller (5_0 or 6_0)
  if(DEFINED CG_DEFAULT_SM)
    set(_sm "${CG_DEFAULT_SM}")
  else()
    set(_sm "5_0")
  endif()

  get_filename_component(_base "${SHADER_PATH}" NAME)
  string(REGEX MATCH "([\\._-])(vs|ps|cs|gs|hs|ds)([\\._-])" _m "${_base}")
  if(_m)
    string(REGEX REPLACE ".*([\\._-])(vs|ps|cs|gs|hs|ds)([\\._-]).*" "\\2" _stage "${_base}")
  else()
    set(_stage "ps")
  endif()
  set(${OUT_PROFILE} "${_stage}_${_sm}" PARENT_SCOPE)
endfunction()

# Build aggregated include/define args. Keep flag and value as separate argv entries.
function(_cg_accumulate_args PREFIX OUT_LIST)
  set(_res "")
  foreach(inc IN LISTS ${PREFIX}_INCLUDE_DIRS)
    if(inc) list(APPEND _res "/I" "${inc}") endif()
  endforeach()
  foreach(def IN LISTS ${PREFIX}_DEFINES)
    if(def) list(APPEND _res "/D" "${def}") endif()
  endforeach()

  if(CG_SHADERS_WARNINGS_AS_ERRORS)
    # DXC: -Werror; FXC: /WX
    list(APPEND _res "$<$<BOOL:${DXC_ENABLED}>:-Werror>" "$<$<NOT:$<BOOL:${DXC_ENABLED}>>:/WX>")
  endif()

  if(CG_SHADERS_ADDITIONAL_FLAGS)
    separate_arguments(_extra_flags NATIVE_COMMAND "${CG_SHADERS_ADDITIONAL_FLAGS}")
    list(APPEND _res ${_extra_flags})
  endif()

  set(${OUT_LIST} "${_res}" PARENT_SCOPE)
endfunction()

# Build a dependency list by scanning include directories for common shader include extensions.
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

# Convenience: per-file overrides in CMakeLists:
#   cg_set_hlsl_properties(<file>
#     [ENTRY main] [PROFILE ps_5_0]
#     [DEFINES FOO=1 BAR=2] [INCLUDE_DIRS dir1 dir2] [FLAGS "/Zpr /Ges"] )
function(cg_set_hlsl_properties FILE)
  set(oneValueArgs ENTRY PROFILE)
  set(multiValueArgs DEFINES INCLUDE_DIRS FLAGS)
  cmake_parse_arguments(H "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(H_ENTRY)         set_source_files_properties(${FILE} PROPERTIES HLSL_ENTRY "${H_ENTRY}") endif()
  if(H_PROFILE)       set_source_files_properties(${FILE} PROPERTIES HLSL_PROFILE "${H_PROFILE}") endif()
  if(H_DEFINES)       set_source_files_properties(${FILE} PROPERTIES HLSL_DEFINES "${H_DEFINES}") endif()
  if(H_INCLUDE_DIRS)  set_source_files_properties(${FILE} PROPERTIES HLSL_INCLUDE_DIRS "${H_INCLUDE_DIRS}") endif()
  if(H_FLAGS)         set_source_files_properties(${FILE} PROPERTIES HLSL_FLAGS "${H_FLAGS}") endif()
endfunction()

# ----------------------------------------------------------------------------- #
#  Public API
# ----------------------------------------------------------------------------- #
# cg_compile_hlsl(TargetName
#   SHADERS    a.b.hlsl ...
#   [INCLUDE_DIRS ...]
#   [DEFINES ...]
#   [OUTPUT_DIR <dir>]   # default: ${CMAKE_BINARY_DIR}/shaders[/$<CONFIG>]
#   [EMBED]              # also generate .h from blobs via _BinaryToHeader.cmake
# )
function(cg_compile_hlsl TARGET_NAME)
  set(options EMBED)
  set(oneValueArgs OUTPUT_DIR)
  set(multiValueArgs SHADERS INCLUDE_DIRS DEFINES)
  cmake_parse_arguments(CG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT CG_SHADERS)
    message(FATAL_ERROR "cg_compile_hlsl(${TARGET_NAME}): SHADERS list is required.")
  endif()

  # Discover compilers and choose one based on policy
  _cg_find_dxc(DXC_EXE)
  _cg_find_fxc(FXC_EXE)

  string(TOUPPER "${CG_HLSL_COMPILER}" _CGC)
  set(DXC_ENABLED 0)
  if(_CGC STREQUAL "DXC")
    if(NOT DXC_EXE)
      message(FATAL_ERROR "CG_HLSL_COMPILER=DXC but dxc.exe not found.")
    endif()
    set(DXC_ENABLED 1)
  elseif(_CGC STREQUAL "FXC")
    if(NOT FXC_EXE)
      message(FATAL_ERROR "CG_HLSL_COMPILER=FXC but fxc.exe not found.")
    endif()
  else() # AUTO
    if(DXC_EXE)
      set(DXC_ENABLED 1)
    elseif(FXC_EXE)
      set(DXC_ENABLED 0)
    else()
      message(FATAL_ERROR "No HLSL compiler found (dxc or fxc). Install Windows SDK or vcpkg: directx-dxc.")
    endif()
  endif()

  # Default output dir (multi-config aware)
  if(NOT CG_OUTPUT_DIR)
    if(CMAKE_CONFIGURATION_TYPES)
      set(CG_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders/$<CONFIG>")
    else()
      set(CG_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
    endif()
  endif()
  file(MAKE_DIRECTORY "${CG_OUTPUT_DIR}")

  # Track includes in provided dirs and current dir for deps; collect common args
  _cg_collect_header_deps(_approx_deps ${CG_INCLUDE_DIRS} "${CMAKE_CURRENT_SOURCE_DIR}")
  _cg_accumulate_args(CG _extra_args)

  # Default SM depends on compiler (DXC->6_0, FXC->5_0)
  if(DXC_ENABLED)
    set(CG_DEFAULT_SM "6_0")
  else()
    set(CG_DEFAULT_SM "5_0")
  endif()

  list(LENGTH CG_SHADERS _hlsl_count)
  message(STATUS "CGShaders: compiling ${_hlsl_count} HLSL file(s) with $<IF:$<BOOL:${DXC_ENABLED}>,DXC,FXC> -> ${CG_OUTPUT_DIR}")

  set(_outputs "")

  foreach(_src IN LISTS CG_SHADERS)
    if(_src MATCHES "\\.(hlsli|fxh|hlslinc)$")
      continue()
    endif()

    get_filename_component(_src_abs "${_src}" ABSOLUTE)
    get_filename_component(_base    "${_src}" NAME_WE)

    # Per-file overrides (optional)
    get_source_file_property(_entry "${_src}" HLSL_ENTRY)
    if(_entry STREQUAL "NOTFOUND" OR NOT _entry)
      set(_entry "main")
    endif()
    _cg_infer_profile("${_src}" _profile)

    get_source_file_property(_src_defines  "${_src}" HLSL_DEFINES)
    if(_src_defines STREQUAL "NOTFOUND") set(_src_defines "") endif()
    get_source_file_property(_src_includes "${_src}" HLSL_INCLUDE_DIRS)
    if(_src_includes STREQUAL "NOTFOUND") set(_src_includes "") endif()
    get_source_file_property(_src_flags    "${_src}" HLSL_FLAGS)
    if(_src_flags STREQUAL "NOTFOUND") set(_src_flags "") endif()

    # Build per-source args
    set(_per_src_args "")
    foreach(inc IN LISTS _src_includes)
      if(inc) list(APPEND _per_src_args "/I" "${inc}") endif()
    endforeach()
    foreach(def IN LISTS _src_defines)
      if(def) list(APPEND _per_src_args "/D" "${def}") endif()
    endforeach()
    if(_src_flags)
      separate_arguments(_src_flags_list NATIVE_COMMAND "${_src_flags}")
      list(APPEND _per_src_args ${_src_flags_list})
    endif()

    # Output paths
    set(_out_ext "${CG_SHADER_OUTPUT_EXT}")
    if(NOT _out_ext) set(_out_ext "cso") endif()
    set(_out "${CG_OUTPUT_DIR}/${_base}.${_out_ext}")

    # Optional PDB path
    set(_pdb "")
    if(CG_SHADERS_DEBUG_PDB)
      set(_pdb "${CG_OUTPUT_DIR}/${_base}.pdb")
    endif()

    if(DXC_ENABLED)
      # DXC flags (SM6+)
      set(_cmd "${DXC_EXE}"
        -T "${_profile}" -E "${_entry}"
        -Fo "${_out}"
        $<$<CONFIG:Debug>:-Od;-Zi>
        $<$<CONFIG:RelWithDebInfo>:-Zi;-O2>
        $<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:-O3>
        ${_extra_args} ${_per_src_args}
        -nologo
      )
      if(_pdb)
        list(APPEND _cmd $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:-Fd;${_pdb}>)
      endif()
      list(APPEND _cmd "${_src_abs}")
    else()
      # FXC flags (SM5.x)
      set(_cmd "${FXC_EXE}"
        /T "${_profile}" /E "${_entry}"
        /Fo "${_out}"
        $<$<CONFIG:Debug>:/Zi;/Od>
        $<$<CONFIG:RelWithDebInfo>:/Zi;/O2>
        $<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:/O3>
        ${_extra_args} ${_per_src_args}
        /nologo
      )
      if(_pdb)
        list(APPEND _cmd $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:/Fd;${_pdb}>)
      endif()
      list(APPEND _cmd "${_src_abs}")
    endif()

    add_custom_command(
      OUTPUT "${_out}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CG_OUTPUT_DIR}"
      COMMAND ${_cmd}
      MAIN_DEPENDENCY "${_src_abs}"
      DEPENDS "${_src_abs}" ${_approx_deps}
      COMMENT "HLSL ${_profile} ${_base}.hlsl -> ${_out}"
      VERBATIM
      COMMAND_EXPAND_LISTS
    )

    list(APPEND _outputs "${_out}")

    if(CG_EMBED)
      set(_hdr "${_out}.h")
      add_custom_command(
        OUTPUT "${_hdr}"
        COMMAND ${CMAKE_COMMAND} -DINPUT="${_out}" -DOUTPUT="${_hdr}"
                -P "${CMAKE_CURRENT_LIST_DIR}/_BinaryToHeader.cmake"
        DEPENDS "${_out}" "${CMAKE_CURRENT_LIST_DIR}/_BinaryToHeader.cmake"
        COMMENT "Embed ${_out} -> ${_hdr}"
        VERBATIM
      )
      list(APPEND _outputs "${_hdr}")
    endif()
  endforeach()

  add_custom_target(${TARGET_NAME} DEPENDS ${_outputs})
  set_property(TARGET ${TARGET_NAME} PROPERTY CG_SHADER_OUTPUT_DIR "${CG_OUTPUT_DIR}")
  set_property(TARGET ${TARGET_NAME} PROPERTY CG_SHADER_OUTPUTS "${_outputs}")
endfunction()

# Copy compiled blobs next to the runtime exe under /renderer/Shaders (default).
function(cg_link_shaders_to_target SHADER_TARGET RUNTIME_TARGET)
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

  add_dependencies(${RUNTIME_TARGET} ${SHADER_TARGET})
  add_custom_command(TARGET ${RUNTIME_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_dest}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${_outdir}" "${_dest}"
    COMMENT "Copying shaders to ${_dest}"
    VERBATIM
  )

  # Install compiled shaders without relying on genex in DIRECTORY (robust across generators).
  cg_install_shaders_from_target(${SHADER_TARGET} "bin/${_subdir}")
endfunction()

# Convenience: compile all shaders matching a glob into a single target.
# Usage: cg_compile_hlsl_glob(MyShaders "${CMAKE_SOURCE_DIR}/shaders/*.hlsl" [INCLUDE_DIRS ...] [DEFINES ...])
function(cg_compile_hlsl_glob TARGET_NAME GLOBPATTERN)
  set(options EMBED)
  set(oneValueArgs OUTPUT_DIR)
  set(multiValueArgs INCLUDE_DIRS DEFINES)
  cmake_parse_arguments(CG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  file(GLOB _SH CONFIGURE_DEPENDS ${GLOBPATTERN})
  if(NOT _SH)
    message(STATUS "cg_compile_hlsl_glob: no files matched ${GLOBPATTERN}")
  endif()
  cg_compile_hlsl(${TARGET_NAME}
    SHADERS ${_SH}
    INCLUDE_DIRS ${CG_INCLUDE_DIRS}
    DEFINES ${CG_DEFINES}
    OUTPUT_DIR ${CG_OUTPUT_DIR}
    $<$<BOOL:${CG_EMBED}>:EMBED>
  )
endfunction()

# Install helper that avoids generator expressions in DIRECTORY.
# It installs per configuration when multi-config generators are used.
function(cg_install_shaders_from_target SHADER_TARGET DEST_SUBDIR)
  get_target_property(_outdir ${SHADER_TARGET} CG_SHADER_OUTPUT_DIR)
  if(NOT _outdir)
    message(FATAL_ERROR "cg_install_shaders_from_target: ${SHADER_TARGET} has no CG_SHADER_OUTPUT_DIR")
  endif()

  if(CMAKE_CONFIGURATION_TYPES)
    foreach(_cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
      # Example path: <build>/shaders/Debug/
      install(DIRECTORY "${CMAKE_BINARY_DIR}/shaders/${_cfg}/"
              DESTINATION "${DEST_SUBDIR}"
              CONFIGURATIONS ${_cfg})
    endforeach()
  else()
    install(DIRECTORY "${_outdir}/" DESTINATION "${DEST_SUBDIR}")
  endif()
endfunction()
