# cmake/shaders.cmake
include_guard(GLOBAL)

# Options:
#   COLONY_D3D_BACKEND: "11" (default) or "12"
#       11 -> compile HLSL with FXC to DXBC (D3D11)
#       12 -> compile HLSL with DXC to DXIL (D3D12)
option(COLONY_D3D_BACKEND "Direct3D backend (11 or 12)" "11")

# Helper: locate FXC when using DX11
function(colony_find_fxc OUT_FXC)
  if(NOT WIN32)
    message(FATAL_ERROR "FXC only available on Windows")
  endif()

  # Typical Windows SDK locations:
  #   %WindowsSdkDir%\bin\<ver>\x64\fxc.exe
  # Try cmake's vs env hints and registry-based variables
  set(_cand)
  foreach(_v 10 11 12)
    foreach(_arch x64 x86)
      list(APPEND _cand
        "$ENV{WindowsSdkDir}/bin/${_v}.0/${_arch}/fxc.exe"
        "$ENV{WindowsSdkDir}/bin/${_arch}/fxc.exe"
      )
    endforeach()
  endforeach()

  foreach(p IN LISTS _cand)
    file(TO_CMAKE_PATH "${p}" p2)
    if(EXISTS "${p2}")
      set(${OUT_FXC} "${p2}" PARENT_SCOPE)
      return()
    endif()
  endforeach()

  # As a fallback, rely on PATH (VS Developer Prompt usually has it)
  find_program(_fxc NAMES fxc.exe)
  if(_fxc)
    set(${OUT_FXC} "${_fxc}" PARENT_SCOPE)
    return()
  endif()

  message(FATAL_ERROR "Could not find fxc.exe. Install the Windows 10/11 SDK. See Microsoft 'Effect-Compiler Tool (FXC)'.")
endfunction()

# Function:
#   colony_add_hlsl(
#       TARGET <cxx-target>
#       OUTDIR <binary-relpath>
#       SOURCES <list of .hlsl files>
#       PROFILE <vs_5_0|ps_5_0|cs_5_0|... or *_6_7 when DX12>
#       ENTRY <main or user entry>
#       DEFINES <optional list>
#       INCLUDES <optional list of include dirs for #include in shaders>
#   )
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
    message(FATAL_ERROR "colony_add_hlsl: provide PROFILE (e.g. vs_5_0 / ps_5_0 for DX11, or vs_6_7 for DX12)")
  endif()
  if(NOT CAH_ENTRY)
    set(CAH_ENTRY "main")
  endif()

  set(_outdir "${CMAKE_CURRENT_BINARY_DIR}/${CAH_OUTDIR}")
  file(MAKE_DIRECTORY "${_outdir}")

  set(_products)
  set(_commands)

  if(COLONY_D3D_BACKEND STREQUAL "11")
    # DX11 -> FXC
    colony_find_fxc(FXC_EXE)  # may message(FATAL_ERROR) if not found
    foreach(src IN LISTS CAH_SOURCES)
      get_filename_component(_name "${src}" NAME)
      set(_out "${_outdir}/${_name}.cso")

      # Map profile *_5_0 by default if user passed *_6_x accidentally
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
        COMMAND "${FXC_EXE}"
            /nologo
            /T ${_profile}
            /E ${CAH_ENTRY}
            /Fo "${_out}"
            ${_defargs}
            ${_incargs}
            "${CMAKE_CURRENT_SOURCE_DIR}/${src}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${src}"
        BYPRODUCTS "${_out}"
        COMMENT "FXC: ${src} -> ${_out}"
        VERBATIM
      )
      list(APPEND _products "${_out}")
    endforeach()

  else()
    # DX12 -> DXC via vcpkg target (DXIL)
    # vcpkg exposes: find_package(directx-dxc CONFIG REQUIRED) + Microsoft::DirectXShaderCompiler
    find_package(directx-dxc CONFIG REQUIRED)  # per vcpkg usage text
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
        COMMAND $<TARGET_FILE:Microsoft::DirectXShaderCompiler>
            -nologo
            -T ${CAH_PROFILE}      # e.g. vs_6_7, ps_6_7
            -E ${CAH_ENTRY}
            -Fo "${_out}"
            ${_defargs}
            ${_incargs}
            "${CMAKE_CURRENT_SOURCE_DIR}/${src}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${src}"
        BYPRODUCTS "${_out}"
        COMMENT "DXC: ${src} -> ${_out}"
        VERBATIM
      )
      list(APPEND _products "${_out}")
    endforeach()
  endif()

  # Create a buildable target for the shader outputs and make the game depend on it.
  add_custom_target(${CAH_TARGET}_shaders ALL DEPENDS ${_products})

  # Important: the dependent target must exist *somewhere* in the project,
  # add_dependencies works across directories (unlike add_custom_command(TARGET...)).
  add_dependencies(${CAH_TARGET} ${CAH_TARGET}_shaders)

  # Optional: make outputs visible in IDE
  set_source_files_properties(${_products} PROPERTIES GENERATED TRUE)
  source_group("Shaders\\Built" FILES ${_products})
endfunction()
