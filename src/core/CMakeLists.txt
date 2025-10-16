# src/core/CMakeLists.txt

# Real target with a unique, internal name:
add_library(colony_core_lib STATIC
    # list your core sources here ...
)

# Namespaced ALIAS for every place that consumes it:
add_library(colony::core ALIAS colony_core_lib)

# Keep the produced file name as colony_core(.lib)
set_target_properties(colony_core_lib PROPERTIES
    OUTPUT_NAME colony_core     # artifact stays Colony’s original name
    EXPORT_NAME core            # name consumers will see when imported
)

target_compile_features(colony_core_lib PUBLIC cxx_std_23)

# Includes/defines/compile options as before, e.g.:
# target_include_directories(colony_core_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# target_compile_options(colony_core_lib PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
