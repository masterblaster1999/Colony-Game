# cmake/win/EmbedManifest.cmake
# cg_embed_manifest(<target> <manifest_path>)

if(NOT WIN32)
  function(cg_embed_manifest)
  endfunction()
  return()
endif()

function(cg_embed_manifest target manifest_path)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "cg_embed_manifest: target '${target}' does not exist")
  endif()

  # Normalize path and decide whether to use the provided manifest or a generated default.
  set(_use_path "")
  if(manifest_path AND EXISTS "${manifest_path}")
    file(TO_CMAKE_PATH "${manifest_path}" _use_path)
  else()
    # Generate a minimal, safe default manifest.
    set(_gen "${CMAKE_CURRENT_BINARY_DIR}/${target}_default.manifest")
    file(WRITE "${_gen}" "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>
<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">
  <assemblyIdentity version=\"1.0.0.0\" processorArchitecture=\"*\" name=\"${target}\" type=\"win32\"/>
  <description>${target}</description>
  <trustInfo xmlns=\"urn:schemas-microsoft-com:asm.v3\">
    <security>
      <requestedPrivileges>
        <requestedExecutionLevel level=\"asInvoker\" uiAccess=\"false\"/>
      </requestedPrivileges>
    </security>
  </trustInfo>
  <compatibility xmlns=\"urn:schemas-microsoft-com:compatibility.v1\">
    <application>
      <!-- Windows 10 / 11 -->
      <supportedOS Id=\"{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}\"/>
    </application>
  </compatibility>
  <application xmlns:asmv3=\"urn:schemas-microsoft-com:asm.v3\">
    <asmv3:windowsSettings>
      <asmv3:dpiAware>true/pm</asmv3:dpiAware>
    </asmv3:windowsSettings>
  </application>
</assembly>
")
    file(TO_CMAKE_PATH "${_gen}" _use_path)
    message(WARNING "cg_embed_manifest: '${manifest_path}' not found; using default manifest for target '${target}'.")
  endif()

  # Write an RC that embeds the manifest using a forward-slash path and numeric RT_MANIFEST (24)
  set(_rc "${CMAKE_CURRENT_BINARY_DIR}/${target}_manifest.rc")
  file(WRITE "${_rc}" "1 24 \"${_use_path}\"\n") # 1 RT_MANIFEST "path"
  target_sources(${target} PRIVATE "${_rc}")
  source_group("Generated" FILES "${_rc}")

  # Disable automatic linker embedding to ensure we only have the one above.
  if(MSVC)
    target_link_options(${target} PRIVATE /MANIFEST:NO)
  endif()
endfunction()
