# src/CMakeLists.txt or renderer/CMakeLists.txt
target_sources(ColonyGame PRIVATE
  ${CMAKE_SOURCE_DIR}/renderer/Swapchain.cpp
  ${CMAKE_SOURCE_DIR}/renderer/Swapchain.h
)

target_link_libraries(ColonyGame PRIVATE d3d11 dxgi)
