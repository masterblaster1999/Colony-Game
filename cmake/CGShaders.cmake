# cmake/CGShaders.cmake
include_guard(GLOBAL)

# Make optional helper modules available if you keep them
include(ColonyHLSL OPTIONAL)
include(ShaderSetup OPTIONAL)    # provides colony_compile_hlsl()/colony_add_hlsl() in some setups
include(AutoHlsl OPTIONAL)
include(DxcShaders OPTIONAL)
include(ConfigureHLSL OPTIONAL)

# ========== Shader directory discovery ==========
# Collect shader roots from typical locations (de-duplicated)
function(cg_collect_shader_dirs OUT_VAR)
  set(_dirs)
  foreach(_cand
    "${CMAKE_SOURCE_DIR}/shaders"
    "${CMAKE_SOURCE_DIR}/renderer/Shaders"
    "${CMAKE_SOURCE_DIR}/src/pcg/shaders")
    if(EXISTS "${_cand}")
      list(APPEND _dirs "${_cand}")
    endif()
  endforeach()
  if(_dirs)
    list(REMOVE_DUPLICATES _dirs)
  endif()
  set(${OUT_VAR} "${_dirs}" PARENT_SCOPE)
endfunction()

# ========== Visual Studio per-file property fallback ==========
# If ConfigureHLSL wasn't provided, emulate the minimal bits for VS generators.
if(NOT COMMAND configure_hlsl_target)
  function(configure_hlsl_target target)
    if(NOT MSVC)
      return()
    endif()
    get_target_property(_srcs ${target} SOURCES)
    if(NOT _srcs)
      return()
    endif()
    foreach(_s IN LISTS _srcs)
      get_filename_component(_ext "${_s}" EXT)
      if(NOT _ext STREQUAL ".hlsl")
        continue()
      endif()
      get_filename_component(_name "${_s}" NAME_WE)
      string(TOLOWER "${_name}" _lower)
      set(_type "")
      set(_entry "")
      if(_lower MATCHES "_vs$")      ; set(_type "Vertex")   ; set(_entry "VSMain")
      elseif(_lower MATCHES "_ps$")  ; set(_type "Pixel")    ; set(_entry "PSMain")
      elseif(_lower MATCHES "_cs$")  ; set(_type "Compute")  ; set(_entry "CSMain")
      elseif(_lower MATCHES "_gs$")  ; set(_type "Geometry") ; set(_entry "GSMain")
      elseif(_lower MATCHES "_hs$")  ; set(_type "Hull")     ; set(_entry "HSMain")
      elseif(_lower MATCHES "_ds$")  ; set(_type "Domain")   ; set(_entry "DSMain")
      else()
        set_source_files_properties(${_s} PROPERTIES HEADER_FILE_ONLY ON)
        continue()
      endif()
      if(_lower STREQUAL "noise_fbm_cs")
        set(_entry "main")
      endif()
      # CMake VS HLSL file props: VS_SHADER_TYPE, VS_SHADER_MODEL, VS_SHADER_ENTRYPOINT
      set_source_files_properties(${_s} PROPERTIES
        VS_SHADER_TYPE       "${_type}"
        VS_SHADER_MODEL      "${COLONY_HLSL_MODEL}"
        VS_SHADER_ENTRYPOINT "${_entry}"
      )
    endforeach()
  endfunction()
endif()

# Compute nice status text (used by summary)
set(_CG_HLSL_TOOLCHAIN "unknown")

