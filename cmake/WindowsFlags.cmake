# cmake/WindowsFlags.cmake
if (MSVC)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)

  # Modern, strict, exception model and conformance
  add_compile_options(
    /permissive-
    /Zc:__cplusplus
    /Zc:inline
    /Zc:preprocessor
    /utf-8
    /EHsc
    /W4
    /MP
  )

  # Per-config tuning (one flag per generator expression)
  add_compile_options(
    $<$<CONFIG:Debug>:/Od>
    $<$<CONFIG:Debug>:/Zi>

    $<$<CONFIG:RelWithDebInfo>:/O2>
    $<$<CONFIG:RelWithDebInfo>:/Zi>

    $<$<CONFIG:Release>:/O2>

    $<$<CONFIG:MinSizeRel>:/O1>
  )

  # Runtime selection is best set in root via CMP0091 / CMAKE_MSVC_RUNTIME_LIBRARY.
endif()

# Debug-only /RTC1 opt-in (call this per target)
function(colony_apply_debug_runtime_checks tgt)
  if (MSVC AND TARGET "${tgt}")
    target_compile_options(${tgt} PRIVATE $<$<CONFIG:Debug>:/RTC1>)
  endif()
endfunction()

# Defensive cleanup for non-Debug global flags
if (MSVC)
  foreach(_cfg RELEASE RELWITHDEBINFO MINSIZEREL)
    foreach(_lang C CXX)
      string(TOUPPER "${_cfg}" _CFG_UP)
      set(_VAR "CMAKE_${_lang}_FLAGS_${_CFG_UP}")
      if(DEFINED ${_VAR} AND NOT "${${_VAR}}" STREQUAL "")
        string(REGEX REPLACE "(/RTC1|/RTCs|/RTCu|/RTCc)" "" _FILTERED "${${_VAR}}")
        if (NOT _FILTERED STREQUAL "${${_VAR}}")
          set(${_VAR} "${_FILTERED}" CACHE STRING "" FORCE)
        endif()
      endif()
    endforeach()
  endforeach()
endif()
