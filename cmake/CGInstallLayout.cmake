# cmake/CGInstallLayout.cmake
# Defines install directory names ("path install names") for a Windows game-friendly layout.
# Uses GNUInstallDirs because CMake doesn't define CMAKE_INSTALL_*DIR by default. :contentReference[oaicite:3]{index=3}

include_guard(GLOBAL)

include(GNUInstallDirs)

option(COLONY_INSTALL_PORTABLE_LAYOUT
  "Install a portable layout: EXE + DLLs at prefix root, assets next to it (recommended for Windows games)"
  ON
)

# Defaults (only set if user didn't already define them in cache/presets).
if(COLONY_INSTALL_PORTABLE_LAYOUT)
  if(NOT DEFINED COLONY_INSTALL_BINDIR)
    set(COLONY_INSTALL_BINDIR "." CACHE PATH "Install directory for executables and runtime DLLs")
  endif()
  if(NOT DEFINED COLONY_INSTALL_SHADERDIR)
    set(COLONY_INSTALL_SHADERDIR "shaders" CACHE PATH "Install directory for compiled shaders")
  endif()
  if(NOT DEFINED COLONY_INSTALL_ASSETSDIR)
    set(COLONY_INSTALL_ASSETSDIR "assets" CACHE PATH "Install directory for assets/")
  endif()
  if(NOT DEFINED COLONY_INSTALL_DATADIR)
    set(COLONY_INSTALL_DATADIR "data" CACHE PATH "Install directory for data/")
  endif()
  if(NOT DEFINED COLONY_INSTALL_RESDIR)
    set(COLONY_INSTALL_RESDIR "resources" CACHE PATH "Install directory for resources/")
  endif()
else()
  # More "standard" dev install layout
  if(NOT DEFINED COLONY_INSTALL_BINDIR)
    set(COLONY_INSTALL_BINDIR "${CMAKE_INSTALL_BINDIR}" CACHE PATH "Install directory for executables and runtime DLLs")
  endif()
  if(NOT DEFINED COLONY_INSTALL_SHADERDIR)
    set(COLONY_INSTALL_SHADERDIR "${CMAKE_INSTALL_DATADIR}/ColonyGame/shaders" CACHE PATH "Install directory for compiled shaders")
  endif()
  if(NOT DEFINED COLONY_INSTALL_ASSETSDIR)
    set(COLONY_INSTALL_ASSETSDIR "${CMAKE_INSTALL_DATADIR}/ColonyGame/assets" CACHE PATH "Install directory for assets/")
  endif()
  if(NOT DEFINED COLONY_INSTALL_DATADIR)
    set(COLONY_INSTALL_DATADIR "${CMAKE_INSTALL_DATADIR}/ColonyGame/data" CACHE PATH "Install directory for data/")
  endif()
  if(NOT DEFINED COLONY_INSTALL_RESDIR)
    set(COLONY_INSTALL_RESDIR "${CMAKE_INSTALL_DATADIR}/ColonyGame/resources" CACHE PATH "Install directory for resources/")
  endif()
endif()

if(NOT DEFINED COLONY_INSTALL_LIBDIR)
  set(COLONY_INSTALL_LIBDIR "${CMAKE_INSTALL_LIBDIR}" CACHE PATH "Install directory for libraries (.lib, import libs)")
endif()
if(NOT DEFINED COLONY_INSTALL_INCLUDEDIR)
  set(COLONY_INSTALL_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}" CACHE PATH "Install directory for public headers")
endif()
