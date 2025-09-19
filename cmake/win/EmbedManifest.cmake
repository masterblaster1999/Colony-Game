# cmake/win/EmbedManifest.cmake
# Generate a per-target RC with a single explicit RT_MANIFEST and turn off the linker's auto-embed.

function(cg_embed_manifest tgt manifest_path)
  if(NOT MSVC)
    return() # Windows/MSVC only
  endif()

  if(NOT TARGET ${tgt})
    message(FATAL_ERROR "cg_embed_manifest: target '${tgt}' does not exist")
  endif()

  if(NOT EXISTS "${manifest_path}")
    message(FATAL_ERROR "cg_embed_manifest: manifest not found: ${manifest_path}")
  endif()

  # Normalize path to forward slashes for RC
  get_filename_component(_abs "${manifest_path}" ABSOLUTE)
  file(TO_CMAKE_PATH "${_abs}" _abs_fs)

  # Emit a tiny RC that embeds just this manifest
  set(_rc "${CMAKE_CURRENT_BINARY_DIR}/${tgt}_manifest.rc")
  file(WRITE "${_rc}" "1 RT_MANIFEST \"${_abs_fs}\"\n")

  # Add to target and prevent linker from auto-embedding a second manifest
  target_sources(${tgt} PRIVATE "${_rc}")
  target_link_options(${tgt} PRIVATE "/MANIFEST:NO")
endfunction()
