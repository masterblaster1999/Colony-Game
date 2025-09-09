cmake_minimum_required(VERSION 3.16)
project(MarsColonySim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Sources
add_executable(MarsColonyLauncher
    src/Launcher_SDL.cpp
    src/game/Game.cpp
)

target_include_directories(MarsColonyLauncher PRIVATE
    src
)

# Prefer a modern SDL2 package (vcpkg/homebrew/choco/msys2).
find_package(SDL2 CONFIG QUIET)
if (SDL2_FOUND)
    target_link_libraries(MarsColonyLauncher PRIVATE SDL2::SDL2)
    if (TARGET SDL2::SDL2main)
        target_link_libraries(MarsColonyLauncher PRIVATE SDL2::SDL2main)
    endif()
else()
    # Fallback to FindSDL2 (often available on Linux distros)
    find_package(SDL2 REQUIRED)
    target_include_directories(MarsColonyLauncher PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(MarsColonyLauncher PRIVATE ${SDL2_LIBRARIES})
endif()

# Define so we can provide our own main()
target_compile_definitions(MarsColonyLauncher PRIVATE SDL_MAIN_HANDLED)

# Warnings
if (MSVC)
    target_compile_options(MarsColonyLauncher PRIVATE /W4 /permissive-)
else()
    target_compile_options(MarsColonyLauncher PRIVATE -Wall -Wextra -Wpedantic)
endif()
