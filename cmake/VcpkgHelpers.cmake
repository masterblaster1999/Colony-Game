function(colony_check_vcpkg_result)
    if (DEFINED CACHE{VCPKG_MANIFEST_INSTALL_FAILED})
        message(FATAL_ERROR
            "vcpkg manifest installation failed.\n"
            "See ${CMAKE_BINARY_DIR}/vcpkg-manifest-install.log for details.")
    endif()
endfunction()
