# cmake/Hlsl.cmake
# Per-shader compiler override for HLSL: DXC (SM 6.x) and FXC (SM 5.x) side-by-side.
# Usage summary (after add_executable()):
#   include(cmake/Hlsl.cmake)
#   cg_init_hlsl_compilers() # finds DXC/FXC, sets dirs
#   cg_set_target_shader_defaults(TARGET ColonyGame COMPILER AUTO DXC_SM 6_6 FXC_SM 5_0)
#   cg_compile_hlsl(TARGET ColonyGame SRC "${CG_SHADER_DIR}/Starfield.hlsl"
#                   STAGE vs ENTRY VSMain OUT "${CG_SHADER_BIN_DIR}/StarfieldVS.cso"
#                   COMPILER FXC PROFILE "vs_5_0")
#   cg_compile_hlsl(TARGET ColonyGame SRC "${CG_SHADER_DIR}/CS_SDFDither.hlsl"
#                   STAGE cs ENTRY main OUT "${CG_SHADER_BIN_DIR}/CS_SDFDither.cso"
#                   COMPILER DXC PROFILE "cs_6_6")
#   cg_stage_source("${CG_SHADER_DIR}/Terrain.hlsl")
#   cg_finalize_shader_target(TARGET ColonyGame)

cmake_minimum_required(VERSION 3.16)

include(CMakeParseArguments)

# Global defaults (can be overridden by caller)
option(USE_DXC "Global default: use DXC when found" ON)
set(DXC_SHADER_MODEL "6_0" CACHE STRING "Default SM for DXC (e.g., 6_0, 6_6)")
set(FXC_SHADER_MODEL "5_0" CACHE STRING "Default SM for FXC (e.g., 5_0, 5_1)")

# ------------------------------------------------------------------------------
# Initialize compiler discovery and shader dirs.
# Args (all optional):
#   SHADER_DIR <path>   default: ${CMAKE_SOURCE_DIR}/res/shaders
#   BIN_DIR    <path>   default: ${CMAKE_BINARY_DIR}/shaders
# ------------------------------------------------------------------------------
function(cg_init_hlsl_compilers)
  set(options)
  set(oneValueArgs SHADER_DIR BIN_DIR)
  set(multiValueArgs)
  cmake_parse_arguments(CG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT CG_SHADER_DIR)
    if(CG_SHADER_DIR)
      set(CG_SHADER_DIR "${CG_SHADER_DIR}" PARENT_SCOPE)
    else()
      set(CG_SHADER_DIR "${CMAKE_SOURCE_DIR}/res/shaders" PARENT_SCOPE)
    endif()
  endif()
  if(NOT CG_SHADER_BIN_DIR)
    if(CG_BIN_DIR)
      set(CG_SHADER_BIN_DIR "${CG_BIN_DIR}" PARENT_SCOPE)
    else()
      set(CG_SHADER_BIN_DIR "${CMAKE_BINARY_DIR}/shaders" PARENT_SCOPE)
    endif()
  endif()

  # Re‑export for function scope users
  set(CG_SHADER_DIR "${CG_SHADER_DIR}")
  set(CG_SHADER_BIN_DIR "${CG_SHADER_BIN_DIR}")
  file(MAKE_DIRECTORY "${CG_SHADER_BIN_DIR}")

  # ---- Find DXC (optional) ----
  find_program(DXC_BIN NAMES dxc dxc.exe
               HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/Bin64" "$ENV{VULKAN_SDK}/Bin32"
                     "$ENV{DXC_DIR}" "$ENV{DXC_PATH}")
  if(DXC_BIN)
    message(STATUS "DXC found: ${DXC_BIN}")
    set(CG_DXC_FOUND TRUE PARENT_SCOPE)
  else()
    set(CG_DXC_FOUND FALSE PARENT_SCOPE)
  endif()

  # ---- Find FXC (Windows SDK) ----
  find_program(FXC_BIN fxc.exe)
  if(FXC_BIN)
    message(STATUS "FXC found: ${FXC_BIN}")
    set(CG_FXC_FOUND TRUE PARENT_SCOPE)
  else()
    set(CG_FXC_FOUND FALSE PARENT_SCOPE)
  endif()

  if(NOT CG_DXC_FOUND AND NOT CG_FXC_FOUND)
    message(FATAL_ERROR "Neither DXC nor FXC found. Install Windows SDK for FXC or provide DXC.")
  endif()
endfunction()

