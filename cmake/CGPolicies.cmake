# cmake/CGPolicies.cmake
include_guard(GLOBAL)

# Honor MSVC runtime selection via CMAKE_MSVC_RUNTIME_LIBRARY
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

# option() honors normal variables
if(POLICY CMP0077)
  cmake_policy(SET CMP0077 NEW)
endif()

# Allow MSVC debug format to be controlled via CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    # ProgramDatabase PDBs in Debug and RelWithDebInfo
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
        "$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>"
    )
endif()


# Prefer config packages (vcpkg-style)
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)

# Dev UX
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "_cmake")
