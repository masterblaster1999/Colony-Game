# cmake/shaders.cmake  -- Windows-only HLSL toolchain for Colony-Game

# Ensure sane parsing for $ENV{ProgramFiles\(x86\)} on older 3.x CMake.
if(POLICY CMP0053)
  cmake_policy(SET CMP0053 NEW)
endif()

if(NOT WIN32)
  return()
endif()

# --- Optional DXC (for future D3D12 / SM6). vcpkg defines DIRECTX_DXC_TOOL.
find_package(directx-dxc CONFIG QUIET)
if(directx-dxc_FOUND AND NOT DEFINED DXC_EXE)
  set(DXC_EXE "${DIRECTX_DXC_TOOL}" CACHE FILEPATH "DXC compiler")
  message(STATUS "Using DXC: ${DXC_EXE}")
endif()

# --- FXC is required for D3D11 (SM5.x → DXBC).
# Use SDK-aware hints: CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION + Windows SDK dirs.
set(_fxc_hint_dirs)
if(DEFINED CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION AND CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
  list(APPEND _fxc_hint_dirs
    "$ENV{WindowsSdkDir}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
    "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
    "$ENV{ProgramFiles\(x86\)}/Windows Kits/11/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64")
endif()
list(APPEND _fxc_hint_dirs
  "$ENV{WindowsSdkDir}/bin/x64"
  "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/x64"
  "$ENV{ProgramFiles\(x86\)}/Windows Kits/11/bin/x64"
  )

find_program(FXC_EXE NAMES fxc.exe HINTS ${_fxc_hint_dirs} DOC "Legacy HLSL compiler for D3D11 (SM5.x)")
if(NOT FXC_EXE)
  message(FATAL_ERROR "fxc.exe not found. Install the Windows 10/11 SDK or add fxc.exe to PATH.")
else()
  message(STATUS "Using FXC: ${FXC_EXE}")
endif()

# Helper: compile one HLSL -> .cso
# Usage:
#   cg_compile_hlsl(NAME MyVS SRC ${CMAKE_SOURCE_DIR}/shaders/fullscreen.hlsl
#                   ENTRY main PROFILE vs_5_0 OUTVAR out_blob
#                   DEFS FOO=1;BAR INCLUDEDIRS ${CMAKE_SOURCE_DIR}/shaders/inc)
function(cg_compile_hlsl)
  set(options)
  set(oneValueArgs NAME SRC ENTRY PROFILE OUTVAR)
  set(multiValueArgs DEFS INCLUDEDIRS)
  cmake_parse_arguments(CG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT CG_NAME OR NOT CG_SRC OR NOT CG_ENTRY OR NOT CG_PROFILE OR NOT CG_OUTVAR)
    message(FATAL_ERROR "cg_compile_hlsl: NAME,SRC,ENTRY,PROFILE,OUTVAR are required")
  endif()

  get_filename_component(_stem "${CG_SRC}" NAME_WE)
  set(_outdir "${CMAKE_BINARY_DIR}/shaders/${CMAKE_CFG_INTDIR}")
  set(_out    "${_outdir}/${_stem}.${CG_PROFILE}.cso")
  file(MAKE_DIRECTORY "${_outdir}")

  if(CG_PROFILE MATCHES "_5_") # D3D11 path: FXC → DXBC
    add_custom_command(
      OUTPUT "${_out}"
      MAIN_DEPENDENCY "${CG_SRC}"
      COMMAND "${FXC_EXE}" /nologo /E ${CG_ENTRY} /T ${CG_PROFILE}
              /Fo "${_out}"
              $<$<BOOL:${CG_DEFS}>:/D$<JOIN:${CG_DEFS},\;/D>>
              $<$<BOOL:${CG_INCLUDEDIRS}>:/I$<JOIN:${CG_INCLUDEDIRS},\;/I>>
              $<$<CONFIG:Debug>:/Od /Zi> $<$<CONFIG:RelWithDebInfo>:/Zi>
      COMMENT "FXC ${_stem} (${CG_PROFILE}) → ${_out}"
      VERBATIM)
  else() # future SM6 path: DXC → DXIL (D3D12)
    if(NOT DXC_EXE)
      message(FATAL_ERROR "DXC required for profile ${CG_PROFILE} but dxcompiler not found")
    endif()
    add_custom_command(
      OUTPUT "${_out}"
      MAIN_DEPENDENCY "${CG_SRC}"
      COMMAND "${DXC_EXE}" -nologo -E ${CG_ENTRY} -T ${CG_PROFILE}
              -Fo "${_out}"
              $<$<BOOL:${CG_DEFS}>:-D$<JOIN:${CG_DEFS},;-D>>
              $<$<BOOL:${CG_INCLUDEDIRS}>:-I$<JOIN:${CG_INCLUDEDIRS},;-I>>
              $<$<CONFIG:Debug>:-Od -Zi -Qembed_debug>
      COMMENT "DXC ${_stem} (${CG_PROFILE}) → ${_out}"
      VERBATIM)
  endif()

  set(${CG_OUTVAR} "${_out}" PARENT_SCOPE)
endfunction()
