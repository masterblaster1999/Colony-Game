# In the CMake that defines colony_core (or the target that compiles jps.cpp):
target_include_directories(colony_core
  PUBLIC  ${CMAKE_SOURCE_DIR}/include          # your public headers
  PRIVATE ${CMAKE_SOURCE_DIR}/pathfinding
          ${CMAKE_SOURCE_DIR}/hpa
)
