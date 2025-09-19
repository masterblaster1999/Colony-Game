# cmake/win/EmbedManifest.cmake
#
# cg_embed_manifest(<target> <manifest-abs-or-rel-path>)
#   - Generates a small .rc that references the manifest with a forward-slash path
#   - Disables the linker's auto-embed to ensure a single manifest (/MANIFEST:NO)
#
function(cg_embed_manifest tgt manifest_path)
  if(NOT WIN32)
    return()
  endif()
  if(NOT TARGET ${tgt})
    message(FATAL_ERROR "cg_embed_manifest: target '${tgt}' not found")
  endif()
  if(NOT EXISTS "${manifest_path}")
    message(WARNING "cg_embed_manifest: manifest does not exist: ${manifest_path}")
  endif()

  file(TO_CMAKE_PATH "${manifest_path}" _manifest_norm)
  set(_rc "${CMAKE_CURRENT_BINARY_DIR}/${tgt}_manifest.rc")

  # 1 is RT_MANIFEST (resource type 24)
  file(WRITE "${_rc}" "1 24 \"${_manifest_norm}\"\n")

  target_sources(${tgt} PRIVATE "${_rc}")

  if(MSVC)
    # Guarantee we don't end up with two manifests
    target_link_options(${tgt} PRIVATE /MANIFEST:NO)
  endif()
endfunction()
