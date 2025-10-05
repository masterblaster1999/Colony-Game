# cmake/shaders.cmake
# Windows-first HLSL helpers with flexible parser and VS/MSBuild integration.
# - colony_add_hlsl(): attach .hlsl files to a target with correct VS_* properties
# - cg_compile_hlsl(): (compat) single-file compile rule used by older callsites
#
# References:
#   VS_SHADER_* & VS_TOOL_OVERRIDE properties for Visual Studio generators:
#     https://cmake.org/cmake/help/latest/prop_sf/VS_SHADER_TYPE.html
#     https://cmake.org/cmake/help/latest/prop_sf/VS_SHADER_ENTRYPOINT.html
#     https://cmake.org/cmake/help/latest/prop_sf/VS_SHADER_MODEL.html
#     https://cmake.org/cmake/help/latest/prop_sf/VS_SHADER_FLAGS.html
#     https://cmake.org/cmake/help/latest/prop_sf/VS_SHADER_OBJECT_FILE_NAME.html
#     https://cmake.org/cmake/help/latest/prop_sf/VS_SHADER_ENABLE_DEBUG.html
#     https://cmake.org/cmake/help/latest/prop_sf/VS_SHADER_DISABLE_OPTIMIZATIONS.html
#     https://cmake.org/cmake/help/latest/prop_sf/VS_TOOL_OVERRIDE.html
#
# Behavior:
# - No-ops on non-Windows or non-Visual Studio generators.
# - Accepts both SOURCES/FILES keyword style and positional form: colony_add_hlsl(<tgt> <files...>).
# - Infers shader stage from filename suffix (_vs/_ps/_cs/_gs/_hs/_ds/_ms/_as).
# - Defaults: VSMain/PSMain/CSMain/etc. Entrypoint can be overridden per call
#   via ENTRYPOINT_<STAGE>=name (e.g., ENTRYPOINT_PS=MyPS).
# - Shader Model default: 6_7 for COLONY_RENDERER==d3d12, else 5_0.
# - Excludes MS/AS on D3D11 by setting VS_TOOL_OVERRIDE=None.
#
# Guard double-include:
if(DEFINED _COLONY_SHADERS_CMAKE_INCLUDED)
  return()
endif()
set(_COLONY_SHADERS_CMAKE_INCLUDED 1)

# ---- Helpers ---------------------------------------------------------------

function(_cah_infer_stage_and_entry SRC OUT_STAGE OUT_ENTRY)
  get_filename_component(_name_we "${SRC}" NAME_WE)
  string(TOLOWER "${_name_we}" _lower)
  set(_stage "")
  set(_entry "")

  if(_lower MATCHES "(_|\\.)vs$|^vs_")       # vertex
    set(_stage "Vertex")
    set(_entry "VSMain")
  elseif(_lower MATCHES "(_|\\.)ps$|^ps_")   # pixel
    set(_stage "Pixel")
    set(_entry "PSMain")
  elseif(_lower MATCHES "(_|\\.)cs$|^cs_")   # compute
    set(_stage "Compute")
    set(_entry "CSMain")
  elseif(_lower MATCHES "(_|\\.)gs$|^gs_")   # geometry
    set(_stage "Geometry")
    set(_entry "GSMain")
  elseif(_lower MATCHES "(_|\\.)hs$|^hs_")   # hull
    set(_stage "Hull")
    set(_entry "HSMain")
  elseif(_lower MATCHES "(_|\\.)ds$|^ds_")   # domain
    set(_stage "Domain")
    set(_entry "DSMain")
  elseif(_lower MATCHES "(_|\\.)ms$|^ms_")   # mesh (DX12+)
    set(_stage "Mesh")
    set(_entry "MSMain")
  elseif(_lower MATCHES "(_|\\.)as$|^as_")   # amplification/task (DX12+)
    set(_stage "Amplification")
    set(_entry "ASMain")
  endif()

  set(${OUT_STAGE} "${_stage}" PARENT_SCOPE)
  set(${OUT_ENTRY} "${_entry}" PARENT_SCOPE)
