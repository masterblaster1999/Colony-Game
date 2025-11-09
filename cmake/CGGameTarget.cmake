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

# --------------------------------------------------------------------------
# Link Direct3D libraries conditionally based on selected renderer
# --------------------------------------------------------------------------
if(MSVC)
  if(COLONY_RENDERER STREQUAL "d3d12")
    message(STATUS "Linking Direct3D 12 renderer libraries")
    target_link_libraries(ColonyGame PRIVATE d3d12 dxgi)
    if(TARGET Microsoft::DirectXShaderCompiler)
      target_link_libraries(ColonyGame PRIVATE Microsoft::DirectXShaderCompiler)
    endif()
  else()
    message(STATUS "Linking Direct3D 11 renderer libraries")
    target_link_libraries(ColonyGame PRIVATE d3d11 dxgi d3dcompiler)
  endif()

  set_target_properties(ColonyGame PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

# --------------------------------------------------------------------------
# AddressSanitizer (Debug only)
# --------------------------------------------------------------------------
if(MSVC AND COLONY_ASAN)
  target_compile_options(ColonyGame PRIVATE "$<$<CONFIG:Debug>:/fsanitize=address>")
  target_link_options(ColonyGame   PRIVATE "$<$<CONFIG:Debug>:/fsanitize=address>")
endif()

# --------------------------------------------------------------------------
# HLSL pipeline hookup (prefer cg_compile_hlsl; else fall back to manifest)
# --------------------------------------------------------------------------
# Try tool-agnostic shader compilation first (works in VS and Ninja if provided)
include(${CMAKE_SOURCE_DIR}/cmake/CGShaders.cmake OPTIONAL)

if(COMMAND cg_compile_hlsl)
  # Compile all HLSL in renderer/Shaders
  file(GLOB _all_hlsl CONFIGURE_DEPENDS
       "${CMAKE_SOURCE_DIR}/renderer/Shaders/*.hlsl")

  if(_all_hlsl)
    if(EXISTS "${CMAKE_SOURCE_DIR}/renderer/Shaders/include")
      cg_compile_hlsl(ColonyShaders
        SHADERS      ${_all_hlsl}
        INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/renderer/Shaders/include")
    else()
      cg_compile_hlsl(ColonyShaders
        SHADERS      ${_all_hlsl})
    endif()

    if(COMMAND cg_link_shaders_to_target)
      cg_link_shaders_to_target(ColonyShaders ColonyGame)
    endif()
  endif()

else()
  # Fallback to manifest-driven offline shader build
  include(${CMAKE_SOURCE_DIR}/cmake/ColonyShaders.cmake OPTIONAL)
  if(COMMAND colony_register_shaders AND EXISTS "${CMAKE_SOURCE_DIR}/renderer/Shaders/shaders.json")
    colony_register_shaders(
      TARGET     ColonyGame
      MANIFEST   "${CMAKE_SOURCE_DIR}/renderer/Shaders/shaders.json"
      OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders"
    )
    if(COMMAND colony_install_shaders)
      colony_install_shaders(TARGET ColonyGame DESTINATION bin/shaders)
    endif()
  endif()
endif()
