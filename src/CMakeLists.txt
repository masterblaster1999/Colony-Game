set(SRC_CORE
  core/app.cpp core/app.h
  # ... add your actual sources here
)

set(SRC_WIN32
  platform/win/WinLauncher.cpp
  platform/win/win32_main.cpp
  # split big platform file(s) here if needed (see section 5)
)

add_executable(ColonyGame
  ${SRC_CORE}
  ${SRC_WIN32}
)

# Win32/Console subsystem toggle for the main EXE
if(WIN32 AND NOT SHOW_CONSOLE)
  set_target_properties(ColonyGame PROPERTIES WIN32_EXECUTABLE YES)
endif()

target_link_libraries(ColonyGame
  d3d11 dxgi d3dcompiler
  $<$<BOOL:${ENABLE_IMGUI}>:imgui>
  $<$<BOOL:${ENABLE_TRACY}>:tracy_client>
  $<$<STREQUAL:${FRONTEND},sdl>:SDL2::SDL2 SDL2::SDL2main>
)

target_compile_definitions(ColonyGame PRIVATE
  $<$<BOOL:${ENABLE_IMGUI}>:CG_ENABLE_IMGUI>
  $<$<BOOL:${ENABLE_TRACY}>:TRACY_ENABLE>
)

target_include_directories(ColonyGame PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

