# cmake/ColonyHLSL.cmake
include(CMakeParseArguments)

function(colony_add_hlsl target)
  if (NOT TARGET ${target})
    message(FATAL_ERROR "colony_add_hlsl: Target '${target}' does not exist yet")
  endif()

  # Arguments:
  #   DIR:   optional directory to glob for *.hlsl (and *.hlsli for includes)
  #   SOURCES: explicit list of shader files
  #   MODEL: default shader model (e.g. 6.7)
  #   ENTRY: default entry point (e.g. main)
  #   OUTDIR: output dir for compiled artifacts
  set(options)
  set(oneValueArgs DIR MODEL ENTRY OUTDIR)
  set(multiValueArgs SOURCES)
  cmake_parse_arguments(CAH "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if (NOT CAH_SOURCES AND NOT CAH_DIR)
    message(FATAL_ERROR "colony_add_hlsl: Provide SOURCES or DIR")
  endif()

  if (NOT CAH_MODEL)
    set(CAH_MODEL "6.7")
  endif()

  if (NOT CAH_ENTRY)
    set(CAH_ENTRY "main")
  endif()

  if (NOT CAH_OUTDIR)
    set(CAH_OUTDIR "${CMAKE_BINARY_DIR}/shaders")
  endif()

  if (CAH_DIR)
    file(GLOB_RECURSE HLSL_FILES CONFIGURE_DEPENDS
      "${CAH_DIR}/*.hlsl" "${CAH_DIR}/*.hlsli"
    )
  else()
    set(HLSL_FILES ${CAH_SOURCES})
  endif()

  # Helper to guess shader stage from filename suffix
  #   *_VS.hlsl => Vertex, *_PS.hlsl => Pixel, *_CS.hlsl => Compute, etc.
  function(_colony_guess_stage in out_var)
    set(stage "Pixel")
    if (in MATCHES "_VS\\.hlsl$")     set(stage "Vertex")
    elseif(in MATCHES "_PS\\.hlsl$")  set(stage "Pixel")
    elseif(in MATCHES "_GS\\.hlsl$")  set(stage "Geometry")
    elseif(in MATCHES "_HS\\.hlsl$")  set(stage "Hull")
    elseif(in MATCHES "_DS\\.hlsl$")  set(stage "Domain")
    elseif(in MATCHES "_CS\\.hlsl$")  set(stage "Compute")
    endif()
    set(${out_var} "${stage}" PARENT_SCOPE)
  endfunction()

  # Case A: Visual Studio generator (use VS_* HLSL source properties)
  if (MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio")
    foreach(f IN LISTS HLSL_FILES)
      get_filename_component(namewe "${f}" NAME_WE)
      _colony_guess_stage("${f}" stage)
      file(MAKE_DIRECTORY "${CAH_OUTDIR}/headers")
      set(out_header "${CAH_OUTDIR}/headers/${namewe}.h")

      set_source_files_properties("${f}" PROPERTIES
        VS_SHADER_TYPE "${stage}"                 # Vertex/Pixel/… (auto-guessed)
        VS_SHADER_MODEL "${CAH_MODEL}"            # e.g. 6.7
        VS_SHADER_ENTRYPOINT "${CAH_ENTRY}"       # default "main"
        VS_SHADER_OUTPUT_HEADER_FILE "${out_header}" # embed compiled blob as header if desired
      )
      target_sources(${target} PRIVATE "${f}")
    endforeach()

  # Case B: Other generators on Windows (Ninja, NMake) — use DXC.exe
  elseif (WIN32)
    # Try vcpkg's directx-dxc
    find_package(directx-dxc CONFIG QUIET) # provides DIRECTX_DXC_TOOL
    if (NOT DEFINED DIRECTX_DXC_TOOL)
      message(FATAL_ERROR
        "colony_add_hlsl: DIRECTX_DXC_TOOL not found. "
        "Install 'directx-dxc' via vcpkg manifest or use a Visual Studio generator.")
    endif()

    file(MAKE_DIRECTORY "${CAH_OUTDIR}")
    set(dxc_outputs)
    foreach(f IN LISTS HLSL_FILES)
      get_filename_component(namewe "${f}" NAME_WE)
      _colony_guess_stage("${f}" stage)

      # Map VS stage to DXC target profiles
      set(profile "ps_${CAH_MODEL}")
      if (stage STREQUAL "Vertex")   set(profile "vs_${CAH_MODEL}")
      elseif(stage STREQUAL "Pixel") set(profile "ps_${CAH_MODEL}")
      elseif(stage STREQUAL "Compute") set(profile "cs_${CAH_MODEL}")
      elseif(stage STREQUAL "Geometry") set(profile "gs_${CAH_MODEL}")
      elseif(stage STREQUAL "Hull")  set(profile "hs_${CAH_MODEL}")
      elseif(stage STREQUAL "Domain")set(profile "ds_${CAH_MODEL}")
      endif()

      set(out "${CAH_OUTDIR}/${namewe}.dxil")
      add_custom_command(
        OUTPUT "${out}"
        COMMAND "${DIRECTX_DXC_TOOL}" -T "${profile}" -E "${CAH_ENTRY}"
                -Fo "${out}" "${f}"
        DEPENDS "${f}"
        COMMENT "DXC ${profile} ${f}"
        VERBATIM
      )
      list(APPEND dxc_outputs "${out}")
    endforeach()

    add_custom_target(${target}_shaders DEPENDS ${dxc_outputs})
    add_dependencies(${target} ${target}_shaders)

  else()
    message(FATAL_ERROR "colony_add_hlsl: Unsupported platform/generator")
  endif()
endfunction()
