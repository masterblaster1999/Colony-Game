# cmake/HlslDxil.cmake
# Offline HLSL -> DXIL compilation with DXC (Windows-only).
# Usage example is below this file.

cmake_policy(PUSH)
# Modern CMake has cmake_parse_arguments built-in, but include for safety:
include(CMakeParseArguments)

# Find dxc.exe. Prefer vcpkg's directx-dxc tool if available.
function(_find_dxc DXC_OUT)
  # If vcpkg's directx-dxc was found, it sets DIRECTX_DXC_TOOL. Use that first.
  if(DEFINED DIRECTX_DXC_TOOL AND EXISTS "${DIRECTX_DXC_TOOL}")
    set(_dxc "${DIRECTX_DXC_TOOL}")
  else()
    # Fallback: try PATH and a few common SDK/vcpkg locations.
    find_program(_dxc
      NAMES dxc.exe dxc
      HINTS
        "$ENV{VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/directx-dxc"
        "$ENV{VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/tools/directx-dxc"
        "$ENV{ProgramFiles(x86)}/Windows Kits/10/bin"
        "$ENV{ProgramFiles}/Windows Kits/10/bin"
      DOC "Path to the DirectX Shader Compiler (dxc.exe)")
  endif()

  if(NOT _dxc)
    message(FATAL_ERROR
      "dxc.exe not found. Install vcpkg port 'directx-dxc' (recommended), "
      "or ensure dxc.exe from the Windows 10+ SDK or DXC release is on PATH.")
  endif()

  set(${DXC_OUT} "${_dxc}" PARENT_SCOPE)
endfunction()

# hlsl_dxc_compile(
#   TARGET <custom_target_name>
#   OUT_DIR <dir>                        # default: ${CMAKE_BINARY_DIR}/res/shaders
#   # One or more "FILE:ENTRY:PROFILE" items:
#   ENTRIES
#     "<abs-or-rel-path>.hlsl:main:ps_6_7"
#     "<abs-or-rel-path>.hlsl:VSMain:vs_6_7"
#     "<abs-or-rel-path>.hlsl:csMain:cs_6_7"
#   [INCLUDES <dirs...>]
#   [DEFINES  <macros...>]               # e.g. FOO=1;BAR
#   [FLAGS    <extra dxc args...>]       # e.g. -WX;-Zpr;-enable-16bit-types
#   [HLSL2021 <ON|OFF>]                  # default ON -> adds -HV 2021
#   [INSTALL_DIR <dir>]                  # default: res/shaders
# )
function(hlsl_dxc_compile)
  set(options)
  set(oneValueArgs TARGET OUT_DIR INSTALL_DIR HLSL2021)
  set(multiValueArgs ENTRIES INCLUDES DEFINES FLAGS)
  cmake_parse_arguments(HLSL "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT HLSL_TARGET)
    message(FATAL_ERROR "hlsl_dxc_compile: TARGET is required")
  endif()

  if(NOT HLSL_ENTRIES)
    message(FATAL_ERROR "hlsl_dxc_compile: at least one ENTRIES item is required (FILE:ENTRY:PROFILE)")
  endif()

  if(NOT HLSL_OUT_DIR)
    set(HLSL_OUT_DIR "${CMAKE_BINARY_DIR}/res/shaders")
  endif()

  if(NOT DEFINED HLSL_HLSL2021)
    set(HLSL_HLSL2021 ON)
  endif()

  if(NOT HLSL_INSTALL_DIR)
    set(HLSL_INSTALL_DIR "res/shaders")
  endif()

  _find_dxc(DXC_EXE)

  set(all_outputs)
  foreach(item IN LISTS HLSL_ENTRIES)
    # Parse "FILE:ENTRY:PROFILE"
    string(REPLACE ":" ";" _parts "${item}")
    list(LENGTH _parts _n)
    if(NOT _n EQUAL 3)
      message(FATAL_ERROR "Bad ENTRIES item '${item}'. Expected: FILE:ENTRY:PROFILE")
    endif()

    list(GET _parts 0 SRC)
    list(GET _parts 1 ENTRY)
    list(GET _parts 2 PROFILE)

    # Normalize/validate source path:
    if(NOT IS_ABSOLUTE "${SRC}")
      set(SRC "${CMAKE_CURRENT_SOURCE_DIR}/${SRC}")
    endif()
    if(NOT EXISTS "${SRC}")
      message(FATAL_ERROR "HLSL source not found: ${SRC}")
    endif()

    get_filename_component(SRC_NAME_WE "${SRC}" NAME_WE)

    # Per-config outputs:
    set(OUT_FILE "${HLSL_OUT_DIR}/$<CONFIG>/${SRC_NAME_WE}_${ENTRY}_${PROFILE}.dxil")
    set(PDB_FILE "${HLSL_OUT_DIR}/$<CONFIG>/${SRC_NAME_WE}_${ENTRY}_${PROFILE}.pdb")

    # Common DXC args:
    set(_args)
    list(APPEND _args -nologo -E "${ENTRY}" -T "${PROFILE}")
    if(HLSL_HLSL2021)
      list(APPEND _args -HV 2021)  # enable HLSL 2021 language features
    endif()
    foreach(inc IN LISTS HLSL_INCLUDES)
      list(APPEND _args -I "${inc}")
    endforeach()
    foreach(def IN LISTS HLSL_DEFINES)
      list(APPEND _args -D "${def}")
    endforeach()
    list(APPEND _args ${HLSL_FLAGS})

    # Config-specific: Debug gets symbols embedded; Release strips debug/reflect and optimizes.
    # (We still emit a separate PDB via -Fd for debugging tools.)
    add_custom_command(
      OUTPUT "${OUT_FILE}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${HLSL_OUT_DIR}/$<CONFIG>"
      COMMAND "${DXC_EXE}"
              ${_args}
              $<$<CONFIG:Debug>:-Od>
              $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:-Zi>
              $<$<CONFIG:Debug>:-Qembed_debug>
              $<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:-O3>
              $<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:-Qstrip_debug -Qstrip_reflect>
              -Fo "${OUT_FILE}"
              -Fd "${PDB_FILE}"
              "${SRC}"
      MAIN_DEPENDENCY "${SRC}"
      DEPENDS "${SRC}"
      VERBATIM
      COMMENT "DXC ${PROFILE} ${SRC_NAME_WE}:${ENTRY} -> ${OUT_FILE}"
    )

    list(APPEND all_outputs "${OUT_FILE}")
  endforeach()

  add_custom_target(${HLSL_TARGET} DEPENDS ${all_outputs})

  # Optionally install the compiled DXIL blobs:
  install(FILES ${all_outputs}
          DESTINATION "${HLSL_INSTALL_DIR}"
          CONFIGURATIONS Debug Release RelWithDebInfo MinSizeRel)

  # Export outputs to caller if they want to link/copy further:
  set(${HLSL_TARGET}_DXIL_OUTPUTS ${all_outputs} PARENT_SCOPE)
endfunction()

cmake_policy(POP)
