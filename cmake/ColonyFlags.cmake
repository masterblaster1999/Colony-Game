# cmake/ColonyFlags.cmake
if (MSVC)
  # Language & warnings
  add_compile_options(/permissive- /Zc:__cplusplus /Zc:inline /Zc:preprocessor /EHsc)
  add_compile_options(/W4)  # Consider /Wall only for special builds; it's very noisy

  # Build-type specific (note: VS sets reasonable defaults already)
  add_compile_options(
    $<$<CONFIG:Debug>:/Od /Zi /RTC1>
    $<$<CONFIG:RelWithDebInfo>:/O2 /Zi>
    $<$<CONFIG:Release>:/O2>
  )

  # Optional: multi-proc builds (expect D9030 only for the PCH TU)
  add_compile_options(/MP)  # harmless; MSVC ignores it for the PCH creator TU
endif()
