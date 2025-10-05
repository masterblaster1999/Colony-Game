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
#   DEFINES  <HLSL#defines>
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
    find_package(directx-dxc CONFIG REQUIRED)  # provides Microsoft::DirectXShaderCompiler
    if(NOT DEFINED DIRECTX_DXC_TOOL)
      message(FATAL_ERROR
        "directx-dxc found, but DIRECTX_DXC_TOOL is not set; cannot run dxc.exe")
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
