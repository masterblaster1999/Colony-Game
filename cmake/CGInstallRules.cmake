include_guard(GLOBAL)

include(GNUInstallDirs)

# If you already have an install layout module that sets these, this will not override it.
if(NOT DEFINED COLONY_INSTALL_BINDIR)
  set(COLONY_INSTALL_BINDIR "${CMAKE_INSTALL_BINDIR}" CACHE PATH "Install dir for executables")
endif()
if(NOT DEFINED COLONY_INSTALL_DATADIR)
  set(COLONY_INSTALL_DATADIR "${CMAKE_INSTALL_DATADIR}" CACHE PATH "Install dir for game data")
endif()

# Install one EXE + its runtime DLL dependencies (Windows).
function(colony_install_game_executable exe_target)
  if(NOT TARGET "${exe_target}")
    message(STATUS "Install: target '${exe_target}' does not exist; skipping.")
    return()
  endif()

  # Help CMake find DLLs (especially with vcpkg).
  # NOTE: file(GET_RUNTIME_DEPENDENCIES) does NOT magically search PATH the way users expect,
  # so giving DIRECTORIES is usually required in real Windows builds.
  set(_dep_dirs "")

  if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    # Pick the correct vcpkg bin dir based on config.
    list(APPEND _dep_dirs
      "$<IF:$<CONFIG:Debug>,${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin,${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin>"
    )
  endif()

  if(TARGET SDL2::SDL2)
    list(APPEND _dep_dirs "$<TARGET_FILE_DIR:SDL2::SDL2>")
  endif()

  if(DEFINED COLONY_TRACY_TARGET AND NOT "${COLONY_TRACY_TARGET}" STREQUAL "")
    if(TARGET ${COLONY_TRACY_TARGET})
      list(APPEND _dep_dirs "$<TARGET_FILE_DIR:${COLONY_TRACY_TARGET}>")
    endif()
  endif()

  list(REMOVE_DUPLICATES _dep_dirs)

  # IMPORTANT:
  # RUNTIME_DEPENDENCIES must appear BEFORE RUNTIME/LIBRARY/ARCHIVE clauses in install(TARGETS ...).
  # Otherwise CMake parses it as an unknown argument. :contentReference[oaicite:1]{index=1}
  install(TARGETS "${exe_target}"
    RUNTIME_DEPENDENCIES
      PRE_EXCLUDE_REGEXES
        "api-ms-" "ext-ms-"
      POST_EXCLUDE_REGEXES
        ".*[Ww]indows/system32/.*\\.dll"
        ".*[Ww]indows/syswow64/.*\\.dll"
      DIRECTORIES ${_dep_dirs}
    RUNTIME DESTINATION "${COLONY_INSTALL_BINDIR}"
    COMPONENT Runtime
  )
endfunction()

function(colony_install_assets)
  # Keep this conservative; expand as needed.
  if(EXISTS "${CMAKE_SOURCE_DIR}/data")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/data/"
      DESTINATION "${COLONY_INSTALL_DATADIR}/data"
      COMPONENT Runtime
    )
  endif()

  if(EXISTS "${CMAKE_SOURCE_DIR}/resources")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/resources/"
      DESTINATION "${COLONY_INSTALL_DATADIR}/resources"
      COMPONENT Runtime
    )
  endif()

  if(EXISTS "${CMAKE_SOURCE_DIR}/res")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/res/"
      DESTINATION "${COLONY_INSTALL_DATADIR}/res"
      COMPONENT Runtime
    )
  endif()
endfunction()

function(colony_install_all)
  # Main game exe (supports either fixed target or override)
  if(TARGET ColonyGame)
    colony_install_game_executable(ColonyGame)
  elseif(DEFINED COLONY_GAME_TARGET AND TARGET "${COLONY_GAME_TARGET}")
    colony_install_game_executable("${COLONY_GAME_TARGET}")
  endif()

  # Optional tools
  if(TARGET ColonyLauncher)
    colony_install_game_executable(ColonyLauncher)
  endif()
  if(TARGET ColonyMapViewer)
    colony_install_game_executable(ColonyMapViewer)
  endif()

  colony_install_assets()
endfunction()
