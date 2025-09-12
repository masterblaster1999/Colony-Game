# Copies a single file if it exists: -Dsrc=... -Ddst=...
if(NOT DEFINED src OR src STREQUAL "")
  return()
endif()
if(EXISTS "${src}")
  file(COPY "${src}" DESTINATION "${dst}")
endif()
