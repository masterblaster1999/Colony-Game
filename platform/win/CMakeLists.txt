# --- Windows Platform module ---
add_library(win_platform STATIC
    # Your Windows-only platform sources (edit to match your tree)
    platform/win/win_entry.cpp
    platform/win/win_present_gdi.cpp
    platform/win/WinApp.cpp
    platform/win/CrashHandler.cpp
    platform/win/FilesystemWin.cpp
)

# Public include path for the module
target_include_directories(win_platform PUBLIC platform/win)

# Link Win32 system libs used by the module
if (MSVC)
  target_link_libraries(win_platform PUBLIC
      Shcore         # SetProcessDpiAwareness (fallback for DPI awareness)
      Shell32        # Shell helpers, known folders, drag/drop
      Dbghelp        # MiniDumpWriteDump
      Dwmapi         # DwmFlush / DwmIsCompositionEnabled for vsync-friendly present
      Gdi32          # StretchDIBits / GDI present path
      User32         # Windowing, messages, DPI (SetProcessDpiAwarenessContext)
      Xinput9_1_0    # XInput gamepad (lightweight baseline)
      Winmm          # timeBeginPeriod / timeEndPeriod
  )
  target_compile_definitions(win_platform PUBLIC
      UNICODE _UNICODE
      WIN32_LEAN_AND_MEAN
      NOMINMAX
  )
  target_compile_features(win_platform PUBLIC cxx_std_20)
endif()

# Your game target should link against win_platform (edit target name as needed)
# target_link_libraries(ColonyGame PRIVATE win_platform)
