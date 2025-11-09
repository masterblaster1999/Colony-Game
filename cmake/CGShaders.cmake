# cmake/CGShaders.cmake
include_guard(GLOBAL)
include(CMakeParseArguments)

# Use modern expansion rules (needed for ProgramFiles(x86) escaping)
if(POLICY CMP0053)
  cmake_policy(SET CMP0053 NEW)
endif()

# ---- Normalize key Windows env paths to forward slashes --------------------
set(_PF86 "")
if(DEFINED ENV{ProgramFiles(x86)})
  # Escape parentheses per CMake docs
  set(_PF86 "$ENV{ProgramFiles\(x86\)}")
  file(TO_CMAKE_PATH "${_PF86}" _PF86)
endif()

set(_WSDK "")
if(DEFINED ENV{WindowsSdkDir})
  file(TO_CMAKE_PATH "$ENV{WindowsSdkDir}" _WSDK)
endif()

# Try to infer a concrete Windows SDK version (works for Ninja on CI too)
set(_WINSDK_VER "")
if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
  set(_WINSDK_VER "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")
elseif(_WSDK)
  file(GLOB _cg_bin_vers LIST_DIRECTORIES TRUE "${_WSDK}/bin/*")
  list(SORT _cg_bin_vers COMPARE NATURAL ORDER DESCENDING)
  if(_cg_bin_vers)
    list(GET _cg_bin_vers 0 _cg_latest_bin)
    get_filename_component(_WINSDK_VER "${_cg_latest_bin}" NAME)
  endif()
endif()

# Optional manual override (useful on CI):
# cmake -DCOLONY_FXC_PATH="C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64/fxc.exe"
set(COLONY_FXC_PATH "${COLONY_FXC_PATH}" CACHE FILEPATH "Path to fxc.exe (optional override)")

# ---- FXC discovery (SM 5.x offline compiler for D3D11) --------------------
function(_cg_find_fxc OUT_FXC)
  if(NOT WIN32)
    message(FATAL_ERROR "FXC is Windows-only")
  endif()

  if(COLONY_FXC_PATH AND EXISTS "${COLONY_FXC_PATH}")
    set(${OUT_FXC} "${COLONY_FXC_PATH}" PARENT_SCOPE)
    return()
  endif()

  set(_FXC_HINTS "")
  if(_WSDK)
    if(_WINSDK_VER)
      list(APPEND _FXC_HINTS "${_WSDK}/bin/${_WINSDK_VER}/x64")
    endif()
    list(APPEND _FXC_HINTS "${_WSDK}/bin/x64")
  endif()
  if(_PF86)
    if(_WINSDK_VER)
      list(APPEND _FXC_HINTS "${_PF86}/Windows Kits/10/bin/${_WINSDK_VER}/x64")
      list(APPEND _FXC_HINTS "${_PF86}/Windows Kits/11/bin/${_WINSDK_VER}/x64")
    endif()
    list(APPEND _FXC_HINTS "${_PF86}/Windows Kits/10/bin/x64")
    list(APPEND _FXC_HINTS "${_PF86}/Windows Kits/11/bin/x64")
  endif()
  # Sensible extra fallback; PATH will be searched afterward too
  list(APPEND _FXC_HINTS "C:/Program Files (x86)/Windows Kits/10/bin/x64")

  find_program(FXC_EXE NAMES fxc fxc.exe HINTS ${_FXC_HINTS})
  if(NOT FXC_EXE)
    message(FATAL_ERROR "fxc.exe not found. Install the Windows 10/11 SDK.")
  endif()
  set(${OUT_FXC} "${FXC_EXE}" PARENT_SCOPE)
endfunction()

