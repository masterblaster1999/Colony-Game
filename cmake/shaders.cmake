# cmake/Shaders.cmake
# Compiles HLSL with DXC and picks the correct stage from the filename suffix.
# Supports: *_vs.hlsl, *_ps.hlsl, *_cs.hlsl (extend as needed).

function(colony_add_hlsl OUT_VAR)
  set(options)
  set(oneValueArgs OUTDIR)
  set(multiValueArgs FILES INCLUDES DEFINES)
  cmake_parse_arguments(HLSL "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if (NOT HLSL_OUTDIR)
    message(FATAL_ERROR "colony_add_hlsl: OUTDIR is required")
  endif()

  # Find DXC on Windows (prefers the one from PATH / Windows SDK / vcpkg).
  if (WIN32)
    find_program(DXC_EXECUTABLE NAMES dxc HINTS
      "$ENV{VCToolsInstallDir}/bin/Hostx64/x64"
      "$ENV{WindowsSdkDir}/bin/x64"
      "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/dxc"
    )
  endif()
  if (NOT DXC_EXECUTABLE)
    message(FATAL_ERROR "colony_add_hlsl: dxc.exe not found")
  endif()

  file(MAKE_DIRECTORY "${HLSL_OUTDIR}")

  set(_outputs)
  foreach(SRC IN LISTS HLSL_FILES)
    get_filename_component(NAME "${SRC}" NAME_WE)
    get_filename_component(ABS  "${SRC}" ABSOLUTE)

    # Infer stage from suffix
    set(PROFILE "")
    if (NAME MATCHES "_vs$")
      set(PROFILE "vs_6_0")
    elseif (NAME MATCHES "_ps$")
      set(PROFILE "ps_6_0")
    elseif (NAME MATCHES "_cs$")
      set(PROFILE "cs_6_0")
    else()
      message(FATAL_ERROR "Unknown shader stage for ${SRC} (expected *_vs/_ps/_cs.hlsl)")
    endif()

    # Output .cso next to other binary outputs
    set(OUT "${HLSL_OUTDIR}/${NAME}.cso")

    # Compose include/define args
    set(INC_ARGS "")
    foreach(inc IN LISTS HLSL_INCLUDES)
      list(APPEND INC_ARGS "-I" "${inc}")
    endforeach()

    set(DEF_ARGS "")
    foreach(def IN LISTS HLSL_DEFINES)
      list(APPEND DEF_ARGS "-D" "${def}")
    endforeach()

    # Debug info in Debug; optimized in Release
    set(DCOMMON -E main -T ${PROFILE} ${INC_ARGS} ${DEF_ARGS})
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
      list(APPEND DCOMMON -Zi -Qembed_debug -Od -WX)
    else()
      list(APPEND DCOMMON -O3 -Qstrip_debug -Qstrip_reflect -WX)
    endif()

    add_custom_command(
      OUTPUT  "${OUT}"
      COMMAND "${DXC_EXECUTABLE}" ${DCOMMON} -Fo "${OUT}" "${ABS}"
      DEPENDS "${ABS}"
      COMMENT "HLSL: ${NAME}.hlsl -> ${NAME}.cso (${PROFILE})"
      VERBATIM
    )
    list(APPEND _outputs "${OUT}")
  endforeach()

  set(${OUT_VAR} "${_outputs}" PARENT_SCOPE)
endfunction()
