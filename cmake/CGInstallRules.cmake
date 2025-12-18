# cmake/CGInstallRules.cmake
# Centralized install() rules for targets + headers + assets.
# Uses install(TARGETS ... RUNTIME/LIBRARY/ARCHIVE DESTINATION ...) :contentReference[oaicite:4]{index=4}
# Uses install(TARGETS ... RUNTIME_DEPENDENCIES ...) to bundle DLLs (CMake >= 3.21). :contentReference[oaicite:5]{index=5}

include_guard(GLOBAL)

function(colony_install_library_targets)
  foreach(_t IN ITEMS colony_core colony_nav colony_renderer colony_engine colony_platform_win)
    if(TARGET "${_t}")
      install(TARGETS "${_t}"
        EXPORT ColonyGameTargets
        RUNTIME DESTINATION "${COLONY_INSTALL_BINDIR}"
        LIBRARY DESTINATION "${COLONY_INSTALL_LIBDIR}"
        ARCHIVE DESTINATION "${COLONY_INSTALL_LIBDIR}"
        INCLUDES DESTINATION "${COLONY_INSTALL_INCLUDEDIR}"
      )
    endif()
  endforeach()
endfunction()

function(colony_install_game_executable exe)
  if(NOT TARGET "${exe}")
    return()
  endif()

  # Bundles runtime DLL dependencies next to the EXE (portable install).
  # Excludes Windows API-set pseudo DLLs and system32 DLLs.
  install(TARGETS "${exe}"
    RUNTIME DESTINATION "${COLONY_INSTALL_BINDIR}"
    RUNTIME_DEPENDENCIES
      PRE_EXCLUDE_REGEXES
        "api-ms-.*"
        "ext-ms-.*"
      POST_EXCLUDE_REGEXES
        ".*[Ss]ystem32/.*\\.dll"
        ".*[Ss]yswow64/.*\\.dll"
  )
endfunction()

function(colony_install_headers_and_content)
  # Public headers
  if(EXISTS "${CMAKE_SOURCE_DIR}/include")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/include/"
      DESTINATION "${COLONY_INSTALL_INCLUDEDIR}"
    )
  endif()

  # Game content folders (install if they exist)
  if(EXISTS "${CMAKE_SOURCE_DIR}/assets")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/"
      DESTINATION "${COLONY_INSTALL_ASSETSDIR}"
    )
  endif()

  if(EXISTS "${CMAKE_SOURCE_DIR}/data")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/data/"
      DESTINATION "${COLONY_INSTALL_DATADIR}"
    )
  endif()

  if(EXISTS "${CMAKE_SOURCE_DIR}/resources")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/resources/"
      DESTINATION "${COLONY_INSTALL_RESDIR}"
    )
  endif()

  # Some projects also have a "res" folder (optional).
  if(EXISTS "${CMAKE_SOURCE_DIR}/res")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/res/"
      DESTINATION "${COLONY_INSTALL_RESDIR}/res"
    )
  endif()

  # Compiled shader output (best-effort). Adjust if your CGShaders puts output elsewhere.
  if(EXISTS "${CMAKE_BINARY_DIR}/shaders")
    install(DIRECTORY "${CMAKE_BINARY_DIR}/shaders/"
      DESTINATION "${COLONY_INSTALL_SHADERDIR}"
    )
  endif()
endfunction()

function(colony_install_all)
  colony_install_library_targets()
  colony_install_game_executable(ColonyGame)
  colony_install_headers_and_content()

  # Export set (optional; useful if you ever want to consume libs from another build)
  install(EXPORT ColonyGameTargets
    NAMESPACE colony::
    DESTINATION "${COLONY_INSTALL_LIBDIR}/cmake/ColonyGame"
  )
endfunction()
