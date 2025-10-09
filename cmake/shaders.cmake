# cmake/shaders.cmake
# Windows-only thin wrapper that preserves the legacy cg_compile_hlsl() API
# and forwards to colony_add_hlsl() implemented in cmake/ColonyHLSL.cmake.
# No shader compiler flags or command-lines are duplicated here.

include_guard(GLOBAL)

if(NOT WIN32)
  message(STATUS "shaders.cmake: non-Windows generator detected; module is a no-op.")
  return()
endif()

# Ensure our cmake/ folder is on the CMake module path (usually set at the top level, but
# keeping this here makes the script robust when included directly).
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")

# Load the portable helper that implements colony_add_hlsl()
include(ColonyHLSL OPTIONAL RESULT_VARIABLE _colony_hlsl_loaded)
if(NOT _colony_hlsl_loaded OR NOT COMMAND colony_add_hlsl)
  message(WARNING
    "shaders.cmake: ColonyHLSL.cmake not found or incomplete; cg_compile_hlsl() unavailable.")
  return()
endif()

include(CMakeParseArguments)

# Usage:
#   cg_compile_hlsl(
#     NAME         <label>            # cosmetic, for logs/IDE only
#     SRC          <path/to/file.hlsl>
#     ENTRY        <entrypoint>       # default: main
#     PROFILE      <vs_5_0|ps_6_7>    # drives model + compiler selection
#     [INCLUDEDIRS <dir> ...]
#     [DEFINES     FOO=1 BAR ...]
#     OUTVAR       <var-to-receive-output-path>
#   )
#
# Effect:
#   - Registers an add_custom_command via colony_add_hlsl() that actually compiles the shader.
#   - Returns the expected output file path in OUTVAR so callers can create a target that
#     DEPENDS on it (as your top-level CMakeLists does for target 'shaders').
#
function(cg_compile_hlsl)
  set(options)
  set(oneValueArgs NAME SRC ENTRY PROFILE OUTVAR)
  set(multiValueArgs INCLUDEDIRS DEFINES)
  cmake_parse_arguments(CG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT CG_SRC)
    message(FATAL_ERROR "cg_compile_hlsl: SRC is required.")
  endif()
  if(NOT CG_OUTVAR)
    message(FATAL_ERROR "cg_compile_hlsl: OUTVAR is required.")
  endif()

  # Default entry
  if(NOT CG_ENTRY)
    set(CG_ENTRY "main")
  endif()

  # Derive shader model from PROFILE, e.g. vs_5_0 -> 5.0 ; ps_6_7 -> 6.7
  set(_model "5.0")
  if(CG_PROFILE)
    string(REGEX MATCH "_([0-9]+)_([0-9]+)" _m "${CG_PROFILE}")
    if(_m)
      set(_model "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}")
    endif()
  endif()

  # Pick compiler + object extension from PROFILE
  set(_compiler "AUTO")
  set(_ext ".cso")
  if(CG_PROFILE MATCHES "_6_")
    set(_compiler "DXC")
    set(_ext ".dxil")
  endif()

  # Where ColonyHLSL will emit objects/headers
  set(_outdir "${CMAKE_CURRENT_BINARY_DIR}/shaders")

  # Compute the output object path for this single source (so caller can DEPEND on it)
  get_filename_component(_stem "${CG_SRC}" NAME_WE)
  set(_outpath "${_outdir}/objects/${_stem}${_ext}")

  # Provide a dummy target to satisfy colony_add_hlsl()'s requirement that a target exists.
  # (In the non-VS path colony_add_hlsl() emits stand-alone custom commands; this target
  # is just a placeholder and is not built directly.)
  if(NOT TARGET colony_shader_legacy)
    add_custom_target(colony_shader_legacy)
  endif()

  # Delegate to the real implementation (no duplication here)
  colony_add_hlsl(colony_shader_legacy
    SOURCES  "${CG_SRC}"
    ENTRY    "${CG_ENTRY}"
    MODEL    "${_model}"
    OUTDIR   "${_outdir}"
    COMPILER "${_compiler}"
    EMIT     object
    DEFINES  ${CG_DEFINES}
    INCLUDES ${CG_INCLUDEDIRS}
  )

  # Hand back the expected output path
  set(${CG_OUTVAR} "${_outpath}" PARENT_SCOPE)
endfunction()