endfunction()

function(_cah_build_flags OUT_STR INCLUDES DEFINES)
  set(_f "")
  foreach(d IN LISTS INCLUDES)
    if(d)
      string(APPEND _f " /I\"${d}\"")
    endif()
  endforeach()
  foreach(def IN LISTS DEFINES)
    if(def)
      # Pass through literal; allow FOO or FOO=bar
      string(APPEND _f " /D${def}")
    endif()
  endforeach()
  string(STRIP "${_f}" _f)
  set(${OUT_STR} "${_f}" PARENT_SCOPE)
endfunction()

# ---- Public: colony_add_hlsl ----------------------------------------------
# Usage (keyword):
#   colony_add_hlsl(
#     TARGET <tgt>
#     SOURCES file1.hlsl [file2.hlsl ...]    # or FILES ...
#     [INCLUDE_DIRS dir1;dir2] [DEFINES FOO;BAR=1]
#     [OUTPUT_DIR <abs-or-rel-dir>]
#     [DEFAULT_MODEL 6_7]                    # fallback shader model
#     [ENTRYPOINT_VS name] [ENTRYPOINT_PS name] [ENTRYPOINT_CS name]
#     [ENTRYPOINT_GS name] [ENTRYPOINT_HS name] [ENTRYPOINT_DS name]
#     [ENTRYPOINT_MS name] [ENTRYPOINT_AS name]
#     [QUIET]
#   )
#
# Positional (back-compat):
#   colony_add_hlsl(<tgt> file1.hlsl file2.hlsl ...)
#
function(colony_add_hlsl)
  # Windows + VS only: use MSBuild HLSL integration.
  if(NOT WIN32 OR NOT CMAKE_GENERATOR MATCHES "Visual Studio")
    return()
  endif()

  set(_all_args "${ARGN}")

  # Parse keyword-style arguments.
  set(_opts QUIET)
  set(_one  TARGET OUTPUT_DIR DEFAULT_MODEL
            ENTRYPOINT_VS ENTRYPOINT_PS ENTRYPOINT_CS
            ENTRYPOINT_GS ENTRYPOINT_HS ENTRYPOINT_DS
            ENTRYPOINT_MS ENTRYPOINT_AS)
  set(_multi SOURCES FILES INCLUDE_DIRS DEFINES)
  cmake_parse_arguments(CAH "${_opts}" "${_one}" "${_multi}" ${_all_args})

  # Back-compat: positional form => first token is target, rest are sources
  if(NOT CAH_TARGET)
    list(LENGTH _all_args _n)
    if(_n GREATER 0)
      list(GET _all_args 0 CAH_TARGET)
      list(REMOVE_AT _all_args 0)
      # Remaining (if any) are sources
      if(NOT CAH_SOURCES AND NOT CAH_FILES)
        set(CAH_SOURCES "${_all_args}")
      endif()
    endif()
  endif()

  # FILES alias -> SOURCES
  if(CAH_FILES AND NOT CAH_SOURCES)
    set(CAH_SOURCES "${CAH_FILES}")
  endif()

  if(NOT CAH_TARGET)
    message(FATAL_ERROR "colony_add_hlsl: TARGET is required (or use positional: colony_add_hlsl(<tgt> <files...>))")
  endif()
  if(NOT TARGET ${CAH_TARGET})
    message(FATAL_ERROR "colony_add_hlsl: target '${CAH_TARGET}' does not exist")
  endif()

  if(NOT CAH_SOURCES)
    message(FATAL_ERROR "colony_add_hlsl: provide SOURCES (or positional files list)")
  endif()

  # Defaults
  if(NOT CAH_DEFAULT_MODEL)
    if(DEFINED COLONY_RENDERER AND COLONY_RENDERER STREQUAL "d3d12")
      set(CAH_DEFAULT_MODEL "6_7")   # DXC / SM6.7
    else()
      set(CAH_DEFAULT_MODEL "5_0")   # FXC / SM5.0
    endif()
  endif()
  set(_outdir "${CMAKE_BINARY_DIR}/shaders")
  if(CAH_OUTPUT_DIR)
    set(_outdir "${CAH_OUTPUT_DIR}")
  endif()

  # Compose additional FXC/DXC flags from includes/defines
  _cah_build_flags(_flags "${CAH_INCLUDE_DIRS}" "${CAH_DEFINES}")

  # Attach sources and set VS_* properties
  foreach(SRC IN LISTS CAH_SOURCES)
    if(NOT IS_ABSOLUTE "${SRC}")
      set(_src_full "${CMAKE_CURRENT_LIST_DIR}/../${SRC}")
      if(EXISTS "${SRC}")
        set(_src_full "${SRC}")
      elseif(EXISTS "${CMAKE_SOURCE_DIR}/${SRC}")
        set(_src_full "${CMAKE_SOURCE_DIR}/${SRC}")
      endif()
    else()
      set(_src_full "${SRC}")
    endif()

    # Add file to target
    target_sources(${CAH_TARGET} PRIVATE "${_src_full}")

    # Infer stage + default entry point
    _cah_infer_stage_and_entry("${_src_full}" _stage _entry)

    # Allow per-stage entrypoint overrides from call
    if(_stage STREQUAL "Vertex"       AND CAH_ENTRYPOINT_VS) set(_entry "${CAH_ENTRYPOINT_VS}") endif()
    if(_stage STREQUAL "Pixel"        AND CAH_ENTRYPOINT_PS) set(_entry "${CAH_ENTRYPOINT_PS}") endif()
    if(_stage STREQUAL "Compute"      AND CAH_ENTRYPOINT_CS) set(_entry "${CAH_ENTRYPOINT_CS}") endif()
    if(_stage STREQUAL "Geometry"     AND CAH_ENTRYPOINT_GS) set(_entry "${CAH_ENTRYPOINT_GS}") endif()
    if(_stage STREQUAL "Hull"         AND CAH_ENTRYPOINT_HS) set(_entry "${CAH_ENTRYPOINT_HS}") endif()
    if(_stage STREQUAL "Domain"       AND CAH_ENTRYPOINT_DS) set(_entry "${CAH_ENTRYPOINT_DS}") endif()
    if(_stage STREQUAL "Mesh"         AND CAH_ENTRYPOINT_MS) set(_entry "${CAH_ENTRYPOINT_MS}") endif()
    if(_stage STREQUAL "Amplification"AND CAH_ENTRYPOINT_AS) set(_entry "${CAH_ENTRYPOINT_AS}") endif()

    # D3D11: exclude mesh/amplification (SM6-only stages) to avoid FXC build errors
    if((NOT DEFINED COLONY_RENDERER OR NOT COLONY_RENDERER STREQUAL "d3d12")
       AND (_stage STREQUAL "Mesh" OR _stage STREQUAL "Amplification"))
      if(NOT CAH_QUIET)
        message(STATUS "colony_add_hlsl: excluding DX12-only shader from D3D11 build: ${SRC}")
      endif()
      set_source_files_properties("${_src_full}" PROPERTIES VS_TOOL_OVERRIDE "None")  # exclude from build
      continue()
    endif()

    # Per-file output .cso under ${_outdir}/$(Configuration)/<name>.cso
    get_filename_component(_name_we "${_src_full}" NAME_WE)
    set(_ofile "${_outdir}/${CMAKE_CFG_INTDIR}/${_name_we}.cso")

    # Apply Visual Studio shader properties
    set_source_files_properties("${_src_full}" PROPERTIES
      VS_SHADER_TYPE               "${_stage}"                   # Vertex/Pixel/Compute/...
      VS_SHADER_ENTRYPOINT         "${_entry}"                   # e.g., VSMain/PSMain
      VS_SHADER_MODEL              "${CAH_DEFAULT_MODEL}"        # e.g., 5_0 or 6_7
      VS_SHADER_FLAGS              "${_flags}"                   # /I"/path" /DNAME=VAL ...
      VS_SHADER_OBJECT_FILE_NAME   "${_ofile}"                   # -Fo <out>
      VS_SHADER_ENABLE_DEBUG       "$<$<CONFIG:Debug>:true>"     # -Zi (genex supported)
      VS_SHADER_DISABLE_OPTIMIZATIONS "$<$<CONFIG:Debug>:true>"  # -Od
    )
  endforeach()
