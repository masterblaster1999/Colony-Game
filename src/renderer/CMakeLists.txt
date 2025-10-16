# src/renderer/CMakeLists.txt

add_library(colony_renderer_lib STATIC
    # renderer sources...
)

add_library(colony::renderer ALIAS colony_renderer_lib)

set_target_properties(colony_renderer_lib PROPERTIES
    OUTPUT_NAME colony_renderer
    EXPORT_NAME renderer
)

target_link_libraries(colony_renderer_lib
    PUBLIC colony::core   # example if renderer depends on core
)
