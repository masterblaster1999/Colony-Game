# cmake/ShaderSetup.cmake
#
# Windows + Visual Studio generators only.
# Compiles HLSL files and drops .cso to $(OutDir)/<OUTPUT_SUBDIR>.
#
# Requires: CMake >= 3.12 for VS_SHADER_OBJECT_FILE_NAME.
#
# Usage (example):
#   include(ShaderSetup)
#   colony_compile_hlsl(
#     TARGET ColonyGame
#     DIR    "${CMAKE_SOURCE_DIR}/renderer/Shaders"
#     RECURSE
#     MODEL  5.0                 # use 6.0+ to switch VS to DXC automatically
#     OUTPUT_SUBDIR "shaders"
#     DEFINES "USE_FOG=1;PROFILE=1"
#     INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/renderer/Shaders/include"
#     ENTRYPOINT_MAP "Water.hlsl=PixelMain;Atmosphere.hlsl=PS"  # per-file overrides
#   )

function(colony_compile_hlsl)
  set(options RECURSE INFER_BY_SUFFIX)
  set(oneValueArgs TARGET DIR MODEL OUTPUT_SUBDIR)
  set(multiValueArgs FILES DEFINES INCLUDE_DIRS ENTRYPOINT_MAP)
  cmake_parse_arguments(COLONY "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT COLONY_TARGET)
    message(FATAL_ERROR "colony_compile_hlsl: TARGET is required")
  endif()

  # VS generator + MSVC only (Windows focus)
  if(NOT MSVC OR NOT CMAKE_GENERATOR MATCHES "Visual Studio")
    message(STATUS "colony_compile_hlsl: Skipped (requires Visual Studio generators on Windows).")
    return()
  endif()

  # Defaults
  if(NOT COLONY_MODEL)
    set(COLONY_MODEL "5.0")
  endif()
  if(NOT COLONY_OUTPUT_SUBDIR)
    set(COLONY_OUTPUT_SUBDIR "shaders")
  endif()
  if(NOT DEFINED COLONY_INFER_BY_SUFFIX)
    set(COLONY_INFER_BY_SUFFIX ON)
  endif()

  # Collect shader files
  set(HLSL_FILES)
  set(HLSLI_FILES)

  if(COLONY_DIR)
    if(COLONY_RECURSE)
      file(GLOB_RECURSE HLSL_FROM_DIR  "${COLONY_DIR}/*.hlsl")
      file(GLOB_RECURSE HLSLI_FROM_DIR "${COLONY_DIR}/*.hlsli")
    else()
      file(GLOB HLSL_FROM_DIR  "${COLONY_DIR}/*.hlsl")
      file(GLOB HLSLI_FROM_DIR "${COLONY_DIR}/*.hlsli")
    endif()
    list(APPEND HLSL_FILES  ${HLSL_FROM_DIR})
    list(APPEND HLSLI_FILES ${HLSLI_FROM_DIR})
  endif()

  if(COLONY_FILES)
    foreach(f IN LISTS COLONY_FILES)
      if(f MATCHES "\\.hlsli$")
        list(APPEND HLSLI_FILES "${f}")
      elseif(f MATCHES "\\.hlsl$")
        list(APPEND HLSL_FILES "${f}")
      endif()
    endforeach()
  endif()

  list(REMOVE_DUPLICATES HLSL_FILES)
  list(REMOVE_DUPLICATES HLSLI_FILES)

  # Never build includes
  if(HLSLI_FILES)
    set_source_files_properties(${HLSLI_FILES} PROPERTIES HEADER_FILE_ONLY ON)  # CMake property
  endif()

  # Build extra flags (-D / -I) for FXC/DXC via VS_SHADER_FLAGS
  set(_extra_flags "")
  foreach(def IN LISTS COLONY_DEFINES)
    string(APPEND _extra_flags " -D${def}")
  endforeach()
  foreach(inc IN LISTS COLONY_INCLUDE_DIRS)
    # Quote include path for spaces
    string(APPEND _extra_flags " -I\"${inc}\"")
  endforeach()
  string(STRIP "${_extra_flags}" _extra_flags)

  # Make sure destination exists before compilation starts
  # $(OutDir) maps to $<TARGET_FILE_DIR:...> for the active config
  add_custom_command(TARGET ${COLONY_TARGET} PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${COLONY_TARGET}>/${COLONY_OUTPUT_SUBDIR}"
  )

  # Apply per-file settings
  foreach(SHADER ${HLSL_FILES})
    get_filename_component(_name "${SHADER}" NAME)
    get_filename_component(_stem "${SHADER}" NAME_WE)
    string(TOLOWER "${_stem}" _stem_lower)

    # Infer shader type & default entrypoint
    set(_type "Pixel")    # sensible default
    set(_entry "PSMain")

    if(COLONY_INFER_BY_SUFFIX)
      if(_stem_lower MATCHES ".*([._-]vs)$")
        set(_type "Vertex")
        set(_entry "VSMain")
      elseif(_stem_lower MATCHES ".*([._-]ps)$")
        set(_type "Pixel")
        set(_entry "PSMain")
      elseif(_stem_lower MATCHES ".*([._-]cs)$")
        set(_type "Compute")
        set(_entry "CSMain")
      endif()
    endif()

    # Per-file entrypoint overrides like "Water.hlsl=PixelMain"
    foreach(map IN LISTS COLONY_ENTRYPOINT_MAP)
      if(map MATCHES "=")
        string(REPLACE "=" ";" _kv "${map}")
        list(GET _kv 0 _pat)
        list(GET _kv 1 _ep)
        if(_name MATCHES "${_pat}")
          set(_entry "${_ep}")
        endif()
      endif()
    endforeach()

    # Core VS HLSL properties
    set_source_files_properties("${SHADER}" PROPERTIES
      VS_SHADER_TYPE             "${_type}"         # Vertex/Pixel/Compute
      VS_SHADER_MODEL            "${COLONY_MODEL}"  # e.g., 5.0, 6.0, 6.6
      VS_SHADER_ENTRYPOINT       "${_entry}"
      VS_SHADER_OBJECT_FILE_NAME "$(OutDir)/${COLONY_OUTPUT_SUBDIR}/%(Filename).cso"
      VS_SHADER_ENABLE_DEBUG     "$<IF:$<CONFIG:Debug>,true,false>"
      VS_SHADER_DISABLE_OPTIMIZATIONS "$<IF:$<CONFIG:Debug>,true,false>"
    )

    if(_extra_flags)
      # Additional flags passed to FXC/DXC (e.g., -DNAME=VALUE, -Ipath)
      set_source_files_properties("${SHADER}" PROPERTIES VS_SHADER_FLAGS "${_extra_flags}")
    endif()
  endforeach()

  # Attach to the target so VS builds them
  target_sources(${COLONY_TARGET} PRIVATE ${HLSL_FILES} ${HLSLI_FILES})
endfunction()
