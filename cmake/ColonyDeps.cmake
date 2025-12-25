# cmake/ColonyDeps.cmake
# ---------------------
# Defines a single INTERFACE target (colony::deps) that carries third-party
# dependency usage requirements.
#
# Goals:
#   - Make dependency include paths consistent across *all* targets.
#   - Avoid "target A compiles but target B can't find headers" issues.
#   - Keep dependency discovery in one place (vcpkg manifest mode friendly).

include_guard(GLOBAL)

if(TARGET colony::deps)
  return()
endif()

if(NOT WIN32)
  message(FATAL_ERROR "Colony-Game is currently Windows-only.")
endif()

add_library(colony_deps INTERFACE)
add_library(colony::deps ALIAS colony_deps)

# Keep /utf-8 consistent even when a target forgets to opt into it.
# (Redundant with cmake/WindowsFlags.cmake, but safe and self-contained.)
target_compile_options(colony_deps INTERFACE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

# --------------------------------------------------------------------------------------
# Required deps (vcpkg manifest)
# --------------------------------------------------------------------------------------

find_package(fmt CONFIG REQUIRED)
target_link_libraries(colony_deps INTERFACE fmt::fmt)

find_package(spdlog CONFIG REQUIRED)
if(TARGET spdlog::spdlog_header_only)
  target_link_libraries(colony_deps INTERFACE spdlog::spdlog_header_only)
elseif(TARGET spdlog::spdlog)
  target_link_libraries(colony_deps INTERFACE spdlog::spdlog)
endif()

# EnTT has had multiple exported target spellings depending on packager.
set(_cg_entt_target "")
find_package(EnTT CONFIG QUIET)
if(EnTT_FOUND)
  if(TARGET EnTT::EnTT)
    set(_cg_entt_target EnTT::EnTT)
  elseif(TARGET EnTT::entt)
    set(_cg_entt_target EnTT::entt)
  endif()
else()
  find_package(entt CONFIG QUIET)
  if(entt_FOUND AND TARGET entt::entt)
    set(_cg_entt_target entt::entt)
  endif()
endif()

if(NOT _cg_entt_target)
  message(FATAL_ERROR "EnTT dependency not found (vcpkg: port 'entt').")
endif()
target_link_libraries(colony_deps INTERFACE ${_cg_entt_target})
unset(_cg_entt_target)

find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(colony_deps INTERFACE nlohmann_json::nlohmann_json)

# --------------------------------------------------------------------------------------
# Optional deps (only linked when available / enabled)
# --------------------------------------------------------------------------------------

find_package(Taskflow CONFIG QUIET)
if(TARGET Taskflow::Taskflow)
  target_link_libraries(colony_deps INTERFACE Taskflow::Taskflow)
endif()

# DirectX helper libs used by some modules.
find_package(directx-headers CONFIG QUIET)
if(TARGET Microsoft::DirectX-Guids)
  target_link_libraries(colony_deps INTERFACE Microsoft::DirectX-Guids)
endif()

find_package(directxtex CONFIG QUIET)
if(TARGET Microsoft::DirectXTex)
  target_link_libraries(colony_deps INTERFACE Microsoft::DirectXTex)
endif()

find_package(directx-dxc CONFIG QUIET)
if(TARGET Microsoft::DirectXShaderCompiler)
  target_link_libraries(colony_deps INTERFACE Microsoft::DirectXShaderCompiler)
endif()

if(DEFINED ENABLE_TRACY AND ENABLE_TRACY)
  find_package(Tracy CONFIG QUIET)
  if(TARGET Tracy::TracyClient)
    target_link_libraries(colony_deps INTERFACE Tracy::TracyClient)
    target_compile_definitions(colony_deps INTERFACE TRACY_ENABLE=1)
  endif()
endif()

if(DEFINED ENABLE_IMGUI AND ENABLE_IMGUI)
  find_package(imgui CONFIG QUIET)
  if(TARGET imgui::imgui)
    target_link_libraries(colony_deps INTERFACE imgui::imgui)
    target_compile_definitions(colony_deps INTERFACE COLONY_WITH_IMGUI=1)
  endif()
endif()

if(DEFINED FRONTEND AND FRONTEND STREQUAL "sdl")
  find_package(SDL2 CONFIG QUIET)
  if(TARGET SDL2::SDL2)
    target_link_libraries(colony_deps INTERFACE SDL2::SDL2)
    if(TARGET SDL2::SDL2main)
      target_link_libraries(colony_deps INTERFACE SDL2::SDL2main)
    endif()
    target_compile_definitions(colony_deps INTERFACE COLONY_WITH_SDL2=1 COLONY_WITH_SDL=1 SDL_MAIN_HANDLED=1)
  endif()
endif()
