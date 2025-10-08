# cmake/shaders.cmake
include_guard(GLOBAL)

# 11 -> FXC/DXBC (D3D11), 12 -> DXC/DXIL (D3D12)
option(COLONY_D3D_BACKEND "Direct3D backend (11 or 12)" "11")

# Find fxc.exe in common SDK locations or PATH
function(colony_find_fxc OUT_FXC)
  if(NOT WIN32)
    message(FATAL_ERROR "FXC only available on Windows")
  endif()

  # Prefer unversioned kit bin, then probe versioned subdirs, then PATH
  set(_candidates
    "$ENV{WindowsSdkDir}/bin/x64/fxc.exe"
    "$ENV{WindowsSdkDir}/bin/x86/fxc.exe"
  )
  if(EXISTS "$ENV{WindowsSdkDir}/bin")
    file(GLOB _kits "$ENV{WindowsSdkDir}/bin/*")
    foreach(_kit IN LISTS _kits)
      if(EXISTS "${_kit}/x64/fxc.exe")
        list(APPEND _candidates "${_kit}/x64/fxc.exe")
      endif()
      if(EXISTS "${_kit}/x86/fxc.exe")
        list(APPEND _candidates "${_kit}/x86/fxc.exe")
      endif()
    endforeach()
  endif()

  foreach(p IN LISTS _candidates)
    file(TO_CMAKE_PATH "${p}" p2)
    if(EXISTS "${p2}")
      set(${OUT_FXC} "${p2}" PARENT_SCOPE)
      return()
    endif()
  endforeach()

  find_program(_fxc NAMES fxc.exe)
  if(_fxc)
    set(${OUT_FXC} "${_fxc}" PARENT_SCOPE)
    return()
  endif()

  message(FATAL_ERROR "Could not find fxc.exe. Install the Windows 10/11 SDK (HLSL Effects Compiler).")
endfunction()

# Detect stage from filename suffix (_vs/_ps/_cs/_gs/_hs/_ds/_as/_ms). Empty if unknown.
function(_colony_detect_stage STEM OUTVAR)
  set(_stage "")
  if("${STEM}" MATCHES ".*_vs$")      set(_stage "vs")
  elseif("${STEM}" MATCHES ".*_ps$") set(_stage "ps")
  elseif("${STEM}" MATCHES ".*_cs$") set(_stage "cs")
  elseif("${STEM}" MATCHES ".*_gs$") set(_stage "gs")
  elseif("${STEM}" MATCHES ".*_hs$") set(_stage "hs")
  elseif("${STEM}" MATCHES ".*_ds$") set(_stage "ds")
  elseif("${STEM}" MATCHES ".*_as$") set(_stage "as") # Amplification (DX12/SM6+)
  elseif("${STEM}" MATCHES ".*_ms$") set(_stage "ms") # Mesh (DX12/SM6+)
  endif()
  set(${OUTVAR} "${_stage}" PARENT_SCOPE)
endfunction()

# Map stage -> Visual Studio "VS_SHADER_TYPE" value
function(_colony_stage_to_vs_type STAGE OUTVAR)
  if("${STAGE}" STREQUAL "vs")       set(_type "Vertex")
  elseif("${STAGE}" STREQUAL "ps")   set(_type "Pixel")
  elseif("${STAGE}" STREQUAL "cs")   set(_type "Compute")
  elseif("${STAGE}" STREQUAL "gs")   set(_type "Geometry")
  elseif("${STAGE}" STREQUAL "hs")   set(_type "Hull")
  elseif("${STAGE}" STREQUAL "ds")   set(_type "Domain")
  elseif("${STAGE}" STREQUAL "as")   set(_type "Amplification")
  elseif("${STAGE}" STREQUAL "ms")   set(_type "Mesh")
  else()                              set(_type "Pixel")
  endif()
  set(${OUTVAR} "${_type}" PARENT_SCOPE)
endfunction()

