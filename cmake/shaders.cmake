# cmake/shaders.cmake
include_guard(GLOBAL)

# 11 -> FXC/DXBC (D3D11), 12 -> DXC/DXIL (D3D12)
option(COLONY_D3D_BACKEND "Direct3D backend (11 or 12)" "11")

# Find fxc.exe in common SDK locations or PATH
function(colony_find_fxc OUT_FXC)
  if(NOT WIN32)
    message(FATAL_ERROR "FXC only available on Windows")
  endif()

  set(_cand)
  foreach(_v 10 11 12)
    foreach(_arch x64 x86)
      list(APPEND _cand
        "$ENV{WindowsSdkDir}/bin/${_v}.0/${_arch}/fxc.exe"
        "$ENV{WindowsSdkDir}/bin/${_arch}/fxc.exe")
    endforeach()
  endforeach()

  foreach(p IN LISTS _cand)
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

  message(FATAL_ERROR
    "Could not find fxc.exe. Install the Windows 10/11 SDK (Effect-Compiler Tool).")
endfunction()

# colony_add_hlsl(
#   TARGET   <target_that_depends_on_shaders>
#   OUTDIR   <relative output dir under current binary dir>
#   SOURCES  <.hlsl files>
#   PROFILE  <vs_5_0|ps_5_0|cs_6_7 ...>
#   ENTRY    <entry point, defaults to main>
#   DEFINES  <HLSL #defines>
#   INCLUDES <include dirs>
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
  if(NOT CAH_PROFILE)
    message(FATAL_ERROR "colony_add_hlsl: provide PROFILE (e.g. vs_5_0 / ps_5_0 or vs_6_7)")
  endif()
  if(NOT CAH_ENTRY)
    set(CAH_ENTRY "main")
  endif()

  # ------------------------------------------------------------
  # Visual Studio MSBuild HLSL path (directory-agnostic):
  # Put MSBuild-compiled CSO into ${CMAKE_BINARY_DIR}/res/shaders/$(Configuration)
  # ------------------------------------------------------------
  if(WIN32 AND CMAKE_GENERATOR MATCHES "Visual Studio")
    # Ensure the target knows about the shader sources so MSBuild compiles them.
    target_sources(${CAH_TARGET} PRIVATE ${CAH_SOURCES})
    source_group("Shaders" FILES ${CAH_SOURCES})

    # Use MSBuild variable for per-config output directory (avoids CMake PRE_BUILD).
    set(_msbuild_outdir "${CMAKE_BINARY_DIR}/res/shaders/$(Configuration)")

    foreach(src IN LISTS CAH_SOURCES)
      get_filename_component(_name "${src}" NAME_WE)

      # Derive shader type from PROFILE first, then fall back to filename suffixes.
      set(_stage "")
      if(CAH_PROFILE)
        string(REGEX MATCH "^[a-z]+" _stage "${CAH_PROFILE}") # e.g. 'vs' from 'vs_5_0'
      endif()
      if(NOT _stage)
        if(_name MATCHES "_vs$")       # ..._vs.hlsl
          set(_stage "vs")
        elseif(_name MATCHES "_ps$")
          set(_stage "ps")
        elseif(_name MATCHES "_cs$")
          set(_stage "cs")
        elseif(_name MATCHES "_gs$")
          set(_stage "gs")
        elseif(_name MATCHES "_hs$")
          set(_stage "hs")
        elseif(_name MATCHES "_ds$")
          set(_stage "ds")
        endif()
      endif()

      set(_type "")
      if(_stage STREQUAL "vs")
        set(_type "Vertex")
      elseif(_stage STREQUAL "ps")
        set(_type "Pixel")
      elseif(_stage STREQUAL "cs")
        set(_type "Compute")
      elseif(_stage STREQUAL "gs")
        set(_type "Geometry")
      elseif(_stage STREQUAL "hs")
        set(_type "Hull")
      elseif(_stage STREQUAL "ds")
        set(_type "Domain")
      else()
        message(WARNING "colony_add_hlsl: Could not infer shader type for ${src}; defaulting to Pixel.")
        set(_type "Pixel")
      endif()

      # Derive shader model "M.N" from PROFILE (e.g., vs_5_0 -> "5.0", vs_6_7 -> "6.7")
      set(_model "5.0")
      if(CAH_PROFILE MATCHES "^[a-z]+_([0-9]+)_([0-9]+)$")
        string(REGEX REPLACE "^[a-z]+_([0-9]+)_([0-9]+)$" "\\1.\\2" _model "${CAH_PROFILE}")
      endif()

      # Compose extra flags for MSBuild HLSL tool (defines/includes + per-config opts)
      set(_flags "$<$<CONFIG:Debug>:/Zi;/Od>;$<$<CONFIG:Release>:/O3>")
      foreach(d IN LISTS CAH_DEFINES)
        set(_flags "${_flags};/D;${d}")
      endforeach()
      foreach(i IN LISTS CAH_INCLUDES)
        set(_flags "${_flags};/I;${i}")
      endforeach()

      # Force object file under ${build}/res/shaders/$(Configuration)/ with a stable .cso name.
      set(_obj "${_msbuild_outdir}/${_name}.cso")

      set_source_files_properties("${src}" PROPERTIES
        VS_SHADER_TYPE               "${_type}"
        VS_SHADER_MODEL              "${_model}"
        VS_SHADER_ENTRYPOINT         "${CAH_ENTRY}"
        VS_SHADER_OBJECT_FILE_NAME   "${_obj}"
        VS_SHADER_FLAGS              "${_flags}"
      )
    endforeach()

    # Create output folders at configure time so MSBuild can write .cso without PRE_BUILD.
    if(CMAKE_CONFIGURATION_TYPES)
      foreach(_cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/res/shaders/${_cfg}")
      endforeach()
    else()
      file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/res/shaders")
    endif()

    # MSBuild handles compilation; no explicit custom target/products required here.
    return()
  endif()

  # ------------------------------------------------------------
  # Non-VS generators: explicit FXC/DXC custom commands (original path)
  # ------------------------------------------------------------
  set(_outdir "${CMAKE_CURRENT_BINARY_DIR}/${CAH_OUTDIR}")
  file(MAKE_DIRECTORY "${_outdir}")

  set(_products)

  if(COLONY_D3D_BACKEND STREQUAL "11")
    # ---- DX11 path (FXC, DXBC/SM5.x) -----------------------------------------
    colony_find_fxc(FXC_EXE)
    foreach(src IN LISTS CAH_SOURCES)
      get_filename_component(_name "${src}" NAME)
      set(_out "${_outdir}/${_name}.cso")

      # If a *_6_* profile slips in, map to *_5_* automatically on DX11
      string(REPLACE "_6_" "_5_" _profile "${CAH_PROFILE}")

      set(_defargs "")
      foreach(d IN LISTS CAH_DEFINES)
        list(APPEND _defargs "/D" "${d}")
      endforeach()

      set(_incargs "")
      foreach(i IN LISTS CAH_INCLUDES)
        list(APPEND _incargs "/I" "${i}")
      endforeach()

      add_custom_command(
        OUTPUT "${_out}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_outdir}"
        COMMAND "${FXC_EXE}" /nologo
                /T ${_profile} -E ${CAH_ENTRY}
                /Fo "${_out}" ${_defargs} ${_incargs}
                "${CMAKE_CURRENT_SOURCE_DIR}/${src}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${src}"
        BYPRODUCTS "${_out}"
        COMMENT "FXC: ${src} -> ${_out}"
        VERBATIM)
      list(APPEND _products "${_out}")
    endforeach()
  else()
    # ---- DX12 path (DXC, DXIL/SM6.x) -----------------------------------------
    # Prefer vcpkg-provided tool, but fall back to common locations if unset.
    find_package(directx-dxc CONFIG QUIET)  # may define toolchain targets
    if(NOT DEFINED DIRECTX_DXC_TOOL)
      find_program(DIRECTX_DXC_TOOL NAMES dxc
        HINTS
          "$ENV{VCPKG_INSTALLATION_ROOT}/installed/x64-windows/tools/directx-dxc"
          "$ENV{VCPKG_ROOT}/installed/x64-windows/tools/directx-dxc"
          "$ENV{WindowsSdkDir}/bin/x64"
          "C:/Program Files (x86)/Windows Kits/10/bin/x64"
          "C:/Program Files/Windows Kits/10/bin/x64")
    endif()
    if(NOT DIRECTX_DXC_TOOL)
      message(FATAL_ERROR
        "DirectX Shader Compiler (dxc.exe) not found. "
        "Install 'directx-dxc' via vcpkg or set DIRECTX_DXC_TOOL to the dxc.exe path.")
    endif()

    foreach(src IN LISTS CAH_SOURCES)
      get_filename_component(_name "${src}" NAME)
      set(_out "${_outdir}/${_name}.dxil")

      set(_defargs "")
      foreach(d IN LISTS CAH_DEFINES)
        list(APPEND _defargs "-D" "${d}")
      endforeach()

      set(_incargs "")
      foreach(i IN LISTS CAH_INCLUDES)
        list(APPEND _incargs "-I" "${i}")
      endforeach()

      add_custom_command(
        OUTPUT "${_out}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_outdir}"
        COMMAND "${DIRECTX_DXC_TOOL}" -nologo
                -T ${CAH_PROFILE} -E ${CAH_ENTRY}
                -Fo "${_out}" ${_defargs} ${_incargs}
                "${CMAKE_CURRENT_SOURCE_DIR}/${src}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${src}"
        BYPRODUCTS "${_out}"
        COMMENT "DXC: ${src} -> ${_out}"
        VERBATIM)
      list(APPEND _products "${_out}")
    endforeach()
  endif()

  add_custom_target(${CAH_TARGET}_shaders ALL DEPENDS ${_products})
  add_dependencies(${CAH_TARGET} ${CAH_TARGET}_shaders)

  set_source_files_properties(${_products} PROPERTIES GENERATED TRUE)
  source_group("Shaders\\Built" FILES ${_products})
endfunction()