# ------------------------------------------------------------------------------
# Set per-target defaults used by cg_compile_hlsl when COMPILER/PROFILE omitted.
# COMPILER: AUTO|DXC|FXC     DXC_SM: e.g. 6_6     FXC_SM: e.g. 5_0
# ------------------------------------------------------------------------------
function(cg_set_target_shader_defaults)
  set(options)
  set(oneValueArgs TARGET COMPILER DXC_SM FXC_SM)
  set(multiValueArgs)
  cmake_parse_arguments(CG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT CG_TARGET)
    message(FATAL_ERROR "cg_set_target_shader_defaults: TARGET is required")
  endif()

  if(NOT CG_COMPILER)
    set(CG_COMPILER "AUTO")
  endif()
  if(NOT CG_DXC_SM)
    set(CG_DXC_SM "${DXC_SHADER_MODEL}")
  endif()
  if(NOT CG_FXC_SM)
    set(CG_FXC_SM "${FXC_SHADER_MODEL}")
  endif()

  set_property(TARGET ${CG_TARGET} PROPERTY CG_SHADER_COMPILER_DEFAULT "${CG_COMPILER}")
  set_property(TARGET ${CG_TARGET} PROPERTY CG_DXC_SM_DEFAULT "${CG_DXC_SM}")
  set_property(TARGET ${CG_TARGET} PROPERTY CG_FXC_SM_DEFAULT "${CG_FXC_SM}")
endfunction()

# --- internal: append output to target property list
function(_cg_append_shader_output tgt out)
  get_property(_curr TARGET ${tgt} PROPERTY CG_SHADER_OUTPUTS)
  if(NOT _curr)
    set(_curr "")
  endif()
  list(APPEND _curr "${out}")
  set_property(TARGET ${tgt} PROPERTY CG_SHADER_OUTPUTS "${_curr}")
endfunction()

# ------------------------------------------------------------------------------
# Compile a single HLSL shader with per-shader overrides.
# Required:
#   TARGET <tgt>   SRC <file>   STAGE <vs|ps|cs|gs|hs|ds>   ENTRY <name>   OUT <file.cso>
# Optional:
#   COMPILER <AUTO|DXC|FXC>   PROFILE <vs_6_6 / ps_5_0 / ...>
#   DEFINES <list...> INCLUDES <list...>
#   FLAGS_DXC <list...> FLAGS_FXC <list...>
# ------------------------------------------------------------------------------
function(cg_compile_hlsl)
  set(options)
  set(oneValueArgs TARGET SRC STAGE ENTRY OUT COMPILER PROFILE)
  set(multiValueArgs DEFINES INCLUDES FLAGS_DXC FLAGS_FXC)
  cmake_parse_arguments(CG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT CG_TARGET OR NOT CG_SRC OR NOT CG_STAGE OR NOT CG_ENTRY OR NOT CG_OUT)
    message(FATAL_ERROR "cg_compile_hlsl: TARGET, SRC, STAGE, ENTRY, OUT are required")
  endif()

  # Resolve global dirs set by cg_init_hlsl_compilers()
  if(NOT DEFINED CG_SHADER_DIR)
    set(CG_SHADER_DIR "${CMAKE_SOURCE_DIR}/res/shaders")
  endif()
  if(NOT DEFINED CG_SHADER_BIN_DIR)
    set(CG_SHADER_BIN_DIR "${CMAKE_BINARY_DIR}/shaders")
  endif()
  file(MAKE_DIRECTORY "${CG_SHADER_BIN_DIR}")

  # Determine compiler choice
  get_property(_tgt_default TARGET ${CG_TARGET} PROPERTY CG_SHADER_COMPILER_DEFAULT)
  if(NOT _tgt_default)
    set(_tgt_default "AUTO")
  endif()
  if(NOT CG_COMPILER)
    set(CG_COMPILER "${_tgt_default}")
  endif()

  # Determine DXC/FXC availability
  if(NOT DEFINED CG_DXC_FOUND)
    # Fallback if user forgot cg_init_hlsl_compilers()
    find_program(DXC_BIN NAMES dxc dxc.exe)
    set(CG_DXC_FOUND ${DXC_BIN})
  endif()
  if(NOT DEFINED CG_FXC_FOUND)
    find_program(FXC_BIN fxc.exe)
    set(CG_FXC_FOUND ${FXC_BIN})
  endif()

  # Choose effective compiler
  set(_use_dxc FALSE)
  if(CG_COMPILER STREQUAL "DXC")
    set(_use_dxc TRUE)
  elseif(CG_COMPILER STREQUAL "FXC")
    set(_use_dxc FALSE)
  else() # AUTO
    if(USE_DXC AND CG_DXC_FOUND)
      set(_use_dxc TRUE)
    else()
      set(_use_dxc FALSE)
    endif()
  endif()

  # Compute default profiles when not explicitly provided
  if(NOT CG_PROFILE)
    if(_use_dxc)
      get_property(_sm TARGET ${CG_TARGET} PROPERTY CG_DXC_SM_DEFAULT)
      if(NOT _sm) # fallback to cache default
        set(_sm "${DXC_SHADER_MODEL}")
      endif()
      set(CG_PROFILE "${CG_STAGE}_${_sm}")
    else()
      get_property(_sm TARGET ${CG_TARGET} PROPERTY CG_FXC_SM_DEFAULT)
      if(NOT _sm)
        set(_sm "${FXC_SHADER_MODEL}")
      endif()
      set(CG_PROFILE "${CG_STAGE}_${_sm}")
    endif()
  endif()

  # Build define/include flags for each compiler
  set(_fxc_defs "")
  set(_dxc_defs "")
  foreach(D IN LISTS CG_DEFINES)
    list(APPEND _fxc_defs "/D" "${D}")
    list(APPEND _dxc_defs "-D${D}")
  endforeach()

  set(_fxc_inc "")
  set(_dxc_inc "")
  foreach(I IN LISTS CG_INCLUDES)
    list(APPEND _fxc_inc "/I" "${I}")
    list(APPEND _dxc_inc "-I" "${I}")
  endforeach()
  # Always include the shader dir
  list(APPEND _fxc_inc "/I" "${CG_SHADER_DIR}")
  list(APPEND _dxc_inc "-I" "${CG_SHADER_DIR}")

  if(_use_dxc)
    if(NOT DXC_BIN)
      find_program(DXC_BIN NAMES dxc dxc.exe)
    endif()
    if(NOT DXC_BIN)
      message(FATAL_ERROR "DXC requested but not found")
    endif()
    add_custom_command(
      OUTPUT "${CG_OUT}"
      COMMAND "${DXC_BIN}" -nologo -T "${CG_PROFILE}" -E "${CG_ENTRY}"
              -Fo "${CG_OUT}" -O3 -Qstrip_reflect -Qstrip_debug
              ${_dxc_defs} ${_dxc_inc} ${CG_FLAGS_DXC}
              "${CG_SRC}"
      DEPENDS "${CG_SRC}"
      WORKING_DIRECTORY "${CG_SHADER_DIR}"
      COMMENT "DXC ${CG_SRC} (${CG_ENTRY}/${CG_PROFILE}) -> ${CG_OUT}"
      VERBATIM
    )
  else()
    if(NOT FXC_BIN)
      find_program(FXC_BIN fxc.exe)
    endif()
    if(NOT FXC_BIN)
      message(FATAL_ERROR "FXC requested but not found")
    endif()
    add_custom_command(
      OUTPUT "${CG_OUT}"
      COMMAND "${FXC_BIN}" /nologo /T "${CG_PROFILE}" /E "${CG_ENTRY}"
              /Fo "${CG_OUT}" /O3 /Qstrip_reflect /Qstrip_debug
              ${_fxc_defs} ${_fxc_inc} ${CG_FLAGS_FXC}
              "${CG_SRC}"
      DEPENDS "${CG_SRC}"
      WORKING_DIRECTORY "${CG_SHADER_DIR}"
      COMMENT "FXC ${CG_SRC} (${CG_ENTRY}/${CG_PROFILE}) -> ${CG_OUT}"
      VERBATIM
    )
  endif()

  # Record output against the target for finalization
  _cg_append_shader_output(${CG_TARGET} "${CG_OUT}")
