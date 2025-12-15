# cmake/CGShadersPipeline.cmake
#
# Centralizes all shader wiring for ColonyGame:
# - If an external/top-level shader pipeline is present (cg_compile_hlsl / colony_register_shaders),
#   we only add shader files to the IDE as HEADER_FILE_ONLY and do not compile/copy here.
# - Otherwise:
#   - Visual Studio generator: configure VS_SHADER_* properties (FXC via VS integration, SM5.0).
#   - Non-VS generators: try to include ColonyShaders.cmake + compile via shaders.json + DXC pipeline.
#
# Call:
#   include(${COLONY_ROOT_DIR}/cmake/CGShadersPipeline.cmake)
#   cg_setup_shaders_pipeline(
#     TARGET ColonyGame
#     ROOT_DIR "${COLONY_ROOT_DIR}"
#     ENABLE_COMPUTE_SHADERS ${COLONY_ENABLE_COMPUTE_SHADERS}
#   )

include_guard(GLOBAL)

# Helper for Visual Studio shader properties
function(_cg_vs_configure_shader file vs_outdir inc_flags_str)
  get_filename_component(_we "${file}" NAME_WE)

  set(_type "")
  if(_we MATCHES "_vs$")
    set(_type "Vertex")
  elseif(_we MATCHES "_ps$")
    set(_type "Pixel")
  elseif(_we MATCHES "_cs$")
    set(_type "Compute")
  else()
    return()
  endif()

  # Read the shader file to detect whether VSMain/PSMain/CSMain exist.
  file(READ "${file}" _hlsl_src)

  set(_entry "main")
  if(_type STREQUAL "Vertex")
    string(FIND "${_hlsl_src}" "VSMain(" _hit)
    if(NOT _hit EQUAL -1)
      set(_entry "VSMain")
    endif()
  elseif(_type STREQUAL "Pixel")
    string(FIND "${_hlsl_src}" "PSMain(" _hit)
    if(NOT _hit EQUAL -1)
      set(_entry "PSMain")
    endif()
  elseif(_type STREQUAL "Compute")
    string(FIND "${_hlsl_src}" "CSMain(" _hit)
    if(NOT _hit EQUAL -1)
      set(_entry "CSMain")
    endif()
  endif()

  set_source_files_properties("${file}" PROPERTIES
    VS_SHADER_TYPE                  "${_type}"
    VS_SHADER_MODEL                 "5.0"
    VS_SHADER_ENTRYPOINT            "${_entry}"
    VS_SHADER_OBJECT_FILE_NAME      "${vs_outdir}\\%(Filename).cso"
    VS_SHADER_ENABLE_DEBUG          "$<CONFIG:Debug>"
    VS_SHADER_DISABLE_OPTIMIZATIONS "$<CONFIG:Debug>"
    VS_SHADER_FLAGS                 "${inc_flags_str}"
  )

  unset(_we)
  unset(_type)
  unset(_entry)
  unset(_hlsl_src)
  unset(_hit)
endfunction()

