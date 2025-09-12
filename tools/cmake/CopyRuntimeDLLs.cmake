# Copies all files from a semicolon-separated list: -Ddlls="a;b;c" -Ddst=...
if(NOT DEFINED dlls OR dlls STREQUAL "")
  return()
endif()
foreach(dll IN LISTS dlls)
  if(EXISTS "${dll}")
    file(COPY "${dll}" DESTINATION "${dst}")
  endif()
endforeach()