endfunction()

# ------------------------------------------------------------------------------
# Stage (copy) a raw shader source into the shader bin dir (for runtime compilation).
# ------------------------------------------------------------------------------
function(cg_stage_source SRC)
  if(NOT DEFINED CG_SHADER_BIN_DIR)
    set(CG_SHADER_BIN_DIR "${CMAKE_BINARY_DIR}/shaders")
  endif()
  get_filename_component(_base "${SRC}" NAME)
  set(_dst "${CG_SHADER_BIN_DIR}/${_base}")
  add_custom_command(
    OUTPUT "${_dst}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SRC}" "${_dst}"
    DEPENDS "${SRC}"
    COMMENT "Staging shader source: ${_base}"
    VERBATIM
  )
  # Group into a project-wide target so it gets built by default
  if(NOT TARGET cg_staged_sources)
    add_custom_target(cg_staged_sources ALL DEPENDS "${_dst}")
  else()
    add_dependencies(cg_staged_sources "${_dst}")
  endif()
endfunction()

# ------------------------------------------------------------------------------
# Create a per-target aggregation target and copy shaders next to the EXE.
# ------------------------------------------------------------------------------
function(cg_finalize_shader_target)
  set(options)
  set(oneValueArgs TARGET)
  set(multiValueArgs)
  cmake_parse_arguments(CG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT CG_TARGET)
    message(FATAL_ERROR "cg_finalize_shader_target: TARGET is required")
  endif()

  get_property(_outs TARGET ${CG_TARGET} PROPERTY CG_SHADER_OUTPUTS)
  if(_outs)
    add_custom_target(shaders_${CG_TARGET} ALL DEPENDS ${_outs})
    add_dependencies(${CG_TARGET} shaders_${CG_TARGET})

    if(NOT DEFINED CG_SHADER_BIN_DIR)
      set(CG_SHADER_BIN_DIR "${CMAKE_BINARY_DIR}/shaders")
    endif()

    add_custom_command(TARGET ${CG_TARGET} POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${CG_TARGET}>/shaders"
      COMMAND "${CMAKE_COMMAND}" -E copy_directory
              "${CG_SHADER_BIN_DIR}" "$<TARGET_FILE_DIR:${CG_TARGET}>/shaders"
      COMMENT "Copying shaders to runtime dir for ${CG_TARGET}"
      VERBATIM
    )
  endif()
endfunction()
