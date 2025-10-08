# cmake/ShaderSetup.cmake
#
# Windows + Visual Studio generators only.
# Compiles HLSL files and drops .cso to $(OutDir)/<OUTPUT_SUBDIR>.
#
# Requires:
#   - CMake >= 3.12 for VS_SHADER_OBJECT_FILE_NAME and other VS HLSL properties
#   - CMake >= 3.20 to use $<CONFIG> in add_custom_command(OUTPUT)
#
# Usage (example):
#   include(cmake/ShaderSetup.cmake)
#   colony_compile_hlsl(
#     TARGET ColonyGame
#     DIR    "${CMAKE_SOURCE_DIR}/renderer/Shaders"
#     RECURSE
#     MODEL  5.0                 # Use 6.0+ to switch Visual Studio to DXC automatically
#     OUTPUT_SUBDIR "shaders"
#     DEFINES "USE_FOG=1;PROFILE=1"
#     INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/renderer/Shaders/include"
#     ENTRYPOINT_MAP "Water.hlsl=PixelMain;Atmosphere.hlsl=PS"  # per-file overrides
#   )
#
# Notes:
#  - Call AFTER the target (add_executable/add_library) exists.
#  - No add_custom_command(TARGET ...) is used (avoids "TARGET was not created in this directory").
#  - A per-config stamp ensures $(OutDir)\OUTPUT_SUBDIR exists without creating
#    directory-scope cycles or cross-directory TARGET modifications.

function(colony_compile_hlsl)
  set(options RECURSE INFER_BY_SUFFIX)
  set(oneValueArgs TARGET DIR MODEL OUTPUT_SUBDIR)
  set(multiValueArgs FILES DEFINES INCLUDE_DIRS ENTRYPOINT_MAP)
  cmake_parse_arguments(COLONY "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT COLONY_TARGET)
    message(FATAL_ERROR "colony_compile_hlsl: TARGET is required")
  endif()
  if(NOT TARGET ${COLONY_TARGET})
    message(FATAL_ERROR
      "colony_compile_hlsl: TARGET '${COLONY_TARGET}' does not exist yet. "
      "Call after add_executable/add_library.")
  endif()

  # Windows-only: use MSVC + Visual Studio generators
  if(NOT MSVC OR NOT CMAKE_GENERATOR MATCHES "Visual Studio")
    message(STATUS "colony_compile_hlsl: Skipped (requires Visual Studio generators on Windows).")
    return()
  endif()

  # Defaults
  if(NOT COLONY_MODEL)
    set(COLONY_MODEL "5.0")
  endif()
  if(NOT DEFINED COLONY_OUTPUT_SUBDIR)
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
    set_source_files_properties(${HLSLI_FILES} PROPERTIES HEADER_FILE_ONLY ON)
  endif()

  # Build extra flags (-D / -I) for FXC/DXC via VS_SHADER_FLAGS
  set(_extra_flags "")
  foreach(def IN LISTS COLONY_DEFINES)
    if(NOT def STREQUAL "")
      string(APPEND _extra_flags " -D${def}")
    endif()
  endforeach()
  foreach(inc IN LISTS COLONY_INCLUDE_DIRS)
    if(NOT inc STREQUAL "")
      string(APPEND _extra_flags " -I\"${inc}\"")
    endif()
  endforeach()
  string(STRIP "${_extra_flags}" _extra_flags)

  # Ensure $(OutDir)/<subdir> exists per-config WITHOUT using add_custom_command(TARGET ...)
  # We create a configuration-specific stamp and attach it as a non-built source.
  if(NOT COLONY_OUTPUT_SUBDIR STREQUAL "")
    set(_stamp "${CMAKE_CURRENT_BINARY_DIR}/.${COLONY_TARGET}_${COLONY_OUTPUT_SUBDIR}_$<CONFIG>_shdir.stamp")
    add_custom_command(
      OUTPUT  "${_stamp}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "$(OutDir)/${COLONY_OUTPUT_SUBDIR}"
      COMMAND ${CMAKE_COMMAND} -E touch "${_stamp}"
      COMMENT "Ensuring '$(OutDir)/${COLONY_OUTPUT_SUBDIR}' exists"
      VERBATIM
    )
    target_sources(${COLONY_TARGET} PRIVATE "${_stamp}")
    set_source_files_properties("${_stamp}" PROPERTIES GENERATED TRUE HEADER_FILE_ONLY TRUE)
  endif()

  # Apply per-file settings and attach to target
  foreach(SHADER ${HLSL_FILES})
    get_filename_component(_name "${SHADER}" NAME)
    get_filename_component(_stem "${SHADER}" NAME_WE)
    string(TOLOWER "${_stem}" _stem_lower)

    # Infer shader type & default entrypoint
    set(_type "Pixel")    # default
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
      elseif(_stem_lower MATCHES ".*([._-]gs)$")
        set(_type "Geometry")
        set(_entry "GSMain")
      elseif(_stem_lower MATCHES ".*([._-]hs)$")
        set(_type "Hull")
        set(_entry "HSMain")
      elseif(_stem_lower MATCHES ".*([._-]ds)$")
        set(_type "Domain")
        set(_entry "DSMain")
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

    # Choose output path
    if(COLONY_OUTPUT_SUBDIR STREQUAL "")
      set(_out "$(OutDir)/%(Filename).cso")
    else()
      set(_out "$(OutDir)/${COLONY_OUTPUT_SUBDIR}/%(Filename).cso")
    endif()

    # Core VS HLSL properties
    set_source_files_properties("${SHADER}" PROPERTIES
      VS_SHADER_TYPE                  "${_type}"            # Vertex/Pixel/Compute/Geometry/Hull/Domain
      VS_SHADER_MODEL                 "${COLONY_MODEL}"     # e.g., 5.0, 6.0, 6.6
      VS_SHADER_ENTRYPOINT            "${_entry}"
      VS_SHADER_OBJECT_FILE_NAME      "${_out}"             # -Fo
      VS_SHADER_ENABLE_DEBUG          "$<IF:$<CONFIG:Debug>,true,false>"  # -Zi
      VS_SHADER_DISABLE_OPTIMIZATIONS "$<IF:$<CONFIG:Debug>,true,false>"  # -Od
    )

    if(_extra_flags)
      set_source_files_properties("${SHADER}" PROPERTIES VS_SHADER_FLAGS "${_extra_flags}")
    endif()
  endforeach()

  # Show in VS filters and attach sources
  if(HLSL_FILES)
    if(COLONY_DIR)
      source_group(TREE "${COLONY_DIR}" PREFIX "Shaders" FILES ${HLSL_FILES} ${HLSLI_FILES})
    endif()
    target_sources(${COLONY_TARGET} PRIVATE ${HLSL_FILES} ${HLSLI_FILES})
  endif()
endfunction()
