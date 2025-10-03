# cmake/shaders.cmake  (Windows only)
include_guard(GLOBAL)

if(NOT WIN32)
  message(FATAL_ERROR "This shaders.cmake is Windows-only by design.")
endif()

# --- Locate FXC (DXBC compiler, SM 2..5.1) ---
# Escape ProgramFiles(x86) in CMake: $ENV{ProgramFiles\(x86\)}
set(_FXC_HINTS
  "$ENV{ProgramFiles\\(x86\\)}/Windows Kits/11/bin"
  "$ENV{ProgramFiles\\(x86\\)}/Windows Kits/10/bin"
  "$ENV{ProgramW6432}/Windows Kits/11/bin"
  "$ENV{ProgramW6432}/Windows Kits/10/bin"
  "$ENV{WindowsSdkDir}/bin"
)

find_program(FXC_EXE NAMES fxc.exe
  HINTS ${_FXC_HINTS}
  PATH_SUFFIXES x64 10.0.22621.0/x64 10.0.22000.0/x64
  NO_CACHE
)

if(NOT FXC_EXE)
  # Fallback: search PATH (SDK often adds fxc there)
  find_program(FXC_EXE NAMES fxc.exe)
endif()

if(NOT FXC_EXE)
  message(FATAL_ERROR
    "fxc.exe not found. Install Windows 10/11 SDK (HLSL Tools). "
    "Searched:\n  ${_FXC_HINTS}\n"
    "Or ensure fxc.exe is on PATH.")
endif()

# Output root (multi-config aware)
set(COLONY_SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/shaders/$<CONFIG>")

# Compile one HLSL source to a .cso using FXC.
# Usage:
#   cg_compile_hlsl(<src> ENTRY <E> PROFILE <P> [DEFINES k=v ...] [INCLUDEDIRS dir1 ...])
function(cg_compile_hlsl CG_SRC)
  set(options)
  set(oneValueArgs ENTRY PROFILE)
  set(multiValueArgs DEFINES INCLUDEDIRS)
  cmake_parse_arguments(CG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT EXISTS "${CG_SRC}")
    message(FATAL_ERROR "cg_compile_hlsl: source not found: ${CG_SRC}")
  endif()
  if(NOT CG_ENTRY)
    message(FATAL_ERROR "cg_compile_hlsl: ENTRY is required (e.g. VSMain/PSMain/CSMain).")
  endif()
  if(NOT CG_PROFILE)
    message(FATAL_ERROR "cg_compile_hlsl: PROFILE is required (e.g. vs_5_0, ps_5_0, cs_5_0).")
  endif()

  file(TO_CMAKE_PATH "${CG_SRC}" _src_norm)
  cmake_path(GET _src_norm FILENAME _srcname)
  cmake_path(GET _src_norm PARENT_PATH _srcdir)

  set(_out "${COLONY_SHADER_OUT_DIR}/${_srcname}.cso")
  set(_pdb "${COLONY_SHADER_OUT_DIR}/${_srcname}.pdb")
  set(_asm "${COLONY_SHADER_OUT_DIR}/${_srcname}.asm")

  # Always include the source's own directory + common shader dirs you use.
  set(_incs "${_srcdir};${CMAKE_SOURCE_DIR}/src;${CMAKE_SOURCE_DIR}/src/pcg/shaders")
  if(CG_INCLUDEDIRS)
    list(APPEND _incs ${CG_INCLUDEDIRS})
  endif()
  list(REMOVE_DUPLICATES _incs)

  # Construct /I flags in native form
  set(_inc_flags "")
  foreach(d IN LISTS _incs)
    file(TO_NATIVE_PATH "${d}" _nd)
    list(APPEND _inc_flags "/I\"${_nd}\"")
  endforeach()

  # Defines -> /D
  set(_def_flags "")
  foreach(kv IN LISTS CG_DEFINES)
    list(APPEND _def_flags "/D${kv}")
  endforeach()

  # Base FXC flags
  set(_fxc_flags
    /nologo
    /E "${CG_ENTRY}"
    /T "${CG_PROFILE}"
    ${_def_flags}
    ${_inc_flags}
    /Fo "${_out}"
    /Fd "${_pdb}"
    /Fc "${_asm}"
  )

  # Debug/Dev friendly flags; Release optimized (/O3 is valid for FXC)
  set(_fxc_flags_debug "/Zi" "/Od")
  set(_fxc_flags_release "/O3")

  add_custom_command(
    OUTPUT "${_out}"
    BYPRODUCTS "${_pdb}" "${_asm}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${COLONY_SHADER_OUT_DIR}"
    # Use source dir as working dir so relative #includes behave intuitively
    COMMAND "${FXC_EXE}" ${_fxc_flags}
            $<$<CONFIG:Debug>:${_fxc_flags_debug}>
            $<$<CONFIG:RelWithDebInfo>:${_fxc_flags_debug}>
            $<$<CONFIG:Release>:${_fxc_flags_release}>
            "${_src_norm}"
    COMMAND ${CMAKE_COMMAND} -E echo "FXC OK: ${_srcname} -> ${_out}"
    DEPENDS "${CG_SRC}"
    WORKING_DIRECTORY "${_srcdir}"
    VERBATIM
    USES_TERMINAL
    COMMENT "FXC ${CG_PROFILE} ${CG_SRC} -> ${_out}"
  )

  # return path to .cso to the caller
  set(CG_OUT "${_out}" PARENT_SCOPE)
endfunction()
