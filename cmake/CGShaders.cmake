# cmake/CGShaders.cmake
include_guard(GLOBAL)
include(CMakeParseArguments)

# Use modern variable/escape evaluation (helps when reading $ENV{...})
if(POLICY CMP0053)
  cmake_policy(SET CMP0053 NEW)
endif()

# Optional override (useful on CI):
# cmake -DCOLONY_FXC_PATH="C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64/fxc.exe"
set(COLONY_FXC_PATH "" CACHE FILEPATH "Full path to fxc.exe (optional override)")

# --- Internal: find fxc.exe (SM 5.x offline compiler for D3D11) -------------
function(_cg_find_fxc OUT_EXE)
  if(NOT WIN32)
    message(FATAL_ERROR "FXC is Windows-only")
  endif()

  if(COLONY_FXC_PATH AND EXISTS "${COLONY_FXC_PATH}")
    set(${OUT_EXE} "${COLONY_FXC_PATH}" PARENT_SCOPE)
    return()
  endif()

  set(_hints "")

  # Windows SDK root (authoritative)
  if(DEFINED ENV{WindowsSdkDir} AND NOT "$ENV{WindowsSdkDir}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{WindowsSdkDir}" _WSDK)

    if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
      list(APPEND _hints "${_WSDK}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64")
    endif()

    list(APPEND _hints "${_WSDK}/bin/x64")
  endif()

  # Well-known fallbacks (don’t depend on ProgramFiles(x86) env parsing)
  list(APPEND _hints
    "C:/Program Files (x86)/Windows Kits/11/bin/x64"
    "C:/Program Files (x86)/Windows Kits/10/bin/x64")

  # Search hints first, then PATH
  find_program(FXC_EXE NAMES fxc fxc.exe HINTS ${_hints} PATH_SUFFIXES x64)
  if(NOT FXC_EXE)
    find_program(FXC_EXE NAMES fxc fxc.exe)
  endif()

  if(NOT FXC_EXE)
    message(FATAL_ERROR "fxc.exe not found. Install the Windows 10/11 SDK or pass -DCOLONY_FXC_PATH=...")
  endif()

  set(${OUT_EXE} "${FXC_EXE}" PARENT_SCOPE)
endfunction()

# --- Internal: infer SM5 profile from filename suffix ------------------------
function(_cg_guess_profile_from_name SRC OUT_PROFILE)
  get_filename_component(_name_we "${SRC}" NAME_WE)
  set(_stage "ps")
  if(_name_we MATCHES "_vs$") set(_stage "vs") endif()
  if(_name_we MATCHES "_ps$") set(_stage "ps") endif()
  if(_name_we MATCHES "_cs$") set(_stage "cs") endif()
  if(_name_we MATCHES "_gs$") set(_stage "gs") endif()
  if(_name_we MATCHES "_hs$") set(_stage "hs") endif()
  if(_name_we MATCHES "_ds$") set(_stage "ds") endif()
  set(${OUT_PROFILE} "${_stage}_5_0" PARENT_SCOPE)
endfunction()

# --- Public API: compile HLSL with FXC into .cso blobs -----------------------
# cg_compile_hlsl(
#   <TARGET_NAME>
#   SHADERS <list of .hlsl files>
#   [INCLUDE_DIRS <dirs...>]
#   [DEFINES <defs...>]
#   [OUTPUT_DIR <dir>]              # default: ${CMAKE_BINARY_DIR}/shaders
#   [ENTRY <name>]                  # default: main
# )
function(cg_compile_hlsl TARGET_NAME)
  set(_opts)
  set(_one SHADERS OUTPUT_DIR ENTRY)
  set(_many INCLUDE_DIRS DEFINES)
  cmake_parse_arguments(CG "${_opts}" "${_one}" "${_many}" ${ARGN})

  if(NOT CG_SHADERS)
    message(WARNING "cg_compile_hlsl: no SHADERS specified")
    add_custom_target(${TARGET_NAME})
    return()
  endif()

  _cg_find_fxc(FXC_EXE)

  if(NOT CG_OUTPUT_DIR)
    set(CG_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
  endif()
  file(MAKE_DIRECTORY "${CG_OUTPUT_DIR}")

  if(NOT CG_ENTRY)
    set(CG_ENTRY "main")
  endif()

  # Build include/define flags
  set(_fxc_flags_base
      "$<$<CONFIG:Debug>:/Zi>" "$<$<CONFIG:Debug>:/Od>"
      "$<$<NOT:$<CONFIG:Debug>>:/O3>")

  set(_fxc_inc "")
  foreach(_inc IN LISTS CG_INCLUDE_DIRS)
    list(APPEND _fxc_inc "/I" "${_inc}")
  endforeach()

  set(_fxc_def "")
  foreach(_def IN LISTS CG_DEFINES)
    list(APPEND _fxc_def "/D" "${_def}")
  endforeach()

  set(_outputs "")
  foreach(_src IN LISTS CG_SHADERS)
    # Skip headers
    if(_src MATCHES "\\.hlsli$")
      continue()
    endif()

    get_filename_component(_abs  "${_src}" ABSOLUTE)
    get_filename_component(_base "${_src}" NAME_WE)

    _cg_guess_profile_from_name("${_abs}" _profile)
    set(_out "${CG_OUTPUT_DIR}/${_base}.cso")

    add_custom_command(
      OUTPUT "${_out}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CG_OUTPUT_DIR}"
      COMMAND "${FXC_EXE}"
              /nologo
              /T "${_profile}"
              /E "${CG_ENTRY}"
              ${_fxc_flags_base}
              ${_fxc_def} ${_fxc_inc}
              /Fo "${_out}" "${_abs}"
      MAIN_DEPENDENCY "${_abs}"
      COMMENT "FXC ${_profile}:${CG_ENTRY} ${_base}.hlsl -> ${_base}.cso"
      VERBATIM
    )
    list(APPEND _outputs "${_out}")
  endforeach()

  add_custom_target(${TARGET_NAME} DEPENDS ${_outputs})
  set_property(TARGET ${TARGET_NAME} PROPERTY CG_SHADER_OUTPUT_DIR "${CG_OUTPUT_DIR}")
  set_property(TARGET ${TARGET_NAME} PROPERTY CG_SHADER_OUTPUTS "${_outputs}")
endfunction()

# --- Public API: wire shader build outputs to the runtime target -------------
function(cg_link_shaders_to_target SHADER_TARGET GAME_TARGET)
  if(TARGET ${SHADER_TARGET} AND TARGET ${GAME_TARGET})
    get_target_property(_outdir ${SHADER_TARGET} CG_SHADER_OUTPUT_DIR)
    if(NOT _outdir)
      set(_outdir "${CMAKE_BINARY_DIR}/shaders")
    endif()
    add_dependencies(${GAME_TARGET} ${SHADER_TARGET})

    add_custom_command(TARGET ${GAME_TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory
              "$<TARGET_FILE_DIR:${GAME_TARGET}>/shaders"
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${_outdir}" "$<TARGET_FILE_DIR:${GAME_TARGET}>/shaders"
      COMMENT "Copying shaders to runtime directory"
      VERBATIM
    )

    install(DIRECTORY "${_outdir}/" DESTINATION "bin/shaders")
  endif()
endfunction()
