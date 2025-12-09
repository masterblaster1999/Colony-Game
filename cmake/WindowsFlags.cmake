# cmake/WindowsFlags.cmake
if (MSVC)
  # Keep Windows headers lean and avoid std::min/max collisions
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)

  # Modern, strict, exception model and conformance
  add_compile_options(/permissive- /Zc:__cplusplus /Zc:inline /Zc:preprocessor /EHsc)

  # Warnings: /W4 is a good default; /WX can be CI-only
  add_compile_options(/W4)

  # Per-config tuning (avoid stringing into CMAKE_CXX_FLAGS)
  add_compile_options(
    $<$<CONFIG:Debug>:/Od /Zi /RTC1>
    $<$<CONFIG:RelWithDebInfo>:/O2 /Zi>
    $<$<CONFIG:Release>:/O2>
  )

  # Multiprocess build: MSVC disables /MP for the PCH-creating TU (warning D9030)
  add_compile_options(/MP)

  # Choose runtime with first-class CMake abstraction (3.15+)
  # Example: keep default DLL runtime. To switch to static /MT add:
  # set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