endfunction()

# ---- Compatibility: cg_compile_hlsl (only define if missing) ---------------
# Interface kept intentionally small to match older call sites:
#   cg_compile_hlsl(
#     NAME <logical>
#     SRC <file.hlsl>
#     ENTRY <EntryPoint>
#     PROFILE <vs_5_0|ps_6_7|...>
#     [INCLUDEDIRS dir1;dir2]
#     OUTVAR <varname>
#   )
if(NOT COMMAND cg_compile_hlsl)
  function(cg_compile_hlsl)
    set(_one NAME SRC ENTRY PROFILE OUTVAR)
    set(_multi INCLUDEDIRS)
    cmake_parse_arguments(CG "" "${_one}" "${_multi}" ${ARGN})

    if(NOT CG_NAME OR NOT CG_SRC OR NOT CG_ENTRY OR NOT CG_PROFILE OR NOT CG_OUTVAR)
      message(FATAL_ERROR "cg_compile_hlsl: require NAME, SRC, ENTRY, PROFILE, OUTVAR")
    endif()

    # Determine tool by profile major (SM6+ => DXC, else FXC)
    if(CG_PROFILE MATCHES "_6_")
      find_program(DXC_EXE NAMES dxc
        HINTS "$ENV{VCToolsInstallDir}/bin/Hostx64/x64" "$ENV{WindowsSdkDir}/bin/x64" "$ENV{WindowsSdkDir}/bin")
      if(NOT DXC_EXE)
        message(FATAL_ERROR "cg_compile_hlsl: dxc.exe not found for profile ${CG_PROFILE}")
      endif()
      set(_tool "${DXC_EXE}")
      set(_cmd "${_tool} -nologo -T ${CG_PROFILE} -E ${CG_ENTRY}")
      foreach(inc IN LISTS CG_INCLUDEDIRS)
        string(APPEND _cmd " -I \"${inc}\"")
      endforeach()
      set(_debug "$<$<CONFIG:Debug>:-Zi -Qembed_debug>")
      set(_strip "$<$<NOT:$<CONFIG:Debug>>:-Qstrip_debug>")
      set(_args "${_cmd} ${_debug} ${_strip}")
    else()
      find_program(FXC_EXE NAMES fxc
        HINTS "$ENV{WindowsSdkDir}/bin/x64" "$ENV{WindowsSdkDir}/bin")
      if(NOT FXC_EXE)
        message(FATAL_ERROR "cg_compile_hlsl: fxc.exe not found for profile ${CG_PROFILE}")
      endif()
      set(_tool "${FXC_EXE}")
      set(_cmd "\"${_tool}\" /nologo /T ${CG_PROFILE} /E ${CG_ENTRY}")
      foreach(inc IN LISTS CG_INCLUDEDIRS)
        string(APPEND _cmd " /I \"${inc}\"")
      endforeach()
      set(_args "${_cmd} $<$<CONFIG:Debug>:/Zi /Od>")
    endif()

    set(_bin_dir "${CMAKE_BINARY_DIR}/shaders")
    file(MAKE_DIRECTORY "${_bin_dir}")
    get_filename_component(_name_we "${CG_SRC}" NAME_WE)
    set(_out "${_bin_dir}/${_name_we}.cso")

    add_custom_command(
      OUTPUT "${_out}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_bin_dir}"
      COMMAND ${_args} -Fo "${_out}" "${CG_SRC}"
      DEPENDS "${CG_SRC}"
      COMMENT "Compile HLSL ${CG_PROFILE} ${CG_SRC} -> ${_out}"
      VERBATIM
    )

    set(${CG_OUTVAR} "${_out}" PARENT_SCOPE)
  endfunction()
endif()
