# cmake/CGGameTarget.cmake
include_guard(GLOBAL)

# Only act if the game's main target exists (created by src/CMakeLists.txt)
if(NOT TARGET ColonyGame)
  return()
endif()

# Link third-party libs gathered earlier
if(COLONY_THIRDPARTY_LIBS)
  target_link_libraries(ColonyGame PRIVATE ${COLONY_THIRDPARTY_LIBS})
endif()

# Link optional internal libs
foreach(_lib colony_core colony_platform_win pcg Factions colony_build_options)
  if(TARGET ${_lib})
    target_link_libraries(ColonyGame PRIVATE ${_lib})
  endif()
endforeach()

# Add common sources if they exist (safe, no-ops if missing)
foreach(_maybe_src IN ITEMS
  src/app/EntryWinMain.cpp
  src/app/App.h src/app/App.cpp
  src/core/FixedTimestep.h src/core/FixedTimestep.cpp
  src/render/HrCheck.h
  src/render/DeviceD3D11.h src/render/DeviceD3D11.cpp
  src/render/Shaders.h src/render/Shaders.cpp
  src/render/Textures.h src/render/Textures.cpp
  src/terrain/ThermalErosion.h src/terrain/ThermalErosion.cpp)
  if(EXISTS "${CMAKE_SOURCE_DIR}/${_maybe_src}")
    target_sources(ColonyGame PRIVATE "${_maybe_src}")
  endif()
endforeach()

# Terrain & simulation (guarded)
set(_terrain
  src/terrain/Heightfield.hpp
  src/terrain/ErosionCommon.hpp
  src/terrain/DeterministicRNG.hpp
  src/terrain/ErosionCPU.hpp src/terrain/ErosionCPU.cpp
  src/terrain/ErosionGPU.hpp src/terrain/ErosionGPU.cpp
  src/simulation/FixedTimestep.hpp
  src/simulation/Replay.hpp src/simulation/Replay.cpp)
foreach(_tsrc IN LISTS _terrain)
  if(EXISTS "${CMAKE_SOURCE_DIR}/${_tsrc}")
    target_sources(ColonyGame PRIVATE "${_tsrc}")
  endif()
endforeach()

# Sky & weather (optional)
if(EXISTS "${CMAKE_SOURCE_DIR}/src/rendering/SkyWeatherSystem.cpp")
  target_sources(ColonyGame PRIVATE src/rendering/SkyWeatherSystem.cpp)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/include")
  target_include_directories(ColonyGame PRIVATE "${CMAKE_SOURCE_DIR}/include")
endif()

# Windows EXE and frontend defs
if(WIN32 AND NOT SHOW_CONSOLE)
  set_target_properties(ColonyGame PROPERTIES WIN32_EXECUTABLE ON)
endif()
if(FRONTEND STREQUAL "win32")
  target_compile_definitions(ColonyGame PRIVATE CG_FRONTEND_WIN32=1)
elseif(FRONTEND STREQUAL "sdl")
  target_compile_definitions(ColonyGame PRIVATE CG_FRONTEND_SDL=1)
endif()

# Per-target PCH (GUI)
if(COLONY_USE_PCH)
  if(EXISTS "${CMAKE_SOURCE_DIR}/src/common/pch_gui.hpp")
    target_precompile_headers(ColonyGame PRIVATE src/common/pch_gui.hpp)
  elseif(EXISTS "${CMAKE_SOURCE_DIR}/src/pch_gui.hpp")
    target_precompile_headers(ColonyGame PRIVATE src/pch_gui.hpp)
  elseif(EXISTS "${CMAKE_SOURCE_DIR}/src/common/pch.hpp")
    target_precompile_headers(ColonyGame PRIVATE src/common/pch.hpp)
  endif()
endif()

# Unity build (opt-in)
if(COLONY_UNITY_BUILD)
  set_target_properties(ColonyGame PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 16)
endif()

# Generated headers/resources
if(EXISTS "${CMAKE_BINARY_DIR}/generated/build_info.h")
  target_include_directories(ColonyGame PRIVATE "${CMAKE_BINARY_DIR}/generated")
endif()
if(EXISTS "${CMAKE_BINARY_DIR}/generated/Version.rc")
  target_sources(ColonyGame PRIVATE "${CMAKE_BINARY_DIR}/generated/Version.rc")
endif()

# Shader runtime define (kept for hot-reload paths)
target_compile_definitions(ColonyGame PRIVATE SHADER_DIR=L\"res/shaders/\")

# Link D3D bits commonly needed (harmless if unused)
if(MSVC)
  target_link_libraries(ColonyGame PRIVATE d3d11 dxgi d3dcompiler)
  set_target_properties(ColonyGame PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

# AddressSanitizer (Debug only)
if(MSVC AND COLONY_ASAN)
  target_compile_options(ColonyGame PRIVATE "$<$<CONFIG:Debug>:/fsanitize=address>")
  target_link_options(ColonyGame   PRIVATE "$<$<CONFIG:Debug>:/fsanitize=address>")
endif()

# ---- HLSL pipeline hookup (unified) ----
# Adds 'shaders' custom target when using offline compilation; makes ColonyGame depend on it.
cg_setup_hlsl_pipeline(TARGET ColonyGame RENDERER ${COLONY_RENDERER})
