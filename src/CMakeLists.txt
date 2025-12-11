# src/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
include(FetchContent)

# -------------------------------- Options --------------------------------
set(FRONTEND "" CACHE STRING "Frontend: '' (Win32 native) or 'sdl'")
if(WIN32)
  option(ENABLE_IMGUI "Enable Dear ImGui UI (Win32 + D3D11 backends)" ON)
  option(ENABLE_TRACY "Enable Tracy instrumentation client" ON)
else()
  option(ENABLE_IMGUI OFF)
  option(ENABLE_TRACY OFF)
endif()
option(SHOW_CONSOLE        "Show console for the Win32 executable(s)" OFF)
option(COLONY_USE_PCH      "Enable precompiled headers" ON)
option(COLONY_UNITY_BUILD  "Enable CMake Unity builds (jumbo)" OFF)

# Gate compute shaders to avoid FXC X3501 until all *_cs.hlsl have CSMain
option(COLONY_ENABLE_COMPUTE_SHADERS "Compile compute shaders (*_cs.hlsl) if they define CSMain" OFF)

# Option B: build a separate launcher EXE that can spawn the game EXE
option(COLONY_BUILD_LAUNCHER "Build native Windows launcher (separate EXE)" ON)

set(COLONY_PCH_HEADER "" CACHE STRING "Path to a shared PCH header (relative to source root)")

if(WIN32)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
endif()

# ------------------------------ Third-party ------------------------------
find_package(taskflow   CONFIG QUIET)
find_package(directxtex CONFIG REQUIRED) # Microsoft::DirectXTex

