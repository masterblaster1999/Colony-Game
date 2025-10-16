# src/app/CMakeLists.txt

add_executable(colony_game
    # app sources...
)

# Link to the namespaced aliases (not the private *_lib names)
target_link_libraries(colony_game
    PRIVATE colony::core
            colony::renderer
)
