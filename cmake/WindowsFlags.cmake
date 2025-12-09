# cmake/WindowsFlags.cmake
# Windows + MSVC baseline flags configured safely for multi-config generators.
# Key rule: NEVER add /RTC* globally; add it per-target in Debug ONLY to avoid
# MSVC D8016 when /O2 (Release) is active. See MS docs on /RTC vs /O*. 

if (MSVC)
  # Keep Windows headers lean and avoid std::min/max collisions
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)

  # Modern, strict, exception model and conformance
  add_compile_options(/permissive- /Zc:__cplusplus /Zc:inline /Zc:preprocessor /EHsc)

  # Warnings: /W4 is a good default; /WX can be CI-only (set it elsewhere)
  add_compile_options(/W4)

  # --------------------------------------------------------------------------
  # Per-config tuning (use generator expressions; do NOT append to CMAKE_*FLAGS)
  # NOTE: Debug has /Od and /Zi, but intentionally NO /RTC1 here.
  #       Add /RTC1 per-target with colony_apply_debug_runtime_checks() below.
  # --------------------------------------------------------------------------
  add_compile_options(
    $<$<CONFIG:Debug>:/Od /Zi>
    $<$<CONFIG:RelWithDebInfo>:/O2 /Zi>
    $<$<CONFIG:Release>:/O2>
    $<$<CONFIG:MinSizeRel>:/O1>
  )

  # Multiprocess build: MSVC disables /MP for the PCH-creating TU (warning D9030)
  add_compile_options(/MP)

  # Choose runtime with first-class CMake abstraction (3.15+, CMP0091).
  # Prefer setting this in the root CMakeLists.txt so all targets inherit it:
  # set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# ------------------------------------------------------------------------------
# Helper: opt-in Basic Runtime Checks (/RTC1) for Debug on specific targets.
# This avoids leaking /RTC* into Release/RelWithDebInfo and triggering D8016.
# Usage (in your CMakeLists.txt after target exists):
#   colony_apply_debug_runtime_checks(MyTarget)
# ------------------------------------------------------------------------------
function(colony_apply_debug_runtime_checks tgt)
  if (MSVC AND TARGET "${tgt}")
    target_compile_options(${tgt} PRIVATE $<$<CONFIG:Debug>:/RTC1>)
  endif()
endfunction()

# ------------------------------------------------------------------------------
# Defensive cleanup: strip any stray /RTC* from non-Debug global toolchain vars.
# This guards against props/presets/toolchains injecting /RTC* into Release-like
# configs, which would clash with /O2 and cause D8016.
# ------------------------------------------------------------------------------
if (MSVC)
  foreach(_cfg RELEASE RELWITHDEBINFO MINSIZEREL)
    foreach(_lang C CXX)
      string(TOUPPER "${_cfg}" _CFG_UP)
      set(_VAR "CMAKE_${_lang}_FLAGS_${_CFG_UP}")
      if (DEFINED ${_VAR} AND NOT "${${_VAR}}" STREQUAL "")
        string(REGEX REPLACE "(/RTC1|/RTCs|/RTCu|/RTCc)" "" _FILTERED "${${_VAR}}")
        if (NOT _FILTERED STREQUAL "${${_VAR}}")
          set(${_VAR} "${_FILTERED}" CACHE STRING "" FORCE)
        endif()
      endif()
    endforeach()
  endforeach()
endif()