# ========== Main entry: configure HLSL pipeline for a target ==========
# Usage: cg_setup_hlsl_pipeline(TARGET ColonyGame RENDERER d3d11|d3d12)
function(cg_setup_hlsl_pipeline)
  cmake_parse_arguments(CG " " "TARGET;RENDERER" "" ${ARGN})
  if(NOT CG_TARGET OR NOT TARGET ${CG_TARGET})
    message(FATAL_ERROR "cg_setup_hlsl_pipeline: missing/invalid TARGET")
  endif()
  if(NOT CG_RENDERER)
    set(CG_RENDERER "d3d11")
  endif()

  # Discover shader roots once
  cg_collect_shader_dirs(COLONY_SHADER_SOURCE_DIRS)
  set(COLONY_SHADER_SOURCE_DIRS "${COLONY_SHADER_SOURCE_DIRS}" PARENT_SCOPE)

  # Early out if no shaders at all
  if(NOT COLONY_SHADER_SOURCE_DIRS)
    set(_CG_HLSL_TOOLCHAIN "none (no shader dirs)")
    set(CG_HLSL_MSBUILD OFF PARENT_SCOPE)
    return()
  endif()

  # Path A: Visual Studio generator with ShaderSetup helpers → MSBuild HLSL
  set(CG_HLSL_MSBUILD OFF)
  if(MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio" AND COMMAND colony_compile_hlsl)
    set(CG_HLSL_MSBUILD ON)
    foreach(_dir IN LISTS COLONY_SHADER_SOURCE_DIRS)
      colony_compile_hlsl(
        TARGET        ${CG_TARGET}
        DIR           "${_dir}"
        RECURSE
        MODEL         ${COLONY_HLSL_MODEL}
        OUTPUT_SUBDIR "shaders"
        INCLUDE_DIRS  "${_dir}/include"
        ENTRYPOINT_MAP "noise_fbm_cs.hlsl=main"
      )
    endforeach()
    set(_CG_HLSL_TOOLCHAIN "Visual Studio HLSL (MSBuild)")
    set(CG_HLSL_MSBUILD ON PARENT_SCOPE)
    return()
  endif()

  # Path B: Visual Studio generator without helpers → set per-file properties
  if(MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio" AND NOT COMMAND colony_compile_hlsl)
    # Add *.hlsl sources so VS knows to compile them
    set(_all_hlsl)
    foreach(_d IN LISTS COLONY_SHADER_SOURCE_DIRS)
      file(GLOB_RECURSE _found CONFIGURE_DEPENDS "${_d}/*.hlsl")
      list(APPEND _all_hlsl ${_found})
    endforeach()
    if(_all_hlsl)
      target_sources(${CG_TARGET} PRIVATE ${_all_hlsl})
      configure_hlsl_target(${CG_TARGET})
      set(_CG_HLSL_TOOLCHAIN "VS native HLSL (per-file props)")
      set(CG_HLSL_MSBUILD ON PARENT_SCOPE)   # still MSBuild-based
      return()
    endif()
  endif()

  # Path C: Non‑VS generators (or forced offline) → build-time FXC/DXC
  # Create a single 'shaders' custom target that produces .cso files under res/shaders/
  set(SHADER_BIN_DIR "${CMAKE_BINARY_DIR}/res/shaders")
  file(MAKE_DIRECTORY "${SHADER_BIN_DIR}")

  if(CG_RENDERER STREQUAL "d3d11")
    # ---- FXC (SM 5.x, DXBC) ----
    find_program(FXC_EXE NAMES fxc HINTS "$ENV{WindowsSdkDir}/bin/x64" "$ENV{WindowsSdkDir}/bin")
    if(NOT FXC_EXE)
      file(GLOB _fxc_cands
        "$ENV{WindowsSdkDir}/bin/*/x64/fxc.exe"
        "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/fxc.exe"
        "C:/Program Files/Windows Kits/10/bin/*/x64/fxc.exe")
      if(_fxc_cands)
        list(SORT _fxc_cands DESC)
        list(GET _fxc_cands 0 FXC_EXE)
      endif()
    endif()
    if(NOT FXC_EXE)
      message(WARNING "FXC not found; HLSL will not be compiled offline for D3D11.")
      set(_CG_HLSL_TOOLCHAIN "missing FXC")
      return()
    endif()
    set(FXC_DEFAULT_SM "5_0" CACHE STRING "Default Shader Model for FXC (5_0 or 5_1)")
    set_property(CACHE FXC_DEFAULT_SM PROPERTY STRINGS 5_0 5_1)

    # Compute include flags once
    set(_FXC_INCLUDES)
    foreach(_inc IN LISTS COLONY_SHADER_SOURCE_DIRS)
      list(APPEND _FXC_INCLUDES /I "${_inc}")
    endforeach()

    set(_BUILT_CSOS)
    function(_fxc_infer SRC OUT_PROFILE OUT_ENTRY OUT_SKIP)
      get_filename_component(_name "${SRC}" NAME_WE)
      string(TOLOWER "${_name}" _lower)
      set(_p "") ; set(_e "") ; set(_skip FALSE)
      if(_lower MATCHES "(_|\\.)vs$|^vs_") ; set(_p "vs_${FXC_DEFAULT_SM}") ; set(_e "VSMain")
      elseif(_lower MATCHES "(_|\\.)ps$|^ps_") ; set(_p "ps_${FXC_DEFAULT_SM}") ; set(_e "PSMain")
      elseif(_lower MATCHES "(_|\\.)cs$|^cs_") ; set(_p "cs_${FXC_DEFAULT_SM}") ; set(_e "CSMain")
      elseif(_lower MATCHES "(_|\\.)gs$|^gs_") ; set(_p "gs_${FXC_DEFAULT_SM}") ; set(_e "GSMain")
      elseif(_lower MATCHES "(_|\\.)hs$|^hs_") ; set(_p "hs_${FXC_DEFAULT_SM}") ; set(_e "HSMain")
      elseif(_lower MATCHES "(_|\\.)ds$|^ds_") ; set(_p "ds_${FXC_DEFAULT_SM}") ; set(_e "DSMain")
      elseif(_lower MATCHES "(_|\\.)ms$|^ms_" OR _lower MATCHES "(_|\\.)as$|^as_")
        set(_skip TRUE) # SM6-only stages
      endif()
      set(${OUT_PROFILE} "${_p}" PARENT_SCOPE)
      set(${OUT_ENTRY}   "${_e}" PARENT_SCOPE)
      set(${OUT_SKIP}    "${_skip}" PARENT_SCOPE)
    endfunction()

    set(HLSL_SOURCES)
    foreach(_d IN LISTS COLONY_SHADER_SOURCE_DIRS)
      file(GLOB_RECURSE _found CONFIGURE_DEPENDS "${_d}/*.hlsl")
      list(APPEND HLSL_SOURCES ${_found})
    endforeach()

    foreach(SRC IN LISTS HLSL_SOURCES)
      _fxc_infer("${SRC}" PROFILE ENTRY SKIP_THIS)
      if(SKIP_THIS OR NOT PROFILE)
        continue()
      endif()
      get_filename_component(NAME_WE "${SRC}" NAME_WE)
      if("${NAME_WE}" STREQUAL "noise_fbm_cs")
        set(ENTRY "main")
      endif()
      set(OUTFILE "${SHADER_BIN_DIR}/${NAME_WE}.cso")
      add_custom_command(
        OUTPUT "${OUTFILE}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_BIN_DIR}"
        COMMAND "${FXC_EXE}" /nologo /T ${PROFILE} /E ${ENTRY}
                $<$<CONFIG:Debug>:/Zi> $<$<CONFIG:Debug>:/Od>
                /Fo "${OUTFILE}" ${_FXC_INCLUDES} "${SRC}"
        DEPENDS "${SRC}"
        COMMENT "FXC ${PROFILE} ${SRC} -> ${OUTFILE}"
        VERBATIM
      )
      list(APPEND _BUILT_CSOS "${OUTFILE}")
    endforeach()

    if(_BUILT_CSOS)
      add_custom_target(shaders ALL DEPENDS ${_BUILT_CSOS})
      set_property(TARGET shaders PROPERTY FOLDER "assets")
      add_dependencies(${CG_TARGET} shaders)
      set(_CG_HLSL_TOOLCHAIN "FXC (SM5) -> ${SHADER_BIN_DIR}")
    else()
      set(_CG_HLSL_TOOLCHAIN "FXC: no matching *.hlsl")
    endif()

  else() # d3d12
    # ---- DXC (SM 6.x, DXIL) ----
    if(COMMAND dxc_compile_shader)
      # Use helper if present
      set(HLSL_SOURCES)
      foreach(_d IN LISTS COLONY_SHADER_SOURCE_DIRS)
        file(GLOB_RECURSE _found CONFIGURE_DEPENDS "${_d}/*.hlsl")
        list(APPEND HLSL_SOURCES ${_found})
      endforeach()

      set(_INCLUDE_DIRS ${COLONY_SHADER_SOURCE_DIRS})
      set(DXC_DEFAULT_SM "6_7" CACHE STRING "Default Shader Model (6_6 or 6_7)")
      set_property(CACHE DXC_DEFAULT_SM PROPERTY STRINGS 6_6 6_7)

      function(_dxc_infer SRC OUT_STAGE OUT_PROFILE OUT_ENTRY)
        get_filename_component(_name "${SRC}" NAME_WE)
        string(TOLOWER "${_name}" _lower)
        set(_stage "") ; set(_entry "")
        if(_lower MATCHES "(_|\\.)vs$|^vs_") ; set(_stage "vs") ; set(_entry "VSMain")
        elseif(_lower MATCHES "(_|\\.)ps$|^ps_") ; set(_stage "ps") ; set(_entry "PSMain")
        elseif(_lower MATCHES "(_|\\.)cs$|^cs_") ; set(_stage "cs") ; set(_entry "CSMain")
        elseif(_lower MATCHES "(_|\\.)gs$|^gs_") ; set(_stage "gs") ; set(_entry "GSMain")
        elseif(_lower MATCHES "(_|\\.)hs$|^hs_") ; set(_stage "hs") ; set(_entry "HSMain")
        elseif(_lower MATCHES "(_|\\.)ds$|^ds_") ; set(_stage "ds") ; set(_entry "DSMain")
        elseif(_lower MATCHES "(_|\\.)ms$|^ms_") ; set(_stage "ms") ; set(_entry "MSMain")
        elseif(_lower MATCHES "(_|\\.)as$|^as_") ; set(_stage "as") ; set(_entry "ASMain")
        endif()
        if("${_name}" STREQUAL "noise_fbm_cs")
          set(_stage "cs") ; set(_entry "main")
        endif()
        set(${OUT_STAGE} "${_stage}" PARENT_SCOPE)
        if(_stage)
          set(${OUT_PROFILE} "${_stage}_${DXC_DEFAULT_SM}" PARENT_SCOPE)
        else()
          set(${OUT_PROFILE} "" PARENT_SCOPE)
        endif()
        set(${OUT_ENTRY} "${_entry}" PARENT_SCOPE)
      endfunction()

      set(_BUILT_DXIL)
      foreach(SRC IN LISTS HLSL_SOURCES)
        _dxc_infer("${SRC}" STAGE PROFILE ENTRY)
        if(NOT STAGE)
          continue()
        endif()
        dxc_compile_shader(OUT _BUILT_DXIL FILE "${SRC}" ENTRY "${ENTRY}" STAGE "${STAGE}" PROFILE "${PROFILE}" INCLUDES ${_INCLUDE_DIRS})
      endforeach()

      if(_BUILT_DXIL)
        add_custom_target(shaders ALL DEPENDS ${_BUILT_DXIL})
        set_property(TARGET shaders PROPERTY FOLDER "assets")
        add_dependencies(${CG_TARGET} shaders)
        set(_CG_HLSL_TOOLCHAIN "DXC (helper) -> ${CMAKE_BINARY_DIR}/shaders")
      else()
        set(_CG_HLSL_TOOLCHAIN "DXC helper: no matching *.hlsl")
      endif()

    else()
      # CLI fallback
      find_program(DXC_EXE NAMES dxc HINTS
        "$ENV{DXC_PATH}" "$ENV{VULKAN_SDK}/Bin" "$ENV{VCToolsInstallDir}/bin/Hostx64/x64"
        "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin"
        "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Tools/Llvm/x64/bin"
        "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Tools/Llvm/x64/bin"
        "$ENV{WindowsSdkDir}/bin/x64" "$ENV{WindowsSdkDir}/bin")
      if(NOT DXC_EXE)
        message(WARNING "DXC not found; HLSL will not be compiled offline for D3D12.")
        set(_CG_HLSL_TOOLCHAIN "missing DXC")
        return()
      endif()

      set(DXC_DEFAULT_SM "6_7" CACHE STRING "Default Shader Model (6_6 or 6_7)")
      set_property(CACHE DXC_DEFAULT_SM PROPERTY STRINGS 6_6 6_7)

      set(HLSL_SOURCES)
      foreach(_d IN LISTS COLONY_SHADER_SOURCE_DIRS)
        file(GLOB_RECURSE _found CONFIGURE_DEPENDS "${_d}/*.hlsl")
        list(APPEND HLSL_SOURCES ${_found})
      endforeach()

      set(_DXC_INCLUDES)
      foreach(_inc IN LISTS COLONY_SHADER_SOURCE_DIRS)
        list(APPEND _DXC_INCLUDES -I "${_inc}")
      endforeach()

      function(_dxc_cli_infer SRC OUT_PROFILE OUT_ENTRY)
        get_filename_component(_name "${SRC}" NAME_WE)
        string(TOLOWER "${_name}" _lower)
        set(_p "") ; set(_e "")
        if(_lower MATCHES "(_|\\.)vs$|^vs_") ; set(_p "vs_${DXC_DEFAULT_SM}") ; set(_e "VSMain")
        elseif(_lower MATCHES "(_|\\.)ps$|^ps_") ; set(_p "ps_${DXC_DEFAULT_SM}") ; set(_e "PSMain")
        elseif(_lower MATCHES "(_|\\.)cs$|^cs_") ; set(_p "cs_${DXC_DEFAULT_SM}") ; set(_e "CSMain")
        elseif(_lower MATCHES "(_|\\.)gs$|^gs_") ; set(_p "gs_${DXC_DEFAULT_SM}") ; set(_e "GSMain")
        elseif(_lower MATCHES "(_|\\.)hs$|^hs_") ; set(_p "hs_${DXC_DEFAULT_SM}") ; set(_e "HSMain")
        elseif(_lower MATCHES "(_|\\.)ds$|^ds_") ; set(_p "ds_${DXC_DEFAULT_SM}") ; set(_e "DSMain")
        elseif(_lower MATCHES "(_|\\.)ms$|^ms_") ; set(_p "ms_${DXC_DEFAULT_SM}") ; set(_e "MSMain")
        elseif(_lower MATCHES "(_|\\.)as$|^as_") ; set(_p "as_${DXC_DEFAULT_SM}") ; set(_e "ASMain")
        endif()
        if("${_name}" STREQUAL "noise_fbm_cs") ; set(_e "main") ; set(_p "cs_${DXC_DEFAULT_SM}") ; endif()
        set(${OUT_PROFILE} "${_p}" PARENT_SCOPE)
        set(${OUT_ENTRY}   "${_e}" PARENT_SCOPE)
      endfunction()

      set(_BUILT_CSOS)
      foreach(SRC IN LISTS HLSL_SOURCES)
        _dxc_cli_infer("${SRC}" PROFILE ENTRY)
        if(NOT PROFILE)
          continue()
        endif()
        get_filename_component(NAME_WE "${SRC}" NAME_WE)
        set(OUTFILE "${SHADER_BIN_DIR}/${NAME_WE}.cso")
        add_custom_command(
          OUTPUT "${OUTFILE}"
          COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_BIN_DIR}"
          COMMAND "${DXC_EXE}" -nologo -T ${PROFILE} -E ${ENTRY}
                  $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Qembed_debug> $<$<CONFIG:Release>:-Qstrip_debug>
                  -Fo "${OUTFILE}" ${_DXC_INCLUDES} "${SRC}"
          DEPENDS "${SRC}"
          COMMENT "DXC ${PROFILE} ${SRC} -> ${OUTFILE}"
          VERBATIM
        )
        list(APPEND _BUILT_CSOS "${OUTFILE}")
      endforeach()

      if(_BUILT_CSOS)
        add_custom_target(shaders ALL DEPENDS ${_BUILT_CSOS})
        set_property(TARGET shaders PROPERTY FOLDER "assets")
        add_dependencies(${CG_TARGET} shaders)
        set(_CG_HLSL_TOOLCHAIN "DXC (SM6) -> ${SHADER_BIN_DIR}")
      else()
        set(_CG_HLSL_TOOLCHAIN "DXC: no matching *.hlsl")
      endif()
    endif()
  endif()

  set(CG_HLSL_MSBUILD OFF PARENT_SCOPE)
  set(CG_HLSL_STATUS "${_CG_HLSL_TOOLCHAIN}" PARENT_SCOPE)
  set(_CG_HLSL_TOOLCHAIN "${_CG_HLSL_TOOLCHAIN}" PARENT_SCOPE)
endfunction()
