# EmbedManifest.cmake — Windows-only manifest embedding helper
# Generates a per-target RC with a forward-slash path to avoid RC backslash escapes.
# Uses a tiny template: cmake/win/embed_manifest_only.rc.in
#
# NOTE:
# - Inside a function(), CMAKE_CURRENT_LIST_DIR refers to the *caller*'s listfile.
#   We therefore use CMAKE_CURRENT_FUNCTION_LIST_DIR when available (CMake >= 3.17)
#   and fall back to a module-dir snapshot captured at include time for 3.16.

# Capture this module's directory at include time (fallback for CMake < 3.17).
set(_EMBEDMANIFEST_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(cg_embed_manifest TARGET_NAME MANIFEST_PATH)
  if(NOT WIN32)
    return()
  endif()

  if(NOT TARGET "${TARGET_NAME}")
    message(FATAL_ERROR "cg_embed_manifest: target not found: ${TARGET_NAME}")
  endif()

  # Normalize to forward slashes for rc.exe (prevents \a, \b escapes, etc.)
  file(TO_CMAKE_PATH "${MANIFEST_PATH}" _MANIFEST_FWD)

  # Output a per-target RC to keep unity/jumbo resource builds clean
  set(_RC_DIR "${CMAKE_CURRENT_BINARY_DIR}/rc/${TARGET_NAME}")
  file(MAKE_DIRECTORY "${_RC_DIR}")

  # Template lives next to this module (not the caller's CMakeLists).
  if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.17")
    # Directory of the listfile that *defined* this function
    # (available only inside function()).
    set(_RC_IN "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/embed_manifest_only.rc.in")
  else()
    # CMake 3.16 fallback: use the module dir captured at include time.
    set(_RC_IN "${_EMBEDMANIFEST_MODULE_DIR}/embed_manifest_only.rc.in")
  endif()

  set(_RC_OUT "${_RC_DIR}/embed_manifest_only.rc")

  if(NOT EXISTS "${_RC_IN}")
    message(FATAL_ERROR
      "cg_embed_manifest: template not found: ${_RC_IN}\n"
      "Expected 'embed_manifest_only.rc.in' to be next to EmbedManifest.cmake.")
  endif()

  # The template expects APP_MANIFEST_CMAKE
  set(APP_MANIFEST_CMAKE "${_MANIFEST_FWD}")
  configure_file("${_RC_IN}" "${_RC_OUT}" @ONLY)

  target_sources(${TARGET_NAME} PRIVATE "${_RC_OUT}")
endfunction()
