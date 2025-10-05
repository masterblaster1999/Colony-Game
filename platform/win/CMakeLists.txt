# --- Windows Platform module ---
add_library(win_platform STATIC
    platform/win/WinApp.cpp
    platform/win/CrashHandler.cpp
    platform/win/FilesystemWin.cpp
)
target_include_directories(win_platform PUBLIC platform/win)

# Link Win32 system libs used by the module
if (MSVC)
  target_link_libraries(win_platform PUBLIC
      Shcore      # DPI awareness (SetProcessDpiAwareness etc.)
      Shell32     # Drag&Drop (DragAcceptFiles)
      Dbghelp     # MiniDumpWriteDump
  )
  target_compile_definitions(win_platform PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_features(win_platform PUBLIC cxx_std_20)
endif()

# Your game target should link against win_platform
# target_link_libraries(ColonyGame PRIVATE win_platform)
