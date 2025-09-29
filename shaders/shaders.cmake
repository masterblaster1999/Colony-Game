# cmake/shaders.cmake
# ------------------------------------------------------------------------------
# Compiles HLSL to .cso with DXC (preferred) or FXC (fallback) and provides a
# custom target 'shaders' that other targets can depend on.
#
# - Source dir (override if needed):
#     CG_SHADERS_DIR     (default: ${CMAKE_SOURCE_DIR}/shaders)
# - Output dir:
#     CG_SHADERS_OUT_DIR (default: ${CMAKE_BINARY_DIR}/shaders)
# - Prefer DXC over FXC:
#     SHADERS_USE_DXC (default: ON)
# - Shader model (D3D11 default):
#     CG_HLSL_SHADER_MODEL (default: 5_0)
#
# Overrides:
#   Optionally create shaders/manifest.cmake and register ambiguous shaders:
#     cg_add_hlsl(FILE <path|relative-to-shaders>
#                 STAGE compute|vertex|pixel|geometry|hull|domain
#                 [ENTRY <EntryPoint>]
#                 [OUTPUT <basename-without-ext>])
# ------------------------------------------------------------------------------

if(DEFINED CG_SHADERS_CMAKE_INCLUDED)
  return()
endif()
set(CG_SHADERS_CMAKE_INCLUDED 1)

if(NOT WIN32)
  message(STATUS "[shaders] Non-Windows platform detected; skipping HLSL compilation.")
  return()
endif()

# ------------------------------------------------------------------------------ #
# Configuration
# ------------------------------------------------------------------------------ #
set(CG_SHADERS_DIR     "${CMAKE_SOURCE_DIR}/shaders" CACHE PATH "Directory containing .hlsl files")
set(CG_SHADERS_OUT_DIR "${CMAKE_BINARY_DIR}/shaders" CACHE PATH "Output directory for compiled .cso")
set(CG_HLSL_SHADER_MODEL "5_0" CACHE STRING "Shader model (e.g. 5_0 for D3D11)")
option(SHADERS_USE_DXC "Prefer DXC over FXC" ON)

# ------------------------------------------------------------------------------ #
# Locate DXC / FXC
# ------------------------------------------------------------------------------ #
if(SHADERS_USE_DXC)
  find_program(DXC_EXE NAMES dxc dxc.exe
    PATHS
      "$ENV{VCPKG_INSTALLATION_ROOT}/installed/x64-windows/tools/dxc"
      "$ENV{VCPKG_ROOT}/installed/x64-windows/tools/dxc"
      "C:/Program Files (x86)/Windows Kits/10/bin/x64"
      "C:/Program Files/Windows Kits/10/bin/x64"
    NO_DEFAULT_PATH
  )
  if(NOT DXC_EXE)
    # Also check PATH
    find_program(DXC_EXE NAMES dxc dxc.exe)
  endif()
endif()

find_program(FXC_EXE NAMES fxc fxc.exe
  PATHS
    "$ENV{VCPKG_INSTALLATION_ROOT}/installed/x64-windows/tools/fxc"
    "$ENV{VCPKG_ROOT}/installed/x64-windows/tools/fxc"
    "C:/Program Files (x86)/Windows Kits/10/bin/x64"
    "C:/Program Files/Windows Kits/10/bin/x64"
  NO_DEFAULT_PATH
)
if(NOT FXC_EXE)
  find_program(FXC_EXE NAMES fxc fxc.exe)
endif()

if(DXC_EXE)
  set(_HLSL_COMPILER "DXC")
elseif(FXC_EXE)
  set(_HLSL_COMPILER "FXC")
else()
  message(WARNING "[shaders] Neither DXC nor FXC was found. The 'shaders' target will be empty.")
  return()
endif()
message(STATUS "[shaders] Using ${_HLSL_COMPILER}")

# ------------------------------------------------------------------------------ #
# Helpers
# ------------------------------------------------------------------------------ #
function(_cg_profile_for_stage STAGE OUT_VAR)
  string(TOLOWER "${STAGE}" st)
  if(st STREQUAL "vertex")
    set(p "vs_${CG_HLSL_SHADER_MODEL}")
  elseif(st STREQUAL "pixel")
    set(p "ps_${CG_HLSL_SHADER_MODEL}")
  elseif(st STREQUAL "geometry")
    set(p "gs_${CG_HLSL_SHADER_MODEL}")
  elseif(st STREQUAL "hull")
    set(p "hs_${CG_HLSL_SHADER_MODEL}")
  elseif(st STREQUAL "domain")
    set(p "ds_${CG_HLSL_SHADER_MODEL}")
  elseif(st STREQUAL "compute")
    set(p "cs_${CG_HLSL_SHADER_MODEL}")
  else()
    set(p "")
  endif()
  set(${OUT_VAR} "${p}" PARENT_SCOPE)
endfunction()

