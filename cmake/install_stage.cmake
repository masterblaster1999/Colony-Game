# Staging / installation (Windows)
include(GNUInstallDirs)

# Default install prefix to the build's "stage" folder unless the user overrides it.
# This makes: <build>/stage/bin/<Config>/ColonyGame.exe, etc.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX
      "${CMAKE_BINARY_DIR}/stage"
      CACHE PATH "Install path prefix" FORCE)
endif()

# Install the game executable into bin/<Config>
install(TARGETS ColonyGame
        RUNTIME DESTINATION "bin/$<CONFIG>"
)

# Install assets next to the executable (under bin/<Config>)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/shaders/"
        DESTINATION "bin/$<CONFIG>/shaders")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/res/"
        DESTINATION "bin/$<CONFIG>/res")

# Guard against duplicate definitions of the staging target (fixes 'stage_win' collision).
# If some subdirectory already created 'stage_win', this will no-op instead of erroring.
if(NOT TARGET stage_win)
  add_custom_target(stage_win
    COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --config "$<CONFIG>"
    COMMENT "Staging into ${CMAKE_BINARY_DIR}/stage/$<CONFIG>"
  )
endif()