function(cg_setup_shaders_pipeline)
  cmake_parse_arguments(ARG "" "TARGET;ROOT_DIR;ENABLE_COMPUTE_SHADERS" "" ${ARGN})

  if(NOT ARG_TARGET)
    message(FATAL_ERROR "cg_setup_shaders_pipeline: TARGET is required.")
  endif()

  set(_tgt "${ARG_TARGET}")

  if(ARG_ROOT_DIR)
    set(_root "${ARG_ROOT_DIR}")
  else()
    get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
  endif()

  # Default OFF if not provided
  set(_enable_cs OFF)
  if(DEFINED ARG_ENABLE_COMPUTE_SHADERS)
    set(_enable_cs "${ARG_ENABLE_COMPUTE_SHADERS}")
  endif()

  # If a top-level shader pipeline is already present (e.g. CGShaders / colony_register_shaders),
  # avoid also configuring VS_SHADER_* or DXC manifest here to prevent double-compilation and mismatches.
  set(_externally_handled FALSE)
  if(COMMAND cg_compile_hlsl OR COMMAND colony_register_shaders)
    set(_externally_handled TRUE)
  endif()

  if(_externally_handled)
    # Add shaders to the IDE (header-only) but let the external pipeline do compilation/copying.
    file(GLOB_RECURSE _ALL_SHADERS CONFIGURE_DEPENDS
      "${_root}/renderer/Shaders/*.hlsl"
      "${_root}/renderer/Shaders/*.hlsli"
      "${_root}/shaders/*.hlsl"
      "${_root}/shaders/*.hlsli"
    )
    if(_ALL_SHADERS)
      target_sources(${_tgt} PRIVATE ${_ALL_SHADERS})
      set_source_files_properties(${_ALL_SHADERS} PROPERTIES HEADER_FILE_ONLY ON)
      source_group(TREE "${_root}" PREFIX "Shaders" FILES ${_ALL_SHADERS})
    endif()
    unset(_ALL_SHADERS)
    return()
  endif()

  # ---------------- Visual Studio generator path (VS_SHADER_* / FXC) ----------------
  if(CMAKE_GENERATOR MATCHES "Visual Studio")
    set(HLSL_DIR        "${_root}/renderer/Shaders")
    set(HLSL_INC_PRIMARY "${HLSL_DIR}/include")
    set(HLSL_INC_ALT     "${_root}/shaders/include")
    set(_VS_OUTDIR       "$(OutDir)res\\shaders")

    # Vertex/Pixel always — only compile real entrypoint files by suffix
    file(GLOB_RECURSE HLSL_SOURCES CONFIGURE_DEPENDS
      "${HLSL_DIR}/*_vs.hlsl"
      "${HLSL_DIR}/*_ps.hlsl"
    )

    # Compute only when enabled (prevents X3501 if CSMain isn’t present yet)
    set(HLSL_LIBRARY "")
    file(GLOB_RECURSE HLSL_CS_ALL CONFIGURE_DEPENDS "${HLSL_DIR}/*_cs.hlsl")
    if(_enable_cs)
      list(APPEND HLSL_SOURCES ${HLSL_CS_ALL})
    else()
      set(HLSL_LIBRARY ${HLSL_CS_ALL})
    endif()

    file(GLOB_RECURSE HLSL_INCLUDES CONFIGURE_DEPENDS "${HLSL_DIR}/*.hlsli")

    if(HLSL_SOURCES)
      target_sources(${_tgt} PRIVATE ${HLSL_SOURCES})
    endif()

    if(HLSL_LIBRARY)
      target_sources(${_tgt} PRIVATE ${HLSL_LIBRARY})
      set_source_files_properties(${HLSL_LIBRARY} PROPERTIES HEADER_FILE_ONLY ON)
    endif()

    if(HLSL_INCLUDES)
      target_sources(${_tgt} PRIVATE ${HLSL_INCLUDES})
      set_source_files_properties(${HLSL_INCLUDES} PROPERTIES HEADER_FILE_ONLY ON)
    endif()

    source_group("Shaders\\Sources"  FILES ${HLSL_SOURCES})
    source_group("Shaders\\Library"  FILES ${HLSL_LIBRARY})
    source_group("Shaders\\Includes" FILES ${HLSL_INCLUDES})

    # Build a safe include flag string for FXC (avoid semicolons in VS_SHADER_FLAGS).
    set(_HLSL_INC_FLAGS_LIST "")
    if(EXISTS "${HLSL_INC_PRIMARY}")
      list(APPEND _HLSL_INC_FLAGS_LIST "/I\"${HLSL_INC_PRIMARY}\"")
    endif()
    if(EXISTS "${HLSL_INC_ALT}")
      list(APPEND _HLSL_INC_FLAGS_LIST "/I\"${HLSL_INC_ALT}\"")
    endif()

    if(_HLSL_INC_FLAGS_LIST)
      string(JOIN " " _HLSL_INC_FLAGS_STR ${_HLSL_INC_FLAGS_LIST})
    else()
      set(_HLSL_INC_FLAGS_STR "")
    endif()

    foreach(_sh IN LISTS HLSL_SOURCES)
      _cg_vs_configure_shader("${_sh}" "${_VS_OUTDIR}" "${_HLSL_INC_FLAGS_STR}")
    endforeach()

    unset(HLSL_DIR)
    unset(HLSL_INC_PRIMARY)
    unset(HLSL_INC_ALT)
    unset(_VS_OUTDIR)
    unset(_HLSL_INC_FLAGS_LIST)
    unset(_HLSL_INC_FLAGS_STR)
    unset(HLSL_SOURCES)
    unset(HLSL_LIBRARY)
    unset(HLSL_CS_ALL)
    unset(HLSL_INCLUDES)
    unset(_sh)

    return()
  endif()

  # ---------------- Non-VS generators: DXC via manifest if available ----------------
  include("${_root}/cmake/ColonyShaders.cmake" OPTIONAL RESULT_VARIABLE _colony_shaders_included)

  set(_COLONY_MANIFEST "")
  if(EXISTS "${_root}/renderer/Shaders/shaders.json")
    set(_COLONY_MANIFEST "${_root}/renderer/Shaders/shaders.json")
  elseif(EXISTS "${_root}/shaders/shaders.json")
    set(_COLONY_MANIFEST "${_root}/shaders/shaders.json")
  endif()

  if(_COLONY_MANIFEST AND COMMAND colony_register_shaders)
    set(_COLONY_SHADER_OUT "${CMAKE_BINARY_DIR}/shaders")

    colony_register_shaders(
      TARGET       ${_tgt}
      MANIFEST     "${_COLONY_MANIFEST}"
      OUTPUT_DIR   "${_COLONY_SHADER_OUT}"
      INCLUDE_DIRS
        "${_root}/renderer/Shaders"
        "${_root}/renderer/Shaders/include"
        "${_root}/shaders"
        "${_root}/shaders/include"
      DXC_ARGS     -nologo
    )

    if(COMMAND colony_install_shaders)
      colony_install_shaders(TARGET ${_tgt} DESTINATION bin/shaders)
    endif()

    add_custom_command(TARGET ${_tgt} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${_tgt}>/res/shaders"
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${CMAKE_BINARY_DIR}/shaders/$<CONFIG>"
              "$<TARGET_FILE_DIR:${_tgt}>/res/shaders"
      VERBATIM
    )
  else()
    message(STATUS "No shaders.json manifest found; DXC shader registration skipped.")
  endif()

  unset(_colony_shaders_included)
  unset(_COLONY_MANIFEST)
  unset(_COLONY_SHADER_OUT)
endfunction()
