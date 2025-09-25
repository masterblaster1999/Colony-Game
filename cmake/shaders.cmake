find_program(DXC_EXE dxc HINTS "$ENV{VCToolsInstallDir}/bin/Hostx64/x64" "$ENV{WindowsSdkDir}/bin/x64")
find_program(FXC_EXE fxc HINTS "$ENV{WindowsSdkDir}/bin/x64")

function(cg_add_hlsl OUT_DIR)
  set(options)
  set(oneValueArgs SRC ENTRY TARGET_PROFILE DEFINES)
  set(multiValueArgs INCLUDES)
  cmake_parse_arguments(CG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(DXC_EXE)
    add_custom_command(
      OUTPUT ${OUT_DIR}/${CG_ENTRY}.cso
      COMMAND ${DXC_EXE} -T ${CG_TARGET_PROFILE} -E ${CG_ENTRY}
              -Fo ${OUT_DIR}/${CG_ENTRY}.cso
              $<$<BOOL:${CG_INCLUDES}>:-I ${CG_INCLUDES}>
              $<$<BOOL:${CG_DEFINES}>:-D ${CG_DEFINES}>
              ${CG_SRC}
      DEPENDS ${CG_SRC}
      COMMENT "DXC ${CG_SRC} -> ${OUT_DIR}/${CG_ENTRY}.cso")
  elseif(FXC_EXE)
    add_custom_command(
      OUTPUT ${OUT_DIR}/${CG_ENTRY}.cso
      COMMAND ${FXC_EXE} /T ${CG_TARGET_PROFILE} /E ${CG_ENTRY}
              /Fo ${OUT_DIR}/${CG_ENTRY}.cso
              ${CG_SRC}
      DEPENDS ${CG_SRC}
      COMMENT "FXC ${CG_SRC} -> ${OUT_DIR}/${CG_ENTRY}.cso")
  else()
    message(FATAL_ERROR "No HLSL compiler (DXC/FXC) found on this Windows machine.")
  endif()
endfunction()