function(_cg_guess_stage_and_entry IN_FILE OUT_STAGE OUT_ENTRY)
  get_filename_component(_name_we "${IN_FILE}" NAME_WE)
  get_filename_component(_dir "${IN_FILE}" DIRECTORY)
  string(TOLOWER "${_name_we}" n)
  string(TOLOWER "${_dir}" d)

  set(stage "")
  set(entry "")

  if(n MATCHES "(^|[_\\-/])(cs|compute)([_\\-/]|$)" OR d MATCHES "([/\\\\])compute([/\\\\])")
    set(stage "compute")  ; set(entry "CSMain")
  elseif(n MATCHES "(^|[_\\-/])(ps|pixel)([_\\-/]|$)" OR d MATCHES "([/\\\\])pixel(s)?([/\\\\])")
    set(stage "pixel")    ; set(entry "PSMain")
  elseif(n MATCHES "(^|[_\\-/])(vs|vertex)([_\\-/]|$)" OR d MATCHES "([/\\\\])vertex([/\\\\])")
    set(stage "vertex")   ; set(entry "VSMain")
  elseif(n MATCHES "(^|[_\\-/])(gs|geom|geometry)([_\\-/]|$)" OR d MATCHES "([/\\\\])geometry([/\\\\])")
    set(stage "geometry") ; set(entry "GSMain")
  elseif(n MATCHES "(^|[_\\-/])(hs|hull)([_\\-/]|$)" OR d MATCHES "([/\\\\])hull([/\\\\])")
    set(stage "hull")     ; set(entry "HSMain")
  elseif(n MATCHES "(^|[_\\-/])(ds|domain)([_\\-/]|$)" OR d MATCHES "([/\\\\])domain([/\\\\])")
    set(stage "domain")   ; set(entry "DSMain")
  endif()

  set(${OUT_STAGE} "${stage}" PARENT_SCOPE)
  set(${OUT_ENTRY} "${entry}" PARENT_SCOPE)
endfunction()

# User override API:
#   cg_add_hlsl(FILE <path> STAGE <stage> [ENTRY <entry>] [OUTPUT <basename>])
set(_CG_HLSL_OVERRIDES "")
function(cg_add_hlsl)
  cmake_parse_arguments(ARG "" "FILE;STAGE;ENTRY;OUTPUT" "" ${ARGN})
  if(NOT ARG_FILE OR NOT ARG_STAGE)
    message(FATAL_ERROR "cg_add_hlsl requires FILE and STAGE")
  endif()
  if(NOT EXISTS "${ARG_FILE}")
    if(EXISTS "${CG_SHADERS_DIR}/${ARG_FILE}")
      set(ARG_FILE "${CG_SHADERS_DIR}/${ARG_FILE}")
    else()
      message(FATAL_ERROR "cg_add_hlsl: file not found: ${ARG_FILE}")
    endif()
  endif()
  list(APPEND _CG_HLSL_OVERRIDES "${ARG_FILE}|${ARG_STAGE}|${ARG_ENTRY}|${ARG_OUTPUT}")
  set(_CG_HLSL_OVERRIDES "${_CG_HLSL_OVERRIDES}" PARENT_SCOPE)
endfunction()

# Load optional manifest with overrides
if(EXISTS "${CG_SHADERS_DIR}/manifest.cmake")
  include("${CG_SHADERS_DIR}/manifest.cmake")
endif()

# ------------------------------------------------------------------------------ #
# Discover source files
# ------------------------------------------------------------------------------ #
if(NOT EXISTS "${CG_SHADERS_DIR}")
  message(STATUS "[shaders] Directory not found: ${CG_SHADERS_DIR}")
  return()
endif()

file(GLOB_RECURSE _all_hlsl LIST_DIRECTORIES FALSE "${CG_SHADERS_DIR}/*.hlsl")

# Filter out obvious include-only files (common/include/shared libs).
# You can re-add any of these via manifest overrides.
set(_skip_regex "(_?common|_?include|_?shared|_?lib)$")
set(CG_HLSL_SOURCES "")
foreach(f IN LISTS _all_hlsl)
  get_filename_component(nwe "${f}" NAME_WE)
  string(TOLOWER "${nwe}" lwe)
  if(lwe MATCHES "${_skip_regex}")
    # skip auto, allow manifest to override
  else()
    list(APPEND CG_HLSL_SOURCES "${f}")
  endif()
endforeach()

# Ensure override files are present in the compile list
foreach(item IN LISTS _CG_HLSL_OVERRIDES)
  string(REPLACE "|" ";" parts "${item}")
  list(GET parts 0 f)
  if(NOT f IN_LIST CG_HLSL_SOURCES)
    list(APPEND CG_HLSL_SOURCES "${f}")
  endif()
endforeach()

# ------------------------------------------------------------------------------ #
# Emit build rules
# ------------------------------------------------------------------------------ #
file(MAKE_DIRECTORY "${CG_SHADERS_OUT_DIR}")

