# CMakeLists.txt (end)
add_custom_target(stage_win ALL
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_BINARY_DIR}/stage/bin"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/stage/bin"
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:ColonyGame> "${CMAKE_BINARY_DIR}/stage/bin/"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets"  "${CMAKE_BINARY_DIR}/stage/bin/assets"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/res"     "${CMAKE_BINARY_DIR}/stage/bin/res"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/shaders" "${CMAKE_BINARY_DIR}/stage/bin/shaders"
  # Optional: copy PDBs in RelWithDebInfo
)
