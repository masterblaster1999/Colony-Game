if(FRONTEND STREQUAL "sdl")
  find_package(SDL2 CONFIG REQUIRED)         # via vcpkg.json
endif()

# ImGui: either local sources or vcpkg (choose one strategy)
if(ENABLE_IMGUI)
  # Example: use bundled imgui in third_party/imgui (if you have it)
  add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_dx11.cpp
    $<$<STREQUAL:${FRONTEND},win32>:third_party/imgui/backends/imgui_impl_win32.cpp>
    $<$<STREQUAL:${FRONTEND},sdl>:third_party/imgui/backends/imgui_impl_sdl2.cpp>
  )
  target_include_directories(imgui PUBLIC third_party/imgui)
endif()

# Tracy
if(ENABLE_TRACY)
  if(TRACY_FETCH)
    include(FetchContent)
    FetchContent_Declare(tracy
      GIT_REPOSITORY https://github.com/wolfpld/tracy.git
      GIT_TAG v0.11
    )
    FetchContent_MakeAvailable(tracy)
    add_library(tracy_client STATIC ${tracy_SOURCE_DIR}/public/TracyClient.cpp)
    target_include_directories(tracy_client PUBLIC ${tracy_SOURCE_DIR}/public)
    target_compile_definitions(tracy_client PUBLIC TRACY_ENABLE)
    if(WIN32)
      target_link_libraries(tracy_client PUBLIC ws2_32)
    endif()
  endif()
endif()
