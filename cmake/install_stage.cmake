include(GNUInstallDirs)
install(TARGETS ColonyGame RUNTIME DESTINATION "bin/$<CONFIG>")

# Copy shaders & assets
install(DIRECTORY ${CMAKE_SOURCE_DIR}/shaders/ DESTINATION "stage/$<CONFIG>/shaders")
install(DIRECTORY ${CMAKE_SOURCE_DIR}/res/     DESTINATION "stage/$<CONFIG>/res")

add_custom_target(stage_win
  COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --config $<CONFIG>
  COMMENT "Staging into build/stage/$<CONFIG>")
