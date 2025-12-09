# cmake/NoFlagsInSources.cmake
# Detects and auto‑removes compile flags or generator expressions that
# incorrectly landed in a target's SOURCES list (source of $<0:/O2> C1083).

function(colony_fixup_sources tgt)
  if(NOT TARGET "${tgt}")
    return()
  endif()

  get_target_property(_srcs "${tgt}" SOURCES)
  if(NOT _srcs)
    return()
  endif()

  set(_clean "")
  set(_bad   "")
  foreach(_s IN LISTS _srcs)
    # Clearly-not-a-path: raw flags or malformed genex
    if(_s MATCHES "^(/|-)(O[0-3]|Od|RTC[^;]*|Z[^;]*|G[^;]*)(;.*)?$"      # /O2 /Od /RTC* /Z* /G*
       OR _s MATCHES "^\\$<[^>]*>")                                     # any $<...> token
      list(APPEND _bad "${_s}")
    else()
      list(APPEND _clean "${_s}")
    endif()
  endforeach()

  if(_bad)
    message(WARNING
      "Purifying target '${tgt}': removing non-source entries from SOURCES:\n  ${_bad}\n"
      "If you see this, somewhere a flag was appended to a source list. "
      "Move flags into target_compile_options()/target_link_options().")
    # Replace SOURCES with the cleaned list
    set_property(TARGET "${tgt}" PROPERTY SOURCES "${_clean}")
  endif()
endfunction()