set(CG_CSO_OUTPUTS "")

foreach(hlsl IN LISTS CG_HLSL_SOURCES)
  # Resolve stage/entry (override > guess)
  set(stage "") ; set(entry "") ; set(outstem "")
  set(_ovr "")
  foreach(item IN LISTS _CG_HLSL_OVERRIDES)
    string(REPLACE "|" ";" parts "${item}")
    list(GET parts 0 f)
    if(f STREQUAL "${hlsl}")
      set(_ovr "${item}")
      break()
    endif()
  endforeach()

  if(_ovr)
    string(REPLACE "|" ";" parts "${_ovr}")
    list(GET parts 1 stage)
    if(parts LENGTH GREATER 2)
      list(GET parts 2 entry)
    endif()
    if(parts LENGTH GREATER 3)
      list(GET parts 3 outstem)
    endif()
  else()
    _cg_guess_stage_and_entry("${hlsl}" stage entry)
  endif()

  if(stage STREQUAL "")
    message(STATUS "[shaders] Skipping (unknown stage; add override in shaders/manifest.cmake): ${hlsl}")
    continue()
  endif()

  if(entry STREQUAL "")
    if(stage STREQUAL "compute")
      set(entry "CSMain")
    elseif(stage STREQUAL "pixel")
      set(entry "PSMain")
    elseif(stage STREQUAL "vertex")
      set(entry "VSMain")
    elseif(stage STREQUAL "geometry")
      set(entry "GSMain")
    elseif(stage STREQUAL "hull")
      set(entry "HSMain")
    elseif(stage STREQUAL "domain")
      set(entry "DSMain")
    endif()
  endif()

  _cg_profile_for_stage("${stage}" profile)
  if(profile STREQUAL "")
    message(WARNING "[shaders] No profile for stage '${stage}' in ${hlsl}; skipping.")
    continue()
  endif()

  # Output path mirrors relative directory under shaders/
  file(RELATIVE_PATH rel "${CG_SHADERS_DIR}" "${hlsl}")
  get_filename_component(rel_dir "${rel}" DIRECTORY)
  get_filename_component(base "${hlsl}" NAME_WE)
  if(outstem STREQUAL "")
    set(outstem "${base}")
  endif()

  set(outdir "${CG_SHADERS_OUT_DIR}/${rel_dir}")
  set(outcso "${outdir}/${outstem}.cso")

  # Ensure per-file output dir exists at build time
  set(_mkdir_cmd ${CMAKE_COMMAND} -E make_directory "${outdir}")

  if(_HLSL_COMPILER STREQUAL "DXC")
    # DXC command
    set(_cmd ${DXC_EXE}
      -nologo
      $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Od> $<$<CONFIG:Debug>:-Qembed_debug>
      $<$<NOT:$<CONFIG:Debug>>:-O3>
      -E ${entry}
      -T ${profile}
      -Fo "${outcso}"
      -I "${CG_SHADERS_DIR}"
      -I "${CMAKE_SOURCE_DIR}/shaders"
      -D $<$<CONFIG:Debug>:DEBUG=1>$<$<NOT:$<CONFIG:Debug>>:NDEBUG=1>
      "${hlsl}"
    )
  else()
    # FXC command
    set(_cmd ${FXC_EXE}
      /nologo
      $<$<CONFIG:Debug>:/Zi> $<$<CONFIG:Debug>:/Od>
      $<$<NOT:$<CONFIG:Debug>>:/O3>
      /T ${profile}
      /E ${entry}
      /Fo "${outcso}"
      /I "${CG_SHADERS_DIR}"
      /I "${CMAKE_SOURCE_DIR}/shaders"
      /D $<$<CONFIG:Debug>:DEBUG=1>$<$<NOT:$<CONFIG:Debug>>:NDEBUG=1>
      "${hlsl}"
    )
  endif()

  add_custom_command(
    OUTPUT "${outcso}"
    COMMAND ${_mkdir_cmd}
    COMMAND ${_cmd}
    DEPENDS "${hlsl}"
    COMMENT "[shaders] ${stage} ${profile}: ${rel} -> ${outcso}"
    VERBATIM
  )

  list(APPEND CG_CSO_OUTPUTS "${outcso}")
endforeach()

if(CG_CSO_OUTPUTS)
  add_custom_target(shaders ALL DEPENDS ${CG_CSO_OUTPUTS})
  # Export convenience vars to parent (top-level may install ${CG_SHADERS_OUT_DIR})
  set(CG_SHADERS_OUTPUT_DIR "${CG_SHADERS_OUT_DIR}" PARENT_SCOPE)
  set(CG_SHADERS_BUILT_CSOS "${CG_CSO_OUTPUTS}" PARENT_SCOPE)
  message(STATUS "[shaders] Will build ${CG_CSO_OUTPUTS}")
else()
  message(STATUS "[shaders] No HLSL files queued for compilation.")
endif()
