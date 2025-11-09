# cmake/cg_paths.cmake
include_guard(GLOBAL)
if(POLICY CMP0053)
  cmake_policy(SET CMP0053 NEW)
endif()

function(cg_get_pf86 OUT_VAR)
  set(_pf "")
  if(DEFINED ENV{ProgramFiles(x86)})
    set(_pf "$ENV{ProgramFiles\(x86\)}")
    file(TO_CMAKE_PATH "${_pf}" _pf)
  endif()
  set(${OUT_VAR} "${_pf}" PARENT_SCOPE)
endfunction()