# Choose a good default entry point based on stage; fallback to "main"
function(_colony_default_entry STAGE OUTVAR)
  if("${STAGE}" STREQUAL "vs")       set(_e "VSMain")
  elseif("${STAGE}" STREQUAL "ps")   set(_e "PSMain")
  elseif("${STAGE}" STREQUAL "cs")   set(_e "CSMain")
  elseif("${STAGE}" STREQUAL "gs")   set(_e "GSMain")
  elseif("${STAGE}" STREQUAL "hs")   set(_e "HSMain")
  elseif("${STAGE}" STREQUAL "ds")   set(_e "DSMain")
  elseif("${STAGE}" STREQUAL "as")   set(_e "ASMain")
  elseif("${STAGE}" STREQUAL "ms")   set(_e "MSMain")
  else()                              set(_e "main")
  endif()
  set(${OUTVAR} "${_e}" PARENT_SCOPE)
endfunction()

# colony_add_hlsl(
#   TARGET   <cmake target>   [required]
#   OUTDIR   <bin-relative outdir> (default: res/shaders)
#   SOURCES  <.hlsl/.hlsli files...> [required]
#   PROFILE  <e.g. 5_0 | 6_7 | vs_5_0 | ps_6_0> (model; stage may be omitted)
#   ENTRY    <entry function> (if omitted, inferred from stage)
#   DEFINES  <list of -D / /D>
#   INCLUDES <list of -I / /I>
# )
function(colony_add_hlsl)
  set(options)
  set(oneValueArgs TARGET OUTDIR PROFILE ENTRY)
  set(multiValueArgs SOURCES DEFINES INCLUDES)
  cmake_parse_arguments(CAH "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT CAH_TARGET)
    message(FATAL_ERROR "colony_add_hlsl: TARGET is required")
  endif()
  if(NOT CAH_SOURCES)
    message(FATAL_ERROR "colony_add_hlsl: provide SOURCES")
  endif()

  if(NOT CAH_OUTDIR OR CAH_OUTDIR STREQUAL "")
    set(CAH_OUTDIR "res/shaders")
  endif()

  # Parse shader model from PROFILE:
  #   Accept "vs_5_0" / "ps_6_7" -> model "5.0"/"6.7"
  #   Accept "5_0" / "6_7"
  #   Default "5.0"
  set(_model "5.0")
  set(_model_us "5_0")
  if(CAH_PROFILE MATCHES "^[a-z]+_([0-9]+)_([0-9]+)$")
    set(_model "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}")
    set(_model_us "${CMAKE_MATCH_1}_${CMAKE_MATCH_2}")
  elseif(CAH_PROFILE MATCHES "^([0-9]+)_([0-9]+)$")
    set(_model "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}")
    set(_model_us "${CMAKE_MATCH_1}_${CMAKE_MATCH_2}")
  endif()

  # If PROFILE includes a stage, remember it as a weak hint
  set(_profile_stage "")
  if(CAH_PROFILE MATCHES "^([a-z]+)_")
    set(_profile_stage "${CMAKE_MATCH_1}")
  endif()

  # Split sources into .hlsl and .hlsli (includes)
  set(_hlsl_sources)
  set(_hlsli_sources)
  foreach(src IN LISTS CAH_SOURCES)
    get_filename_component(_ext "${src}" EXT)
    string(TOLOWER "${_ext}" _ext_lower)
    if(_ext_lower STREQUAL ".hlsli")
      list(APPEND _hlsli_sources "${src}")
    else()
      list(APPEND _hlsl_sources "${src}")
    endif()
  endforeach()

  # --------------------------------------------------------------------------
  # Visual Studio MSBuild HLSL path (directory-agnostic):
  # Put MSBuild-compiled CSO into ${CMAKE_BINARY_DIR}/res/shaders/$(Configuration)
  # --------------------------------------------------------------------------
  if(WIN32 AND CMAKE_GENERATOR MATCHES "Visual Studio")
    # Ensure the target knows about the shader sources so MSBuild compiles them.
    target_sources(${CAH_TARGET} PRIVATE ${_hlsl_sources} ${_hlsli_sources})
    source_group("Shaders" FILES ${_hlsl_sources})
    if(_hlsli_sources)
      source_group("Shaders\\Includes" FILES ${_hlsli_sources})
      set_source_files_properties(${_hlsli_sources} PROPERTIES HEADER_FILE_ONLY ON)
    endif()

    # Use MSBuild variable for per-config output directory
    set(_msbuild_outdir "${CMAKE_BINARY_DIR}/${CAH_OUTDIR}/$(Configuration)")

    foreach(src IN LISTS _hlsl_sources)
      get_filename_component(_namewe "${src}" NAME_WE)
      # Derive stage from filename; if ambiguous, fall back to PROFILE stage, else default to Pixel.
      _colony_detect_stage("${_namewe}" _stage)
      if(NOT _stage AND _profile_stage)
        set(_stage "${_profile_stage}")
      endif()
      if(NOT _stage)
        set(_stage "ps")
      endif()
      # Map to Visual Studio HLSL type
      _colony_stage_to_vs_type("${_stage}" _type)
      # Shader model as "M.N" (e.g., "5.0", "6.7")
      set(_model_vis "${_model}")

      # Build flags: DXC style if SM6.x, else FXC style
      if(_model_vis VERSION_GREATER_EQUAL "6.0")
        set(_flags "$<$<CONFIG:Debug>:-Zi;-Od>;$<$<CONFIG:Release>:-O3;-Qstrip_debug>")
        foreach(d IN LISTS CAH_DEFINES)  list(APPEND _flags "-D" "${d}")  endforeach()
        foreach(i IN LISTS CAH_INCLUDES) list(APPEND _flags "-I" "${i}")  endforeach()
      else()
        set(_flags "$<$<CONFIG:Debug>:/Zi;/Od>;$<$<CONFIG:Release>:/O2>")
        foreach(d IN LISTS CAH_DEFINES)  list(APPEND _flags "/D" "${d}")  endforeach()
        foreach(i IN LISTS CAH_INCLUDES) list(APPEND _flags "/I" "${i}")  endforeach()
      endif()

      # Default entry: allow override via ENTRY, else stage-based heuristic
      set(_entry "${CAH_ENTRY}")
      if(NOT _entry OR _entry STREQUAL "")
        _colony_default_entry("${_stage}" _entry)
      endif()

      # Force object file name with stage suffix to avoid collisions (foo.vs.cso / foo.ps.cso)
      set(_obj "${_msbuild_outdir}/${_namewe}.${_stage}.cso")

      set_source_files_properties("${src}" PROPERTIES
        VS_SHADER_TYPE              "${_type}"
        VS_SHADER_MODEL             "${_model_vis}"
        VS_SHADER_ENTRYPOINT        "${_entry}"
        VS_SHADER_OBJECT_FILE_NAME  "${_obj}"
        VS_SHADER_FLAGS             "${_flags}"
      )
    endforeach()

    # Create output folders at configure time so MSBuild can write .cso
    if(CMAKE_CONFIGURATION_TYPES)
      foreach(_cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/${CAH_OUTDIR}/${_cfg}")
      endforeach()
    else()
      file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/${CAH_OUTDIR}")
    endif()

    # MSBuild handles compilation; no explicit custom target/products required here.
    return()
  endif()

  # --------------------------------------------------------------------------
  # Non-VS generators (Ninja/Makefiles): explicit FXC/DXC custom commands
  # --------------------------------------------------------------------------
  set(_outdir "${CMAKE_CURRENT_BINARY_DIR}/${CAH_OUTDIR}")
  file(MAKE_DIRECTORY "${_outdir}")
  set(_products)

  # Helper: absolute path to source
  function(_colony_abs SRC OUTVAR)
    if(IS_ABSOLUTE "${SRC}")
      set(${OUTVAR} "${SRC}" PARENT_SCOPE)
    else()
      set(${OUTVAR} "${CMAKE_CURRENT_SOURCE_DIR}/${SRC}" PARENT_SCOPE)
    endif()
  endfunction()

  if(COLONY_D3D_BACKEND STREQUAL "11")
    # ---- DX11 path (FXC, DXBC/SM5.x) -----------------------------------------
    colony_find_fxc(FXC_EXE)

    foreach(src IN LISTS _hlsl_sources)
      get_filename_component(_namewe "${src}" NAME_WE)
      _colony_detect_stage("${_namewe}" _stage)
      if(NOT _stage AND _profile_stage)  set(_stage "${_profile_stage}")  endif()
      if(NOT _stage)                     set(_stage "ps")                 endif()

      # Mesh/Amplification require DX12/DXC (SM6+)
      if(_stage STREQUAL "as" OR _stage STREQUAL "ms")
        message(FATAL_ERROR "Shader '${src}': stage '${_stage}' requires DX12 (SM6.x).")
      endif()

      # Force down to SM5.x when requested model is SM6.* but backend is DX11
      if(_model_us MATCHES "^6_")
        string(REGEX REPLACE "^6_" "5_" _model_us_dx11 "${_model_us}")
      else()
        set(_model_us_dx11 "${_model_us}")
      endif()

      set(_profile "${_stage}_${_model_us_dx11}")
      set(_out "${_outdir}/${_namewe}.${_stage}.cso")
      _colony_abs("${src}" _abs_src)

      set(_fxc_flags "$<$<CONFIG:Debug>:/Zi;/Od>;$<$<CONFIG:Release>:/O3>")
      foreach(d IN LISTS CAH_DEFINES)  list(APPEND _fxc_flags "/D" "${d}")  endforeach()
      foreach(i IN LISTS CAH_INCLUDES) list(APPEND _fxc_flags "/I" "${i}")  endforeach()

      add_custom_command(
        OUTPUT "${_out}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_outdir}"
        COMMAND "${FXC_EXE}" /nologo /T ${_profile} /E "$<IF:$<BOOL:${CAH_ENTRY}>,${CAH_ENTRY},$<1:main>>" /Fo "${_out}" ${_fxc_flags} "${_abs_src}"
        DEPENDS "${_abs_src}"
        BYPRODUCTS "${_out}"
        COMMENT "FXC: ${src} -> ${_out}"
        VERBATIM
      )
      list(APPEND _products "${_out}")
    endforeach()
  else()
    # ---- DX12 path (DXC, DXIL/SM6.x) -----------------------------------------
    # Prefer vcpkg-provided tool, but fall back to common locations if unset.
    if(NOT DEFINED DIRECTX_DXC_TOOL)
      find_program(DIRECTX_DXC_TOOL NAMES dxc dxc.exe
        HINTS
          "$ENV{VCPKG_INSTALLATION_ROOT}/installed/x64-windows/tools/directx-dxc"
          "$ENV{VCPKG_ROOT}/installed/x64-windows/tools/directx-dxc"
          "$ENV{WindowsSdkDir}/bin/x64"
          "C:/Program Files (x86)/Windows Kits/10/bin/x64"
          "C:/Program Files/Windows Kits/10/bin/x64")
    endif()
    if(NOT DIRECTX_DXC_TOOL)
      message(FATAL_ERROR "DirectX Shader Compiler (dxc.exe) not found. Install 'directx-dxc' via vcpkg or set DIRECTX_DXC_TOOL.")
    endif()

    foreach(src IN LISTS _hlsl_sources)
      get_filename_component(_namewe "${src}" NAME_WE)
      _colony_detect_stage("${_namewe}" _stage)
      if(NOT _stage AND _profile_stage)  set(_stage "${_profile_stage}")  endif()
      if(NOT _stage)                     set(_stage "ps")                 endif()

      set(_profile "${_stage}_${_model_us}")
      set(_out "${_outdir}/${_namewe}.${_stage}.cso")
      _colony_abs("${src}" _abs_src)

      set(_dxc_flags "$<$<CONFIG:Debug>:-Zi;-Od>;$<$<CONFIG:Release>:-O3;-Qstrip_debug>")
      foreach(d IN LISTS CAH_DEFINES)  list(APPEND _dxc_flags "-D" "${d}")  endforeach()
      foreach(i IN LISTS CAH_INCLUDES) list(APPEND _dxc_flags "-I" "${i}")  endforeach()

      set(_entry "${CAH_ENTRY}")
      if(NOT _entry OR _entry STREQUAL "")
        _colony_default_entry("${_stage}" _entry)
      endif()

      add_custom_command(
        OUTPUT "${_out}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_outdir}"
        COMMAND "${DIRECTX_DXC_TOOL}" -nologo -T ${_profile} -E "${_entry}" -Fo "${_out}" ${_dxc_flags} "${_abs_src}"
        DEPENDS "${_abs_src}"
        BYPRODUCTS "${_out}"
        COMMENT "DXC: ${src} -> ${_out}"
        VERBATIM
      )
      list(APPEND _products "${_out}")
    endforeach()
  endif()

  add_custom_target(${CAH_TARGET}_shaders ALL DEPENDS ${_products})
  add_dependencies(${CAH_TARGET} ${CAH_TARGET}_shaders)
  set_source_files_properties(${_products} PROPERTIES GENERATED TRUE)
  source_group("Shaders\\Built" FILES ${_products})
endfunction()
