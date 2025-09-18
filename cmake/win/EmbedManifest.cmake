# EmbedManifest.cmake — Windows-only manifest embedding helper
# Generates a per-target RC with a forward-slash path to avoid RC backslash escapes.
# Uses a tiny template: cmake/win/embed_manifest_only.rc.in

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

  # Template lives next to this file
  set(_RC_IN  "${CMAKE_CURRENT_LIST_DIR}/embed_manifest_only.rc.in")
  set(_RC_OUT "${_RC_DIR}/embed_manifest_only.rc")

  # The template expects APP_MANIFEST_CMAKE
  set(APP_MANIFEST_CMAKE "${_MANIFEST_FWD}")
  configure_file("${_RC_IN}" "${_RC_OUT}" @ONLY)

  target_sources(${TARGET_NAME} PRIVATE "${_RC_OUT}")
endfunction()
