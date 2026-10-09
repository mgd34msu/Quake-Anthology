include(GNUInstallDirs)

set(QA_NATIVE_BUNDLE_ROOT "" CACHE PATH "Staged owned native runtime for this host")
if(QA_NATIVE_BUNDLE_ROOT)
    get_filename_component(QA_NATIVE_BUNDLE_ROOT "${QA_NATIVE_BUNDLE_ROOT}" ABSOLUTE)
    if(NOT IS_DIRECTORY "${QA_NATIVE_BUNDLE_ROOT}")
        message(FATAL_ERROR "QA_NATIVE_BUNDLE_ROOT must name the actual staged runtime directory")
    endif()
    install(DIRECTORY "${QA_NATIVE_BUNDLE_ROOT}/"
        DESTINATION "${QA_NATIVE_RUNTIME_INSTALL_ROOT}" USE_SOURCE_PERMISSIONS)
endif()
