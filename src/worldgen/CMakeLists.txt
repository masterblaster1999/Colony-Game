cmake_minimum_required(VERSION 3.20)

add_library(ColonyWorldGen STATIC
    Biomes.cpp
    DomainWarp.cpp
    FlowMapGen.cpp
    Hydrology.cpp
    Noise.cpp
    PoissonDisk.cpp
    Random.cpp
    RiversAndLakesGen.cpp
    SettlementConnector.cpp
    StageContext.cpp
    StagesConfig.cpp
    TerrainMesh.cpp
    WorldGen.cpp

    stages/BaseElevation.cpp
    stages/Biome.cpp
    stages/Climate.cpp
    stages/Hydrology.cpp
    stages/Scatter.cpp
)

target_include_directories(ColonyWorldGen
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(ColonyWorldGen PUBLIC cxx_std_23)

# If you want warnings on this target only:
if (MSVC)
    target_compile_options(ColonyWorldGen PRIVATE /W4 /permissive-)
else()
    target_compile_options(ColonyWorldGen PRIVATE -Wall -Wextra -Wpedantic)
endif()