# ---- Optional DXC discovery (for future SM6+ work; not required for D3D11) -
function(_cg_find_dxc OUT_DXC)
  set(_hints "")
  if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    list(APPEND _hints
      "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/directx-dxc"
      "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
  endif()
  if(DEFINED ENV{DXC_DIR} AND NOT "$ENV{DXC_DIR}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{DXC_DIR}" _DXC_DIR)
    list(APPEND _hints "${_DXC_DIR}")
  endif()
  if(_WSDK)
    if(_WINSDK_VER)
      list(APPEND _hints "${_WSDK}/bin/${_WINSDK_VER}/x64")
    endif()
    list(APPEND _hints "${_WSDK}/bin/x64")
  endif()
  if(_PF86)
    if(_WINSDK_VER)
      list(APPEND _hints "${_PF86}/Windows Kits/10/bin/${_WINSDK_VER}/x64")
    endif()
    list(APPEND _hints "${_PF86}/Windows Kits/10/bin/x64")
  endif()

  find_program(DXC_EXE NAMES dxc dxc.exe HINTS ${_hints})
  if(NOT DXC_EXE)
    message(STATUS "dxc.exe not found; continuing (FXC will be used for SM 5.x).")
  endif()
  set(${OUT_DXC} "${DXC_EXE}" PARENT_SCOPE)
endfunction()

# ---- Heuristic: infer SM5 profile from filename suffix ---------------------
function(_cg_guess_profile_from_name SRC OUT_PROFILE)
  get_filename_component(_name_we "${SRC}" NAME_WE)
  set(_p "ps_5_0")
  if(_name_we MATCHES "_vs$")   set(_p "vs_5_0") endif()
  if(_name_we MATCHES "_ps$")   set(_p "ps_5_0") endif()
  if(_name_we MATCHES "_cs$")   set(_p "cs_5_0") endif()
  if(_name_we MATCHES "_gs$")   set(_p "gs_5_0") endif()
  if(_name_we MATCHES "_hs$")   set(_p "hs_5_0") endif()
  if(_name_we MATCHES "_ds$")   set(_p "ds_5_0") endif()
  set(${OUT_PROFILE} "${_p}" PARENT_SCOPE)
endfunction()

# ---- Public API: compile HLSL with FXC into .cso blobs ---------------------
# cg_compile_hlsl(
#   <TARGET_NAME>
#   SHADERS <list of .hlsl>
#   [INCLUDE_DIRS <dirs...>]
#   [DEFINES <defs...>]
# )
function(cg_compile_hlsl TARGET_NAME)
  if(NOT WIN32)
    message(FATAL_ERROR "cg_compile_hlsl is Windows-only")
  endif()

  set(_opts)
  set(_one SHADERS)
  set(_many INCLUDE_DIRS DEFINES)
  cmake_parse_arguments(CG "${_opts}" "${_one}" "${_many}" ${ARGN})

  if(NOT CG_SHADERS)
    message(WARNING "cg_compile_hlsl: no SHADERS specified")
    add_custom_target(${TARGET_NAME})
    return()
  endif()

  _cg_find_fxc(FXC_EXE)

  set(_outdir "${CMAKE_BINARY_DIR}/shaders")
  file(MAKE_DIRECTORY "${_outdir}")

  set(_outputs)
  foreach(_src IN LISTS CG_SHADERS)
    get_filename_component(_abs "${_src}" ABSOLUTE)
    get_filename_component(_base "${_abs}" NAME_WE)

    _cg_guess_profile_from_name("${_abs}" _profile)
    set(_entry "main")
    set(_out   "${_outdir}/${_base}.cso")

    # Per-config flags via generator expressions (safe in multi-config IDEs)
    set(_fxc_flags
      /nologo /T ${_profile} /E ${_entry} /Fo "${_out}"
      "$<$<CONFIG:Debug>:/Od>" "$<$<CONFIG:Debug>:/Zi>"
      "$<$<NOT:$<CONFIG:Debug>>:/O3>")
    foreach(_inc IN LISTS CG_INCLUDE_DIRS)
      list(APPEND _fxc_flags /I "${_inc}")
    endforeach()
    foreach(_def IN LISTS CG_DEFINES)
      list(APPEND _fxc_flags /D "${_def}")
    endforeach()

    add_custom_command(
      OUTPUT "${_out}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_outdir}"
      COMMAND "${FXC_EXE}" ${_fxc_flags} "${_abs}"
      MAIN_DEPENDENCY "${_abs}"
      COMMENT "FXC ${_profile}:${_entry} ${_base}.hlsl -> ${_base}.cso"
      VERBATIM
    )
    list(APPEND _outputs "${_out}")
  endforeach()

  add_custom_target(${TARGET_NAME} DEPENDS ${_outputs})
endfunction()

# Wire shader build target to the game target
function(cg_link_shaders_to_target SH_TARGET GAME_TARGET)
  if(TARGET ${SH_TARGET} AND TARGET ${GAME_TARGET})
    add_dependencies(${GAME_TARGET} ${SH_TARGET})
    install(DIRECTORY "${CMAKE_BINARY_DIR}/shaders/" DESTINATION "bin/shaders")
  endif()
endfunction()
