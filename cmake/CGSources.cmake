# cmake/CGSources.cmake
#
# Central place for "source ownership rules":
# - What goes into colony_core (shared/static lib)
# - What must be compiled only into final EXEs (entrypoints, crash, gpu exports, bootstrap, etc.)
#
# This file is intentionally Windows-aware and preserves the same rules/filters
# that previously lived in src/CMakeLists.txt.

include_guard(GLOBAL)

# ------------------------------ helpers ------------------------------

function(_cg_normalize_paths out_var)
  set(_out "")
  foreach(_f IN LISTS ARGN)
    file(TO_CMAKE_PATH "${_f}" _f_norm)
    list(APPEND _out "${_f_norm}")
  endforeach()
  set(${out_var} "${_out}" PARENT_SCOPE)
endfunction()

function(_cg_pick_first_existing out_var)
  set(_picked "")
  foreach(_cand IN LISTS ARGN)
    if(EXISTS "${_cand}")
      set(_picked "${_cand}")
      break()
    endif()
  endforeach()
  set(${out_var} "${_picked}" PARENT_SCOPE)
endfunction()

function(_cg_add_source_if_exists target file skip_unity)
  if(EXISTS "${file}")
    target_sources(${target} PRIVATE "${file}")
    if(skip_unity)
      set_source_files_properties("${file}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    endif()
  endif()
endfunction()

# ------------------------------ core sources ------------------------------

function(cg_collect_core_sources out_var)
  cmake_parse_arguments(ARG "" "ROOT_DIR;FRONTEND" "" ${ARGN})

  if(ARG_ROOT_DIR)
    set(_root "${ARG_ROOT_DIR}")
  else()
    # default: parent of this file's directory (cmake/.. = repo root)
    get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
  endif()

  set(_frontend "${ARG_FRONTEND}")

  # ------------------------------ Source scan ------------------------------
  file(GLOB_RECURSE SRC_ALL
    CONFIGURE_DEPENDS
    "${_root}/src/*.cpp" "${_root}/src/*.c"
    "${_root}/src/*.hpp" "${_root}/src/*.h"
    "${_root}/platform/win/*.cpp" "${_root}/platform/win/*.h"
  )

  # Normalize paths so regex filters behave consistently on Windows (backslashes vs slashes).
  _cg_normalize_paths(SRC_ALL ${SRC_ALL})

  # Keep app/ and any entry points out of the core lib
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/src/app/.*\\.(c|cc|cxx|cpp)$")
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/src/(launcher|tools)/.*\\.(c|cc|cxx|cpp)$")
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/(win32_main|VerticalSlice|main_win|EntryWinMain|WinMain|LauncherMain)\\.(c|cc|cxx|cpp)$")

  # Exclude vendored imgui sources if present
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/third_party/imgui/.*")

  # Legacy/tool mains not for the core lib
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/src/main\\.cpp$")
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/src/tools/ColonyMapViewerMain\\.cpp$")

  # Frontend gating
  if(_frontend STREQUAL "sdl")
    list(FILTER SRC_ALL EXCLUDE REGEX ".*/platform/win32_main\\.cpp$")
    list(FILTER SRC_ALL EXCLUDE REGEX ".*/launcher/LauncherMain\\.cpp$")
  else()
    list(FILTER SRC_ALL EXCLUDE REGEX ".*/Launcher_SDL\\.cpp$")
    list(FILTER SRC_ALL EXCLUDE REGEX ".*/SDL.*\\.(c|cpp)$")
  endif()

  # ----- Keep all possible Windows entrypoints OUT of the core lib -----
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/platform/win/(WinLauncher|WinMain|VerticalSlice|win32_main|main_win|LauncherMain|AppMain|win_entry)\\.(c|cc|cxx|cpp)$")
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/colonygame\\.(c|cc|cxx|cpp)$")

  # Windows-specific duplicate‑symbol hot spots: keep app/boot code out of the core
  if(WIN32)
    # Prefer AtomicFileWin on Windows; drop cross‑platform AtomicFile.cpp to avoid dupes
    list(FILTER SRC_ALL EXCLUDE REGEX ".*/AtomicFile\\.cpp$")

    # If both case-variants of the D3D11 device exist, keep PascalCase and drop snake_case
    list(FILTER SRC_ALL EXCLUDE REGEX ".*/d3d11_device\\.(c|cc|cxx|cpp)$")

    # If a generic CrashDump.cpp exists alongside CrashDumpWin.cpp, drop the generic TU
    list(FILTER SRC_ALL EXCLUDE REGEX ".*/CrashDump\\.cpp$")

    # IMPORTANT: never compile CrashDumpStub on Windows (prevents LNK4006)
    list(FILTER SRC_ALL EXCLUDE REGEX ".*/platform/win/CrashDumpStub\\.(c|cc|cxx|cpp)$")

    # Exclude real crash handler implementations from the core lib; add to EXE later
    list(FILTER SRC_ALL EXCLUDE REGEX ".*/platform/win/(CrashDumpWin|CrashHandlerWin|WinIntegration|CrashInitBridge)\\.(c|cc|cxx|cpp)$")

    # Keep app/bootstrapy code out of the core; add to EXE(s) instead
    list(FILTER SRC_ALL EXCLUDE REGEX ".*/src/(Bootstrap|GraphicsInit|Game)\\.(c|cc|cxx|cpp)$")

    # Ensure GPU preference exports live only in the final EXE (avoid duplicates)
    list(FILTER SRC_ALL EXCLUDE REGEX ".*/src/platform/win/(GpuPreference|HighPerfGPU)\\.(c|cc|cxx|cpp)$")

    # Keep WinBootstrap out of the core lib; compile only into EXE targets
    list(FILTER SRC_ALL EXCLUDE REGEX ".*/platform/win/WinBootstrap\\.(c|cc|cxx|cpp)$")

    # Also exclude any platform/win Bootstrap variants from the core
    list(FILTER SRC_ALL EXCLUDE REGEX ".*/platform/win/(Bootstrap|WinBootstrap)\\.(c|cc|cxx|cpp)$")
  endif()

  # Defensive de‑duplication (case-insensitive on Windows)
  if(WIN32)
    set(_seen "")
    set(_dedup "")
    foreach(_f IN LISTS SRC_ALL)
      string(TOLOWER "${_f}" _key)
      list(FIND _seen "${_key}" _idx)
      if(_idx EQUAL -1)
        list(APPEND _seen "${_key}")
        list(APPEND _dedup "${_f}")
      endif()
    endforeach()
    set(SRC_ALL "${_dedup}")
    unset(_seen)
    unset(_dedup)
    unset(_key)
    unset(_idx)
  endif()
  list(REMOVE_DUPLICATES SRC_ALL)

  # -------------------- Patch D: DeviceResources ODR guard (Windows) --------------------
  # Ensure exactly one DeviceResources TU is compiled (pre-target, by editing SRC_ALL).
  # Find all DeviceResources candidates; keep one canonical Windows/D3D11 copy.
  set(_dr_candidates)
  foreach(_f IN LISTS SRC_ALL)
    if(_f MATCHES ".*/DeviceResources[^/]*\\.(c|cc|cxx|cpp)$")
      list(APPEND _dr_candidates "${_f}")
    endif()
  endforeach()

  list(LENGTH _dr_candidates _dr_count)
  if(_dr_count GREATER 1)
    # Prefer a platform/win or renderer/*/d3d11 variant when multiple exist.
    set(_dr_keep "")
    foreach(_cand IN LISTS _dr_candidates)
      if(_cand MATCHES ".*/(platform/win|renderer/.*/d3d11|renderer/d3d11)/DeviceResources[^/]*\\.(c|cc|cxx|cpp)$")
        set(_dr_keep "${_cand}")
        break()
      endif()
    endforeach()
    if(NOT _dr_keep)
      list(GET _dr_candidates 0 _dr_keep)
    endif()

    foreach(_cand IN LISTS _dr_candidates)
      if(NOT _cand STREQUAL "${_dr_keep}")
        list(REMOVE_ITEM SRC_ALL "${_cand}")
      endif()
    endforeach()

    message(STATUS "[ODR] Using DeviceResources TU: ${_dr_keep}")
    # Avoid unity-bucket collisions for this TU specifically.
    set_source_files_properties("${_dr_keep}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
  elseif(_dr_count EQUAL 1)
    list(GET _dr_candidates 0 _dr_keep)
    set_source_files_properties("${_dr_keep}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
  endif()
  unset(_dr_candidates)
  unset(_dr_count)
  # -------------------------------------------------------------------------------------

  # ---------------------------- Guardrail: verify filters ----------------------------
  # Fail configure if any app/ or entry-point sources leaked into colony_core.
  set(_colony_disallow_regexes
    ".*/src/app/.*\\.(c|cc|cxx|cpp)$"
    ".*/src/(launcher|tools)/.*\\.(c|cc|cxx|cpp)$"
    ".*/(win32_main|VerticalSlice|main_win|EntryWinMain|WinMain|LauncherMain)\\.(c|cc|cxx|cpp)$"
    ".*/platform/win/(WinLauncher|WinMain|VerticalSlice|win32_main|main_win|LauncherMain|AppMain|win_entry)\\.(c|cc|cxx|cpp)$"
    ".*/platform/win/(CrashDumpWin|CrashHandlerWin|WinIntegration|CrashDumpStub)\\.(c|cc|cxx|cpp)$"
    ".*/colonygame\\.(c|cc|cxx|cpp)$"
    ".*/src/(Bootstrap|GraphicsInit|Game)\\.(c|cc|cxx|cpp)$"
  )

  set(_colony_leaks "")
  foreach(_f IN LISTS SRC_ALL)
    foreach(_rx IN LISTS _colony_disallow_regexes)
      if(_f MATCHES "${_rx}")
        list(APPEND _colony_leaks "${_f}")
        break()
      endif()
    endforeach()
  endforeach()
  list(REMOVE_DUPLICATES _colony_leaks)
  if(_colony_leaks)
    list(JOIN _colony_leaks "\n  " _colony_leaks_joined)
    message(FATAL_ERROR
      "Guardrail: The following app/ or entrypoint sources leaked into colony_core:\n  ${_colony_leaks_joined}\n"
      "Update the filters in cmake/CGSources.cmake to exclude them."
    )
  endif()
  unset(_colony_disallow_regexes)
  unset(_colony_leaks)
  unset(_colony_leaks_joined)
  unset(_rx)
  # -------------------------------------------------------------------------------

  set(${out_var} "${SRC_ALL}" PARENT_SCOPE)
endfunction()

# ------------------------------ EXE ownership ------------------------------

function(cg_add_colonygame_sources target)
  cmake_parse_arguments(ARG "" "ROOT_DIR;FRONTEND" "" ${ARGN})

  if(ARG_ROOT_DIR)
    set(_root "${ARG_ROOT_DIR}")
  else()
    get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
  endif()
  set(_frontend "${ARG_FRONTEND}")

  # --- Windows GUI entry point (single). Prefer EntryWinMain.cpp that calls GameMain() ---
  if(WIN32 AND NOT (_frontend STREQUAL "sdl"))
    set(_ENTRY_ADDED OFF)
    set(_entry_file "")
    foreach(_cand
      "${_root}/src/app/EntryWinMain.cpp"
      "${_root}/src/EntryWinMain.cpp"
      "${_root}/src/platform/win/EntryWinMain.cpp"
      # Fallbacks if repo still has legacy main-in-AppMain.cpp:
      "${_root}/src/AppMain.cpp"
      "${_root}/src/platform/win/AppMain.cpp"
    )
      if(EXISTS "${_cand}")
        target_sources(${target} PRIVATE "${_cand}")
        # Entry / globals should not be combined in unity builds
        set_source_files_properties("${_cand}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
        set(_ENTRY_ADDED ON)
        set(_entry_file "${_cand}")
        break()
      endif()
    endforeach()
    if(NOT _ENTRY_ADDED)
      message(FATAL_ERROR "No Windows entry point found. Provide src/app/EntryWinMain.cpp (that calls GameMain).")
    endif()
    unset(_ENTRY_ADDED)

    # --- Patch 2.1A: ensure GameMain's TU is compiled when using EntryWinMain ---
    set(_HAS_ENTRYWINMAIN OFF)
    if(EXISTS "${_root}/src/app/EntryWinMain.cpp" OR
       EXISTS "${_root}/src/EntryWinMain.cpp" OR
       EXISTS "${_root}/src/platform/win/EntryWinMain.cpp")
      set(_HAS_ENTRYWINMAIN ON)
    endif()
    if(_HAS_ENTRYWINMAIN AND EXISTS "${_root}/src/AppMain.cpp")
      target_sources(${target} PRIVATE "${_root}/src/AppMain.cpp")
      set_source_files_properties("${_root}/src/AppMain.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    endif()
    unset(_HAS_ENTRYWINMAIN)
    unset(_entry_file)
  endif()

  # NEW: Add a single Bootstrap TU to ColonyGame (not to colony_core)
  _cg_pick_first_existing(_bootstrap
    "${_root}/platform/win/Bootstrap.cpp"
    "${_root}/src/platform/win/Bootstrap.cpp"
    "${_root}/src/Bootstrap.cpp"
    "${_root}/platform/win/WinBootstrap.cpp"
    "${_root}/src/platform/win/WinBootstrap.cpp"
  )
  if(_bootstrap)
    target_sources(${target} PRIVATE "${_bootstrap}")
    set_source_files_properties("${_bootstrap}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
  else()
    message(STATUS "[Bootstrap] No Bootstrap*.cpp found; skipping.")
  endif()
  unset(_bootstrap)

  # Bring the app/bootstrap code into the game exe (kept out of the core lib)
  foreach(_appsrc IN ITEMS
    "${_root}/src/GraphicsInit.cpp"
    "${_root}/src/Game.cpp"
  )
    if(EXISTS "${_appsrc}")
      target_sources(${target} PRIVATE "${_appsrc}")
    endif()
  endforeach()
  unset(_appsrc)

  # Windows resources & helpers for the game
  if(WIN32)
    if(EXISTS "${CMAKE_BINARY_DIR}/generated/Version.rc")
      target_sources(${target} PRIVATE "${CMAKE_BINARY_DIR}/generated/Version.rc")
    endif()

    if(EXISTS "${_root}/platform/win/Manifest.rc")
      target_sources(${target} PRIVATE "${_root}/platform/win/Manifest.rc")
    endif()

    # --- Patch 2.1B: choose exactly one GPU preference TU (avoid duplicate exports) ---
    set(_GPU_PREF "")
    if(EXISTS "${_root}/src/platform/win/HighPerfGPU.cpp")
      set(_GPU_PREF "${_root}/src/platform/win/HighPerfGPU.cpp")
    elseif(EXISTS "${_root}/src/platform/win/GpuPreference.cpp")
      set(_GPU_PREF "${_root}/src/platform/win/GpuPreference.cpp")
    endif()
    if(_GPU_PREF)
      target_sources(${target} PRIVATE "${_GPU_PREF}")
      set_source_files_properties("${_GPU_PREF}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    endif()
    unset(_GPU_PREF)

    # Ensure CrashInitBridge.cpp is compiled into the EXE (resolves wincrash::InitCrashHandler)
    if(EXISTS "${_root}/src/platform/win/CrashInitBridge.cpp")
      target_sources(${target} PRIVATE "${_root}/src/platform/win/CrashInitBridge.cpp")
      set_source_files_properties("${_root}/src/platform/win/CrashInitBridge.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    endif()

    # Add crash integration to the EXE (kept out of the core lib)
    # Prefer src/platform/win/* over platform/win/* to avoid duplicate symbols if both exist.
    _cg_pick_first_existing(_crash_handler
      "${_root}/src/platform/win/CrashHandlerWin.cpp"
      "${_root}/platform/win/CrashHandlerWin.cpp"
    )
    if(_crash_handler)
      target_sources(${target} PRIVATE "${_crash_handler}")
      set_source_files_properties("${_crash_handler}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    endif()
    unset(_crash_handler)

    _cg_pick_first_existing(_crash_dump
      "${_root}/src/platform/win/CrashDumpWin.cpp"
      "${_root}/platform/win/CrashDumpWin.cpp"
    )
    if(_crash_dump)
      target_sources(${target} PRIVATE "${_crash_dump}")
      set_source_files_properties("${_crash_dump}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    endif()
    unset(_crash_dump)
  endif()
endfunction()

function(cg_add_colonylauncher_sources target)
  cmake_parse_arguments(ARG "" "ROOT_DIR;FRONTEND" "" ${ARGN})

  if(ARG_ROOT_DIR)
    set(_root "${ARG_ROOT_DIR}")
  else()
    get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
  endif()
  set(_frontend "${ARG_FRONTEND}")

  if(NOT (WIN32 AND NOT (_frontend STREQUAL "sdl")))
    return()
  endif()

  # Launcher entry point (WinLauncher.cpp); exclude from unity if found
  _cg_pick_first_existing(_launcher_entry
    "${_root}/src/platform/win/WinLauncher.cpp"
    "${_root}/platform/win/WinLauncher.cpp"
    "${_root}/WinLauncher.cpp"
  )
  if(_launcher_entry)
    target_sources(${target} PRIVATE "${_launcher_entry}")
    set_source_files_properties("${_launcher_entry}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
  else()
    message(FATAL_ERROR "WinLauncher.cpp not found – provide a single Windows launcher entry point and add it to ColonyLauncher.")
  endif()
  unset(_launcher_entry)

  # Add WinBootstrap only to the launcher EXE (kept out of core)
  _cg_pick_first_existing(_winbootstrap
    "${_root}/platform/win/WinBootstrap.cpp"
    "${_root}/src/platform/win/WinBootstrap.cpp"
  )
  if(_winbootstrap)
    target_sources(${target} PRIVATE "${_winbootstrap}")
    set_source_files_properties("${_winbootstrap}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
  endif()
  unset(_winbootstrap)

  # If the launcher needs crash integration, add it explicitly (kept out of core)
  _cg_pick_first_existing(_crash_handler
    "${_root}/platform/win/CrashHandlerWin.cpp"
    "${_root}/src/platform/win/CrashHandlerWin.cpp"
  )
  if(_crash_handler)
    target_sources(${target} PRIVATE "${_crash_handler}")
    set_source_files_properties("${_crash_handler}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
  endif()
  unset(_crash_handler)

  _cg_pick_first_existing(_crash_dump
    "${_root}/platform/win/CrashDumpWin.cpp"
    "${_root}/src/platform/win/CrashDumpWin.cpp"
  )
  if(_crash_dump)
    target_sources(${target} PRIVATE "${_crash_dump}")
    set_source_files_properties("${_crash_dump}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
  endif()
  unset(_crash_dump)

  # (Optional) Windows resources for the launcher
  if(EXISTS "${_root}/platform/win/Manifest.rc")
    target_sources(${target} PRIVATE "${_root}/platform/win/Manifest.rc")
  endif()
endfunction()
