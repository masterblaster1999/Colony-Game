# src/app/CMakeLists.txt  (append)

# Choose whichever executable target exists.
set(_game_targets)
if(TARGET colony_game)
    list(APPEND _game_targets colony_game)
endif()
if(TARGET ColonyGame)
    list(APPEND _game_targets ColonyGame)
endif()

foreach(tgt IN LISTS _game_targets)
    # Ensure the Windows crash handler implementation is compiled into the EXE.
    target_sources(${tgt} PRIVATE
        ${CMAKE_SOURCE_DIR}/platform/win/CrashHandlerWin.cpp
    )

    # Make the repo root visible to the compiler in case any headers use root-anchored includes.
    target_include_directories(${tgt} PRIVATE ${CMAKE_SOURCE_DIR})

    # Link against DbgHelp (MiniDumpWriteDump) and friends.
    if(MSVC)
        target_link_libraries(${tgt} PRIVATE dbghelp)
    endif()
endforeach()