# ------------------------------ Source scan ------------------------------
file(GLOB_RECURSE SRC_ALL
  CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/src/*.cpp" "${CMAKE_SOURCE_DIR}/src/*.c"
  "${CMAKE_SOURCE_DIR}/src/*.hpp" "${CMAKE_SOURCE_DIR}/src/*.h"
  "${CMAKE_SOURCE_DIR}/platform/win/*.cpp" "${CMAKE_SOURCE_DIR}/platform/win/*.h"
)

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
if(FRONTEND STREQUAL "sdl")
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

  # NEW: Keep WinBootstrap out of the core lib; compile only into EXE targets
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/platform/win/WinBootstrap\\.(c|cc|cxx|cpp)$")

  # NEW: Also exclude any platform/win Bootstrap variants from the core
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/platform/win/(Bootstrap|WinBootstrap)\\.(c|cc|cxx|cpp)$")
endif()

# Defensive de‑duplication
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
    "Update the filters in src/CMakeLists.txt to exclude them."
  )
endif()
# -------------------------------------------------------------------------------

# ------------------------------ Core library -----------------------------
add_library(colony_core STATIC ${SRC_ALL})

# Explicitly control unity (jumbo) builds to avoid accidental ODR/duplicate merges.
# Keep OFF by default for the core; allow opt-in via COLONY_UNITY_BUILD.
if(COLONY_UNITY_BUILD)
  set_target_properties(colony_core PROPERTIES UNITY_BUILD ON)
else()
  set_target_properties(colony_core PROPERTIES UNITY_BUILD OFF)
endif()

if(WIN32)
  target_compile_definitions(colony_core PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

if(CMAKE_VERSION VERSION_LESS 3.21)
  target_compile_features(colony_core PUBLIC cxx_std_20)
else()
  target_compile_features(colony_core PUBLIC cxx_std_23)
endif()

target_include_directories(colony_core
  PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/generated"
)

# NOTE: Keep platform/entry-only sources out of the core.

# PCH
set(_PCH_HEADER "")
if(DEFINED COLONY_PCH_HEADER AND EXISTS "${CMAKE_SOURCE_DIR}/${COLONY_PCH_HEADER}")
  set(_PCH_HEADER "${CMAKE_SOURCE_DIR}/${COLONY_PCH_HEADER}")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/src/pch.h")
  set(_PCH_HEADER "${CMAKE_SOURCE_DIR}/src/pch.h")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/src/pch.hpp")
  set(_PCH_HEADER "${CMAKE_SOURCE_DIR}/src/pch.hpp")
endif()
if(COLONY_USE_PCH AND _PCH_HEADER)
  target_precompile_headers(colony_core PRIVATE "${_PCH_HEADER}")
endif()

# Public feature toggles for consumers
target_compile_definitions(colony_core
  PUBLIC
    $<$<STREQUAL:${FRONTEND},sdl>:COLONY_USE_SDL=1>
    $<$<BOOL:${ENABLE_IMGUI}>:CG_ENABLE_IMGUI=1>
    $<$<BOOL:${ENABLE_TRACY}>:TRACY_ENABLE=1>
)

if(MSVC)
  target_compile_options(colony_core PRIVATE /permissive- /Zc:preprocessor /Zc:__cplusplus /utf-8 /W4 /MP)
  # Treat link warnings as errors to prevent duplicate symbol regressions (LNK4006, etc.)
  target_link_options(colony_core PRIVATE /WX)
endif()

if(WIN32)
  target_link_libraries(colony_core PUBLIC shell32 ole32)
endif()

# ----------------------- Patch C: filter SDL2main when not using SDL -----------------------
set(_TP_LIBS ${COLONY_THIRDPARTY_LIBS})
if(NOT FRONTEND STREQUAL "sdl")
  if(_TP_LIBS)
    # Avoid pulling SDL2main unless we are actually using SDL (prevents SDL_main unresolved).
    list(FILTER _TP_LIBS EXCLUDE REGEX "SDL2main|SDL2::SDL2main")
  endif()
endif()
# Optional aggregate build options + DirectXTex
if(NOT TARGET colony_build_options)
  add_library(colony_build_options INTERFACE)
endif()
target_link_libraries(colony_core PUBLIC ${_TP_LIBS} colony_build_options Microsoft::DirectXTex)

# ------------------------------- ImGui (opt) ------------------------------
if(WIN32 AND ENABLE_IMGUI)
  set(_imgui_ok FALSE)
  if(TARGET imgui::imgui)
    set(_imgui_ok TRUE)
  endif()
  if(NOT _imgui_ok)
    message(STATUS "[ImGui] vcpkg target not found; using FetchContent fallback.")
    FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.90.5)
    FetchContent_MakeAvailable(imgui)
    add_library(imgui_backend STATIC
      ${imgui_SOURCE_DIR}/imgui.cpp
      ${imgui_SOURCE_DIR}/imgui_draw.cpp
      ${imgui_SOURCE_DIR}/imgui_tables.cpp
      ${imgui_SOURCE_DIR}/imgui_widgets.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    )
    target_include_directories(imgui_backend PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_compile_definitions(imgui_backend PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
    target_link_libraries(imgui_backend PUBLIC d3d11 dxgi)
  endif()
  if(TARGET imgui::imgui)
    target_link_libraries(colony_core PRIVATE imgui::imgui)
  elseif(TARGET imgui_backend)
    target_link_libraries(colony_core PRIVATE imgui_backend)
  endif()
endif()

# ------------------------------- Tracy (opt) ------------------------------
if(WIN32 AND ENABLE_TRACY)
  if(NOT TARGET Tracy::TracyClient)
    message(STATUS "[Tracy] packaged target not found; using FetchContent fallback.")
    FetchContent_Declare(tracy GIT_REPOSITORY https://github.com/wolfpld/tracy.git GIT_TAG v0.11.0)
    FetchContent_MakeAvailable(tracy)
    add_library(tracy_client STATIC ${tracy_SOURCE_DIR}/public/TracyClient.cpp)
    target_include_directories(tracy_client PUBLIC ${tracy_SOURCE_DIR}/public ${tracy_SOURCE_DIR})
    if(MSVC)
      target_link_libraries(tracy_client PUBLIC ws2_32 dbghelp)
    endif()
    add_library(Tracy::TracyClient ALIAS tracy_client)
  endif()
  target_link_libraries(colony_core PRIVATE Tracy::TracyClient)
endif()

# ----------------------------- Taskflow fallback --------------------------
if(NOT taskflow_FOUND)
  find_path(TASKFLOW_INCLUDE_DIR taskflow/taskflow.hpp)
  if(NOT TASKFLOW_INCLUDE_DIR)
    message(FATAL_ERROR "Taskflow not found. Install via vcpkg (taskflow) or set TASKFLOW_INCLUDE_DIR.")
  endif()
  target_include_directories(colony_core PUBLIC "${TASKFLOW_INCLUDE_DIR}")
endif()

# =============================== Game EXE ===============================
add_executable(ColonyGame)

if(WIN32)
  target_compile_definitions(ColonyGame PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  if(NOT SHOW_CONSOLE)
    set_target_properties(ColonyGame PROPERTIES WIN32_EXECUTABLE YES)
  endif()
endif()

# Explicitly control unity for the EXE as well (opt-in)
if(COLONY_UNITY_BUILD)
  set_target_properties(ColonyGame PROPERTIES UNITY_BUILD ON UNITY_BUILD_UNIQUE_ID "COLONY")
else()
  set_target_properties(ColonyGame PROPERTIES UNITY_BUILD OFF)
endif()

if(CMAKE_VERSION VERSION_LESS 3.21)
  target_compile_features(ColonyGame PRIVATE cxx_std_20)
else()
  target_compile_features(ColonyGame PRIVATE cxx_std_23)
endif()

target_include_directories(ColonyGame PRIVATE
  "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include" "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/generated"
)

if(MSVC)
  target_compile_options(ColonyGame PRIVATE /permissive- /Zc:preprocessor /Zc:__cplusplus /utf-8 /W4 /MP)
  target_link_options(ColonyGame PRIVATE /WX)
endif()

target_link_libraries(ColonyGame PRIVATE colony_core colony_build_options)

# --- Patch A: link single-owner Windows platform libs only at the EXE ---
if(TARGET Colony::PlatformWin)
  target_link_libraries(ColonyGame PRIVATE Colony::PlatformWin)
endif()
if(TARGET Colony::WinPlatform)
  target_link_libraries(ColonyGame PRIVATE Colony::WinPlatform)
endif()

# --- Windows GUI entry point (single). Prefer EntryWinMain.cpp that calls GameMain() ---
if(WIN32 AND NOT (FRONTEND STREQUAL "sdl"))
  set(_ENTRY_ADDED OFF)
  foreach(_cand
    "${CMAKE_SOURCE_DIR}/src/app/EntryWinMain.cpp"
    "${CMAKE_SOURCE_DIR}/src/EntryWinMain.cpp"
    "${CMAKE_SOURCE_DIR}/src/platform/win/EntryWinMain.cpp"
    # Fallbacks if repo still has legacy main-in-AppMain.cpp:
    "${CMAKE_SOURCE_DIR}/src/AppMain.cpp"
    "${CMAKE_SOURCE_DIR}/src/platform/win/AppMain.cpp"
  )
    if(EXISTS "${_cand}")
      target_sources(ColonyGame PRIVATE "${_cand}")
      # Entry / globals should not be combined in unity builds
      set_source_files_properties("${_cand}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
      set(_ENTRY_ADDED ON)
      break()
    endif()
  endforeach()
  if(NOT _ENTRY_ADDED)
    message(FATAL_ERROR "No Windows entry point found. Provide src/app/EntryWinMain.cpp (that calls GameMain).")
  endif()
endif()

# --- Patch 2.1A: ensure GameMain's TU is compiled when using EntryWinMain ---
# If an EntryWinMain exists (any of common paths), also compile AppMain.cpp as a normal TU.
set(_HAS_ENTRYWINMAIN OFF)
if(EXISTS "${CMAKE_SOURCE_DIR}/src/app/EntryWinMain.cpp" OR
   EXISTS "${CMAKE_SOURCE_DIR}/src/EntryWinMain.cpp" OR
   EXISTS "${CMAKE_SOURCE_DIR}/src/platform/win/EntryWinMain.cpp")
  set(_HAS_ENTRYWINMAIN ON)
endif()
if(_HAS_ENTRYWINMAIN AND EXISTS "${CMAKE_SOURCE_DIR}/src/AppMain.cpp")
  target_sources(ColonyGame PRIVATE "${CMAKE_SOURCE_DIR}/src/AppMain.cpp")
  set_source_files_properties("${CMAKE_SOURCE_DIR}/src/AppMain.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
endif()

# NEW: Add a single Bootstrap TU to ColonyGame (not to colony_core)
set(_BOOTSTRAP_ADDED OFF)
foreach(_bs IN ITEMS
  "${CMAKE_SOURCE_DIR}/platform/win/Bootstrap.cpp"
  "${CMAKE_SOURCE_DIR}/src/platform/win/Bootstrap.cpp"
  "${CMAKE_SOURCE_DIR}/src/Bootstrap.cpp"
  "${CMAKE_SOURCE_DIR}/platform/win/WinBootstrap.cpp"
  "${CMAKE_SOURCE_DIR}/src/platform/win/WinBootstrap.cpp"
)
  if(EXISTS "${_bs}")
    target_sources(ColonyGame PRIVATE "${_bs}")
    # Entry/bootstrap-like files should not be merged into unity buckets
    set_source_files_properties("${_bs}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    set(_BOOTSTRAP_ADDED ON)
    break()
  endif()
endforeach()
if(NOT _BOOTSTRAP_ADDED)
  message(STATUS "[Bootstrap] No Bootstrap*.cpp found; skipping.")
endif()

# Bring the app/bootstrap code into the game exe (kept out of the core lib)
foreach(_appsrc IN ITEMS
  "${CMAKE_SOURCE_DIR}/src/GraphicsInit.cpp"
  "${CMAKE_SOURCE_DIR}/src/Game.cpp"
)
  if(EXISTS "${_appsrc}")
    target_sources(ColonyGame PRIVATE "${_appsrc}")
  endif()
endforeach()

# Windows resources & helpers for the game
if(WIN32)
  if(EXISTS "${CMAKE_BINARY_DIR}/generated/Version.rc")
    target_sources(ColonyGame PRIVATE "${CMAKE_BINARY_DIR}/generated/Version.rc")
  endif()
  if(EXISTS "${CMAKE_SOURCE_DIR}/platform/win/Manifest.rc")
    target_sources(ColonyGame PRIVATE "${CMAKE_SOURCE_DIR}/platform/win/Manifest.rc")
  endif()

  # --- Patch 2.1B: choose exactly one GPU preference TU (avoid duplicate exports) ---
  set(_GPU_PREF "")
  if(EXISTS "${CMAKE_SOURCE_DIR}/src/platform/win/HighPerfGPU.cpp")
    set(_GPU_PREF "${CMAKE_SOURCE_DIR}/src/platform/win/HighPerfGPU.cpp")
  elseif(EXISTS "${CMAKE_SOURCE_DIR}/src/platform/win/GpuPreference.cpp")
    set(_GPU_PREF "${CMAKE_SOURCE_DIR}/src/platform/win/GpuPreference.cpp")
  endif()
  if(_GPU_PREF)
    target_sources(ColonyGame PRIVATE "${_GPU_PREF}")
    set_source_files_properties("${_GPU_PREF}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
  endif()

  # Ensure CrashInitBridge.cpp is compiled into the EXE (resolves wincrash::InitCrashHandler)
  if(EXISTS "${CMAKE_SOURCE_DIR}/src/platform/win/CrashInitBridge.cpp")
    target_sources(ColonyGame PRIVATE "${CMAKE_SOURCE_DIR}/src/platform/win/CrashInitBridge.cpp")
    set_source_files_properties("${CMAKE_SOURCE_DIR}/src/platform/win/CrashInitBridge.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
  endif()

  # Add crash integration to the EXE (kept out of the core lib)
  foreach(_f IN ITEMS
    "${CMAKE_SOURCE_DIR}/src/platform/win/CrashHandlerWin.cpp"
    "${CMAKE_SOURCE_DIR}/platform/win/CrashHandlerWin.cpp"
    "${CMAKE_SOURCE_DIR}/src/platform/win/CrashDumpWin.cpp"
    "${CMAKE_SOURCE_DIR}/platform/win/CrashDumpWin.cpp"
  )
    if(EXISTS "${_f}")
      target_sources(ColonyGame PRIVATE "${_f}")
      set_source_files_properties("${_f}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    endif()
  endforeach()

  # Link DbgHelp for MiniDumpWriteDump (in EXE only)
  target_link_libraries(ColonyGame PRIVATE dbghelp) # MiniDumpWriteDump etc.
endif()

# ============================== Launcher EXE ==============================
if(WIN32 AND COLONY_BUILD_LAUNCHER AND NOT (FRONTEND STREQUAL "sdl"))
  add_executable(ColonyLauncher)
  target_compile_definitions(ColonyLauncher PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  if(NOT SHOW_CONSOLE)
    set_target_properties(ColonyLauncher PROPERTIES WIN32_EXECUTABLE YES)
  endif()
  if(CMAKE_VERSION VERSION_LESS 3.21)
    target_compile_features(ColonyLauncher PRIVATE cxx_std_20)
  else()
    target_compile_features(ColonyLauncher PRIVATE cxx_std_23)
  endif()
  target_include_directories(ColonyLauncher PRIVATE
    "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include" "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/generated"
  )
  if(MSVC)
    target_compile_options(ColonyLauncher PRIVATE /permissive- /Zc:preprocessor /Zc:__cplusplus /utf-8 /W4 /MP)
    target_link_options(ColonyLauncher PRIVATE /WX)
  endif()
  target_link_libraries(ColonyLauncher PRIVATE colony_core colony_build_options shell32 ole32)

  # Link platform libs here too if the launcher uses Win platform utilities
  if(TARGET Colony::PlatformWin)
    target_link_libraries(ColonyLauncher PRIVATE Colony::PlatformWin)
  endif()
  if(TARGET Colony::WinPlatform)
    target_link_libraries(ColonyLauncher PRIVATE Colony::WinPlatform)
  endif()

  # Launcher entry point (WinLauncher.cpp); exclude from unity if found
  set(_LAUNCHER_ENTRY_ADDED OFF)
  foreach(_cand
    "${CMAKE_SOURCE_DIR}/src/platform/win/WinLauncher.cpp"
    "${CMAKE_SOURCE_DIR}/platform/win/WinLauncher.cpp"
  )
    if(EXISTS "${_cand}")
      target_sources(ColonyLauncher PRIVATE "${_cand}")
      set_source_files_properties("${_cand}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
      set(_LAUNCHER_ENTRY_ADDED ON)
      break()
    endif()
  endforeach()
  if(NOT _LAUNCHER_ENTRY_ADDED)
    message(FATAL_ERROR "WinLauncher.cpp not found – provide a single Windows launcher entry point and add it to ColonyLauncher.")
  endif()

  # Add WinBootstrap only to the launcher EXE (kept out of core)
  foreach(_wb IN ITEMS
    "${CMAKE_SOURCE_DIR}/platform/win/WinBootstrap.cpp"
    "${CMAKE_SOURCE_DIR}/src/platform/win/WinBootstrap.cpp"
  )
    if(EXISTS "${_wb}")
      target_sources(ColonyLauncher PRIVATE "${_wb}")
      set_source_files_properties("${_wb}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
      break()
    endif()
  endforeach()

  # If the launcher needs crash integration, add it explicitly (kept out of core)
  foreach(_f IN ITEMS
    "${CMAKE_SOURCE_DIR}/platform/win/CrashHandlerWin.cpp"
    "${CMAKE_SOURCE_DIR}/platform/win/CrashDumpWin.cpp"
  )
    if(EXISTS "${_f}")
      target_sources(ColonyLauncher PRIVATE "${_f}")
      set_source_files_properties("${_f}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    endif()
  endforeach()

  # (Optional) Windows resources for the launcher
  if(EXISTS "${CMAKE_SOURCE_DIR}/platform/win/Manifest.rc")
    target_sources(ColonyLauncher PRIVATE "${CMAKE_SOURCE_DIR}/platform/win/Manifest.rc")
  endif()

  # Set startup project to the game by default; switch to the launcher if you prefer
  set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT ColonyGame)
else()
  set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT ColonyGame)
endif()

# Output (multi/single config)
if(CMAKE_CONFIGURATION_TYPES)
  foreach(cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${cfg}" CFGU)
    set_target_properties(ColonyGame     PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${CFGU} "${CMAKE_BINARY_DIR}/bin/${cfg}")
    if(TARGET ColonyLauncher)
      set_target_properties(ColonyLauncher PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${CFGU} "${CMAKE_BINARY_DIR}/bin/${cfg}")
    endif()
  endforeach()
else()
  set_target_properties(ColonyGame     PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  if(TARGET ColonyLauncher)
    set_target_properties(ColonyLauncher PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  endif()
endif()

# Runtime dir variable
set(_RUNTIME_DIR "$<TARGET_FILE_DIR:ColonyGame>")

# ------------------------------ Post-build copy ---------------------------
add_custom_command(TARGET ColonyGame POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${_RUNTIME_DIR}/assets"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${_RUNTIME_DIR}/res"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "${_RUNTIME_DIR}/assets"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/res"    "${_RUNTIME_DIR}/res"
  VERBATIM
)

set(CONTENT_DIR "${CMAKE_SOURCE_DIR}/Content")
if(EXISTS "${CONTENT_DIR}")
  add_custom_command(TARGET ColonyGame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ColonyGame>/Content"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CONTENT_DIR}" "$<TARGET_FILE_DIR:ColonyGame>/Content"
    COMMENT "Copy Content/ next to the EXE for streaming"
    VERBATIM
  )
endif()

# --------------------------------- SHADERS --------------------------------
if(CMAKE_GENERATOR MATCHES "Visual Studio")
  set(HLSL_DIR  "${CMAKE_SOURCE_DIR}/renderer/Shaders")
  set(HLSL_INC_PRIMARY  "${HLSL_DIR}/include")
  set(HLSL_INC_ALT      "${CMAKE_SOURCE_DIR}/shaders/include")
  set(_VS_OUTDIR "$(OutDir)res\\shaders")

  # Vertex/Pixel always — only compile real entrypoint files by suffix
  file(GLOB_RECURSE HLSL_SOURCES CONFIGURE_DEPENDS
    "${HLSL_DIR}/*_vs.hlsl"
    "${HLSL_DIR}/*_ps.hlsl"
  )

  # Compute only when enabled (prevents X3501 if CSMain isn’t present yet)
  file(GLOB_RECURSE HLSL_CS_ALL CONFIGURE_DEPENDS "${HLSL_DIR}/*_cs.hlsl")
  if(COLONY_ENABLE_COMPUTE_SHADERS)
    list(APPEND HLSL_SOURCES ${HLSL_CS_ALL})
  else()
    set(HLSL_LIBRARY ${HLSL_CS_ALL})
  endif()

  file(GLOB_RECURSE HLSL_ALL_HLSL CONFIGURE_DEPENDS "${HLSL_DIR}/*.hlsl")
  file(GLOB_RECURSE HLSL_INCLUDES CONFIGURE_DEPENDS "${HLSL_DIR}/*.hlsli")

  if(HLSL_SOURCES)
    target_sources(ColonyGame PRIVATE ${HLSL_SOURCES})
  endif()
  if(HLSL_LIBRARY)
    target_sources(ColonyGame PRIVATE ${HLSL_LIBRARY})
    set_source_files_properties(${HLSL_LIBRARY} PROPERTIES HEADER_FILE_ONLY ON)
  endif()
  if(HLSL_INCLUDES)
    target_sources(ColonyGame PRIVATE ${HLSL_INCLUDES})
    set_source_files_properties(${HLSL_INCLUDES} PROPERTIES HEADER_FILE_ONLY ON)
  endif()

  source_group("Shaders\\Sources"  FILES ${HLSL_SOURCES})
  source_group("Shaders\\Library"  FILES ${HLSL_LIBRARY})
  source_group("Shaders\\Includes" FILES ${HLSL_INCLUDES})

  # Build a safe include flag string for FXC (avoid semicolons in VS_SHADER_FLAGS).
  set(_HLSL_INC_FLAGS_LIST "")
  if(EXISTS "${HLSL_INC_PRIMARY}")
    list(APPEND _HLSL_INC_FLAGS_LIST "/I\"${HLSL_INC_PRIMARY}\"")
  endif()
  if(EXISTS "${HLSL_INC_ALT}")
    list(APPEND _HLSL_INC_FLAGS_LIST "/I\"${HLSL_INC_ALT}\"")
  endif()
  if(_HLSL_INC_FLAGS_LIST)
    string(JOIN " " _HLSL_INC_FLAGS_STR ${_HLSL_INC_FLAGS_LIST})
  else()
    set(_HLSL_INC_FLAGS_STR "")
  endif()

  # Auto-detect entrypoint per file; fall back to 'main' if *Main is absent
  function(_configure_vs_shader _file)
    get_filename_component(_we "${_file}" NAME_WE)
    set(_type "")
    if(_we MATCHES "_vs$")
      set(_type "Vertex")
    elseif(_we MATCHES "_ps$")
      set(_type "Pixel")
    elseif(_we MATCHES "_cs$")
      set(_type "Compute")
    else()
      return()
    endif()

    # Read the shader file to detect whether VSMain/PSMain/CSMain exist.
    file(READ "${_file}" _hlsl_src)
    set(_entry "main")
    if(_type STREQUAL "Vertex")
      string(FIND "${_hlsl_src}" "VSMain(" _hit)
      if(NOT _hit EQUAL -1)
        set(_entry "VSMain")
      endif()
    elseif(_type STREQUAL "Pixel")
      string(FIND "${_hlsl_src}" "PSMain(" _hit)
      if(NOT _hit EQUAL -1)
        set(_entry "PSMain")
      endif()
    elseif(_type STREQUAL "Compute")
      string(FIND "${_hlsl_src}" "CSMain(" _hit)
      if(NOT _hit EQUAL -1)
        set(_entry "CSMain")
      endif()
    endif()

    set_source_files_properties("${_file}" PROPERTIES
      VS_SHADER_TYPE                  "${_type}"
      VS_SHADER_MODEL                 "5.0"
      VS_SHADER_ENTRYPOINT            "${_entry}"
      VS_SHADER_OBJECT_FILE_NAME      "${_VS_OUTDIR}\\%(Filename).cso"
      VS_SHADER_ENABLE_DEBUG          "$<CONFIG:Debug>"
      VS_SHADER_DISABLE_OPTIMIZATIONS "$<CONFIG:Debug>"
      VS_SHADER_FLAGS                 "${_HLSL_INC_FLAGS_STR}"
    )
  endfunction()

  foreach(_sh IN LISTS HLSL_SOURCES)
    _configure_vs_shader("${_sh}")
  endforeach()

else()
  include(${CMAKE_SOURCE_DIR}/cmake/ColonyShaders.cmake OPTIONAL RESULT_VARIABLE _colony_shaders_included)

  set(_COLONY_MANIFEST "")
  if(EXISTS "${CMAKE_SOURCE_DIR}/renderer/Shaders/shaders.json")
    set(_COLONY_MANIFEST "${CMAKE_SOURCE_DIR}/renderer/Shaders/shaders.json")
  elseif(EXISTS "${CMAKE_SOURCE_DIR}/shaders/shaders.json")
    set(_COLONY_MANIFEST "${CMAKE_SOURCE_DIR}/shaders/shaders.json")
  endif()

  if(_COLONY_MANIFEST AND COMMAND colony_register_shaders)
    set(_COLONY_SHADER_OUT "${CMAKE_BINARY_DIR}/shaders")
    colony_register_shaders(
      TARGET       ColonyGame
      MANIFEST     "${_COLONY_MANIFEST}"
      OUTPUT_DIR   "${_COLONY_SHADER_OUT}"
      INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/renderer/Shaders"
        "${CMAKE_SOURCE_DIR}/renderer/Shaders/include"
        "${CMAKE_SOURCE_DIR}/shaders"
        "${CMAKE_SOURCE_DIR}/shaders/include"
      DXC_ARGS     -nologo
    )

    if(COMMAND colony_install_shaders)
      colony_install_shaders(TARGET ColonyGame DESTINATION bin/shaders)
    endif()

    add_custom_command(TARGET ColonyGame POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ColonyGame>/res/shaders"
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${CMAKE_BINARY_DIR}/shaders/$<CONFIG>"
              "$<TARGET_FILE_DIR:ColonyGame>/res/shaders"
      VERBATIM
    )
  else()
    message(STATUS "No shaders.json manifest found; DXC shader registration skipped.")
  endif()
endif()

# ------------------------------- Install ----------------------------------
install(TARGETS ColonyGame RUNTIME DESTINATION ".")
if(TARGET ColonyLauncher)
  install(TARGETS ColonyLauncher RUNTIME DESTINATION ".")
endif()
install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/"  DESTINATION "assets")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/res/"     DESTINATION "res")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/Content/" DESTINATION "Content")
