# in the CMake for colony_core (or the target that builds jps.cpp)
target_include_directories(colony_core
  PUBLIC  ${CMAKE_SOURCE_DIR}/include
  PRIVATE ${CMAKE_SOURCE_DIR}/pathfinding
          ${CMAKE_SOURCE_DIR}/hpa
)
