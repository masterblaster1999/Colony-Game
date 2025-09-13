function(enable_win_qol target app_name app_version)
    if (WIN32 AND MSVC)
        target_sources(${target}
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/CrashHandler.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/AppPaths.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/SingleInstance.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/res/version.rc
                ${CMAKE_CURRENT_SOURCE_DIR}/res/app.manifest
        )
        target_include_directories(${target} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32)

        # Reasonable MSVC defaults for games
        target_compile_features(${target} PRIVATE cxx_std_20)
        target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc /utf-8 /Zc:preprocessor /Zc:__cplusplus /MP)
        target_link_libraries(${target} PRIVATE Dbghelp Shell32)
        target_link_options(${target} PRIVATE /guard:cf /dynamicbase /nxcompat /highentropyva)
        target_compile_definitions(${target} PRIVATE
            APP_NAME_W=L"${app_name}" APP_VERSION_W=L"${app_version}")
    endif()
endfunction()
