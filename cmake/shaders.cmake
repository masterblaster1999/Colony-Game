# cmake/shaders.cmake  (Windows Visual Studio HLSL integration)
include_guard(GLOBAL)

if(NOT WIN32)
  message(FATAL_ERROR "This shaders.cmake is Windows-only by design.")
endif()

# Note: The VS HLSL source properties below are honored by the
# Visual Studio generators. Other generators may ignore them.
if (NOT CMAKE_GENERATOR MATCHES "Visual Studio")
  message(STATUS
    "shaders.cmake: Visual Studio generator not detected. "
    "VS_SHADER_* properties may be ignored by this generator.")
endif()

# Default object (.cso) output path for shaders:
# $(IntDir) is per-config (e.g. build/<cfg>/). %(Filename) is the VS item macro.
set(COLONY_DEFAULT_SHADER_OBJECT "$(IntDir)%(Filename).cso")

# ---------------------------------------------------------------------------
# cg_add_hlsl
#
# Register a single HLSL source file on a target and set the Visual Studio
# HLSL properties so VS compiles it during the normal C++ build.
#
# Required:
#   TARGET <target>     -- existing CMake target (executable or library)
#   SOURCE <file.hlsl>  -- shader source file
#   TYPE   <Vertex|Pixel|Geometry|Compute|Domain|Hull|Mesh|Amplification>
#   ENTRY  <entrypoint> -- e.g. 'main'
#   MODEL  <5.0|5.1|6.0|6.6|...> -- shader model; VS picks FXC (≤5.1) or DXC (6.x)
#
# Optional:
#   DEFINES       name[=value] ...     -- adds /D for each
#   INCLUDE_DIRS  dir1 dir2 ...        -- adds /I"dir" for each
#   FLAGS         <raw additional options passed to the shader compiler>
#   OUTPUT        <custom .cso path>   -- defaults to $(IntDir)%(Filename).cso
#
# Example:
#   cg_add_hlsl(TARGET MyGame SOURCE shaders/lighting_ps.hlsl
#               TYPE Pixel ENTRY main MODEL 6.6
#               INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/shaders/inc"
#               DEFINES "USE_PCF=1" FLAGS "/Ges")
#
# Properties used (CMake):
#   VS_SHADER_TYPE, VS_SHADER_ENTRYPOINT, VS_SHADER_MODEL, VS_SHADER_OBJECT_FILE_NAME
# ---------------------------------------------------------------------------
function(cg_add_hlsl)
  set(options)
  set(oneValueArgs TARGET SOURCE TYPE ENTRY MODEL OUTPUT)
  set(multiValueArgs DEFINES INCLUDE_DIRS FLAGS)
  cmake_parse_arguments(CG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if (NOT CG_TARGET)
    message(FATAL_ERROR "cg_add_hlsl: TARGET is required.")
  endif()
  if (NOT TARGET ${CG_TARGET})
    message(FATAL_ERROR "cg_add_hlsl: TARGET '${CG_TARGET}' does not exist.")
  endif()
  if (NOT CG_SOURCE)
    message(FATAL_ERROR "cg_add_hlsl: SOURCE is required.")
  endif()
  if (NOT EXISTS "${CG_SOURCE}")
    message(FATAL_ERROR "cg_add_hlsl: SOURCE not found: ${CG_SOURCE}")
  endif()
  if (NOT CG_TYPE)
    message(FATAL_ERROR "cg_add_hlsl: TYPE is required (Vertex|Pixel|Geometry|Compute|Domain|Hull|Mesh|Amplification).")
  endif()
  if (NOT CG_ENTRY)
    message(FATAL_ERROR "cg_add_hlsl: ENTRY is required (e.g. 'main').")
  endif()
  if (NOT CG_MODEL)
    message(FATAL_ERROR "cg_add_hlsl: MODEL is required (e.g. 5.0, 6.6).")
  endif()

  # Add the shader file to the target's sources so VS knows to build it.
  target_sources(${CG_TARGET} PRIVATE "${CG_SOURCE}")

  # Core Visual Studio HLSL properties (CMake source file properties).
  # Docs: VS_SHADER_TYPE, VS_SHADER_ENTRYPOINT, VS_SHADER_MODEL, VS_SHADER_OBJECT_FILE_NAME
  set_source_files_properties("${CG_SOURCE}" PROPERTIES
    VS_SHADER_TYPE       "${CG_TYPE}"
    VS_SHADER_ENTRYPOINT "${CG_ENTRY}"
    VS_SHADER_MODEL      "${CG_MODEL}"
  )

  # Output object (.cso): either user-supplied or default to $(IntDir)%(Filename).cso
  if (CG_OUTPUT)
    set(_shader_out "${CG_OUTPUT}")
  else()
    set(_shader_out "${COLONY_DEFAULT_SHADER_OBJECT}")
  endif()
  set_source_files_properties("${CG_SOURCE}" PROPERTIES
    VS_SHADER_OBJECT_FILE_NAME "${_shader_out}"
  )

  # Compose additional flags: /I "dir" and /D NAME[=VAL], plus any raw FLAGS.
  set(_extra_flags "")
  foreach(_inc IN LISTS CG_INCLUDE_DIRS)
    # Quote to handle spaces in paths.
    string(APPEND _extra_flags " /I\"${_inc}\"")
  endforeach()
  foreach(_def IN LISTS CG_DEFINES)
    string(APPEND _extra_flags " /D${_def}")
  endforeach()
  foreach(_f IN LISTS CG_FLAGS)
    string(APPEND _extra_flags " ${_f}")
  endforeach()

  if (_extra_flags)
    # VS_SHADER_FLAGS adds options to the HLSL compiler invocation.
    set_source_files_properties("${CG_SOURCE}" PROPERTIES
      VS_SHADER_FLAGS "${_extra_flags}"
    )
  endif()

  # Friendly status for configure logs.
  get_filename_component(_srcname "${CG_SOURCE}" NAME)
  message(STATUS "HLSL (VS) -> ${CG_TARGET}: ${_srcname}  [${CG_TYPE}, SM ${CG_MODEL}, entry=${CG_ENTRY}]")
endfunction()

# ---------------------------------------------------------------------------
# NOTE:
# Previous custom macro 'cg_compile_hlsl(...)' (which shell-called FXC) has
# been removed in favor of Visual Studio's native HLSL integration via
# source file properties. If you still have calls to cg_compile_hlsl in
# your build, replace them with either:
#
#  1) Direct properties (no helper):
#     target_sources(ColonyGame PRIVATE "${CMAKE_SOURCE_DIR}/shaders/terrain_ps.hlsl")
#     set_source_files_properties("${CMAKE_SOURCE_DIR}/shaders/terrain_ps.hlsl" PROPERTIES
#       VS_SHADER_TYPE Pixel
#       VS_SHADER_ENTRYPOINT main
#       VS_SHADER_MODEL 5.0
#       VS_SHADER_OBJECT_FILE_NAME "$(IntDir)%(Filename).cso")
#
#  2) Or the helper function 'cg_add_hlsl(...)' defined above.
# ---------------------------------------------------------------------------
