# cmake/ConfigFingerprint.cmake
include_guard(GLOBAL)

function(colony_compute_fingerprint OUT_VAR)
  # Collect all config variables that should invalidate the cache if changed
  set(_keys
    CMAKE_GENERATOR CMAKE_GENERATOR_PLATFORM CMAKE_BUILD_TYPE
    CMAKE_CXX_STANDARD CMAKE_TOOLCHAIN_FILE VCPKG_ROOT VCPKG_DEFAULT_TRIPLET
    ENABLE_UNITY_BUILD ENABLE_PCH ENABLE_D3D_DEBUG ENABLE_TRACY
    COLONY_DPI_AWARE
  )
  # Read values (empty -> "<unset>" so diffs are stable)
  set(_acc "")
  foreach(k IN LISTS _keys)
    if(DEFINED ${k})
      set(_val "${${k}}")
    else()
      set(_val "<unset>")
    endif()
    string(APPEND _acc "${k}=${_val}\n")
  endforeach()

  # Add hashes of files that affect configuration
  # e.g., manifest for DPI awareness and vcpkg.json lock-in
  foreach(f IN ITEMS
      "${CMAKE_SOURCE_DIR}/CMakeLists.txt"
      "${CMAKE_SOURCE_DIR}/vcpkg.json"
      "${CMAKE_SOURCE_DIR}/app/WinLauncher/app.manifest"
    )
    if(EXISTS "${f}")
      file(SHA256 "${f}" _h)
      string(APPEND _acc "FILE:${f}=${_h}\n")
    endif()
  endforeach()

  # Final digest
  string(SHA256 _digest "${_acc}")
  set(${OUT_VAR} "${_digest}" PARENT_SCOPE)
endfunction()
