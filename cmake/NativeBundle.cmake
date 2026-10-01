include(GNUInstallDirs)

set(QA_NATIVE_BUNDLE_ROOT "" CACHE PATH "Staged complete native runtime root, with per-ABI directories")
set(QA_NATIVE_BUNDLE_WINE_PREFIX "" CACHE PATH "Complete installed Wine/WoW64 prefix to deliver unchanged")
option(QA_NATIVE_BUILD_BUNDLE "Build all four original x86 helper ABIs using supplied toolchains and SDKs" OFF)
option(QA_NATIVE_BUNDLE_REQUIRE_COMPLETE "Reject native installation without all four instrumented helper ABIs and Wine/WoW64" OFF)

set(QA_NATIVE_BUNDLE_ABIS windows-i386 windows-x86_64 linux-i386 linux-x86_64)
if(QA_NATIVE_BUILD_BUNDLE AND NOT QA_NATIVE_BUNDLE_ROOT)
    set(QA_NATIVE_BUNDLE_ROOT "${CMAKE_BINARY_DIR}/native-bundle" CACHE PATH
        "Staged complete native runtime root, with per-ABI directories" FORCE)
endif()
if(QA_NATIVE_BUNDLE_ROOT)
    get_filename_component(QA_NATIVE_BUNDLE_ROOT "${QA_NATIVE_BUNDLE_ROOT}" ABSOLUTE)
endif()
if(QA_NATIVE_BUILD_BUNDLE)
    include(ExternalProject)
    foreach(QA_NATIVE_BUNDLE_ABI IN LISTS QA_NATIVE_BUNDLE_ABIS)
        string(REPLACE "-" "_" QA_NATIVE_BUNDLE_KEY "${QA_NATIVE_BUNDLE_ABI}")
        string(TOUPPER "${QA_NATIVE_BUNDLE_KEY}" QA_NATIVE_BUNDLE_KEY)
        set(QA_NATIVE_BUNDLE_INPUT "QA_NATIVE_BUNDLE_${QA_NATIVE_BUNDLE_KEY}")
        set(${QA_NATIVE_BUNDLE_INPUT}_TOOLCHAIN "" CACHE FILEPATH "Actual ${QA_NATIVE_BUNDLE_ABI} CMake toolchain")
        set(${QA_NATIVE_BUNDLE_INPUT}_LIBFFI_PKGCONFIG_DIR "" CACHE PATH "Target libffi pkg-config directory")
        set(${QA_NATIVE_BUNDLE_INPUT}_LIBFFI_PREFIX "" CACHE PATH "Complete target libffi prefix including its original licenses")
        set(${QA_NATIVE_BUNDLE_INPUT}_DYNAMORIO_DIR "" CACHE PATH "Actual target DynamoRIO SDK cmake directory")
        set(${QA_NATIVE_BUNDLE_INPUT}_SYSROOT "" CACHE PATH "Target pkg-config sysroot, if used by this toolchain")
        set(${QA_NATIVE_BUNDLE_INPUT}_GENERATOR "${CMAKE_GENERATOR}" CACHE STRING "Generator supported by this toolchain")
        foreach(QA_NATIVE_BUNDLE_PATH TOOLCHAIN LIBFFI_PKGCONFIG_DIR LIBFFI_PREFIX DYNAMORIO_DIR SYSROOT)
            if(${QA_NATIVE_BUNDLE_INPUT}_${QA_NATIVE_BUNDLE_PATH})
                get_filename_component(${QA_NATIVE_BUNDLE_INPUT}_${QA_NATIVE_BUNDLE_PATH}
                    "${${QA_NATIVE_BUNDLE_INPUT}_${QA_NATIVE_BUNDLE_PATH}}" ABSOLUTE)
            endif()
        endforeach()
        if(NOT EXISTS "${${QA_NATIVE_BUNDLE_INPUT}_TOOLCHAIN}" OR
           IS_DIRECTORY "${${QA_NATIVE_BUNDLE_INPUT}_TOOLCHAIN}")
            message(FATAL_ERROR "${QA_NATIVE_BUNDLE_INPUT}_TOOLCHAIN must name its actual file")
        endif()
        foreach(QA_NATIVE_BUNDLE_DIRECTORY LIBFFI_PKGCONFIG_DIR LIBFFI_PREFIX DYNAMORIO_DIR)
            if(NOT IS_DIRECTORY "${${QA_NATIVE_BUNDLE_INPUT}_${QA_NATIVE_BUNDLE_DIRECTORY}}")
                message(FATAL_ERROR "${QA_NATIVE_BUNDLE_INPUT}_${QA_NATIVE_BUNDLE_DIRECTORY} must name its actual directory")
            endif()
        endforeach()
        if(NOT EXISTS "${${QA_NATIVE_BUNDLE_INPUT}_LIBFFI_PKGCONFIG_DIR}/libffi.pc" OR
           NOT EXISTS "${${QA_NATIVE_BUNDLE_INPUT}_DYNAMORIO_DIR}/DynamoRIOConfig.cmake")
            message(FATAL_ERROR "Native bundle inputs lack the target libffi or DynamoRIO package metadata")
        endif()
        string(REPLACE "-" ";" QA_NATIVE_BUNDLE_PARTS "${QA_NATIVE_BUNDLE_ABI}")
        list(GET QA_NATIVE_BUNDLE_PARTS 0 QA_NATIVE_BUNDLE_PLATFORM)
        list(GET QA_NATIVE_BUNDLE_PARTS 1 QA_NATIVE_BUNDLE_ARCH)
        ExternalProject_Add(qa-native-bundle-${QA_NATIVE_BUNDLE_ABI}
            SOURCE_DIR "${PROJECT_SOURCE_DIR}"
            BINARY_DIR "${CMAKE_BINARY_DIR}/native-build/${QA_NATIVE_BUNDLE_ABI}"
            DOWNLOAD_COMMAND ""
            UPDATE_COMMAND ""
            CONFIGURE_COMMAND "${CMAKE_COMMAND}" -E env
                "PKG_CONFIG_LIBDIR=${${QA_NATIVE_BUNDLE_INPUT}_LIBFFI_PKGCONFIG_DIR}"
                "PKG_CONFIG_PATH=" "PKG_CONFIG_SYSROOT_DIR=${${QA_NATIVE_BUNDLE_INPUT}_SYSROOT}"
                "${CMAKE_COMMAND}" -S <SOURCE_DIR> -B <BINARY_DIR>
                -G "${${QA_NATIVE_BUNDLE_INPUT}_GENERATOR}"
                "-DCMAKE_TOOLCHAIN_FILE=${${QA_NATIVE_BUNDLE_INPUT}_TOOLCHAIN}"
                -DCMAKE_BUILD_TYPE=Release
                -DQA_NATIVE_HELPER_ONLY=ON -DQA_NATIVE_DYNAMORIO=ON
                -DQA_NATIVE_RUNTIME_INSTALL_ROOT=.
                "-DQA_NATIVE_EXPECTED_PLATFORM=${QA_NATIVE_BUNDLE_PLATFORM}"
                "-DQA_NATIVE_EXPECTED_ARCH=${QA_NATIVE_BUNDLE_ARCH}"
                "-DQA_NATIVE_LIBFFI_PREFIX=${${QA_NATIVE_BUNDLE_INPUT}_LIBFFI_PREFIX}"
                "-DDynamoRIO_DIR=${${QA_NATIVE_BUNDLE_INPUT}_DYNAMORIO_DIR}"
                "-DCMAKE_INSTALL_PREFIX=${QA_NATIVE_BUNDLE_ROOT}"
            BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --config Release
                --target qa-native-runner qa-native-hooks
            INSTALL_COMMAND "${CMAKE_COMMAND}" --install <BINARY_DIR> --config Release)
    endforeach()
endif()

if(QA_NATIVE_BUNDLE_REQUIRE_COMPLETE AND NOT QA_NATIVE_BUNDLE_ROOT)
    message(FATAL_ERROR "Complete native delivery requires QA_NATIVE_BUNDLE_ROOT or QA_NATIVE_BUILD_BUNDLE")
endif()
if(QA_NATIVE_BUNDLE_ROOT)
    if(NOT QA_NATIVE_BUILD_BUNDLE AND NOT IS_DIRECTORY "${QA_NATIVE_BUNDLE_ROOT}")
        message(FATAL_ERROR "QA_NATIVE_BUNDLE_ROOT must name the actual staged runtime directory")
    endif()
    if(QA_NATIVE_BUNDLE_WINE_PREFIX)
        get_filename_component(QA_NATIVE_BUNDLE_WINE_PREFIX "${QA_NATIVE_BUNDLE_WINE_PREFIX}" ABSOLUTE)
        if(NOT IS_DIRECTORY "${QA_NATIVE_BUNDLE_WINE_PREFIX}")
            message(FATAL_ERROR "Native Wine delivery requires its actual complete installed prefix")
        endif()
    endif()
    # Installation checks actual deferred build products before copying them.
    # No artifact is downloaded, executed, inferred from a path, or replaced.
    install(CODE "
        if(${QA_NATIVE_BUNDLE_REQUIRE_COMPLETE})
            set(_qa_bundle_root [==[${QA_NATIVE_BUNDLE_ROOT}]==])
            foreach(_qa_abi IN ITEMS windows-i386 windows-x86_64 linux-i386 linux-x86_64)
                if(_qa_abi MATCHES \"i386$\")
                    set(_qa_bits 32)
                else()
                    set(_qa_bits 64)
                endif()
                if(_qa_abi MATCHES \"^windows\")
                    set(_qa_files qa-native-runner.exe qa-native-hooks.dll drmgr.dll drutil.dll
                        dynamorio/bin\${_qa_bits}/drrun.exe
                        dynamorio/lib\${_qa_bits}/release/dynamorio.dll
                        dynamorio/ext/lib\${_qa_bits}/release/drmgr.dll
                        dynamorio/ext/lib\${_qa_bits}/release/drutil.dll)
                else()
                    set(_qa_files qa-native-runner qa-native-hooks.so
                        dynamorio/bin\${_qa_bits}/drrun
                        dynamorio/lib\${_qa_bits}/release/libdynamorio.so
                        dynamorio/ext/lib\${_qa_bits}/release/libdrmgr.so
                        dynamorio/ext/lib\${_qa_bits}/release/libdrutil.so)
                endif()
                list(APPEND _qa_files dynamorio/License.txt)
                foreach(_qa_file IN LISTS _qa_files)
                    if(NOT EXISTS \"\${_qa_bundle_root}/\${_qa_abi}/\${_qa_file}\" OR
                       IS_DIRECTORY \"\${_qa_bundle_root}/\${_qa_abi}/\${_qa_file}\")
                        message(FATAL_ERROR \"Incomplete native delivery: \${_qa_abi}/\${_qa_file}\")
                    endif()
                endforeach()
                foreach(_qa_tree IN ITEMS dynamorio/lib\${_qa_bits} dynamorio/ext/lib\${_qa_bits} libffi)
                    if(NOT IS_DIRECTORY \"\${_qa_bundle_root}/\${_qa_abi}/\${_qa_tree}\")
                        message(FATAL_ERROR \"Incomplete native runtime tree: \${_qa_abi}/\${_qa_tree}\")
                    endif()
                endforeach()
                file(GLOB_RECURSE _qa_ffi_files LIST_DIRECTORIES FALSE
                    \"\${_qa_bundle_root}/\${_qa_abi}/libffi/*\")
                set(_qa_ffi_license FALSE)
                foreach(_qa_file IN LISTS _qa_ffi_files)
                    get_filename_component(_qa_name \"\${_qa_file}\" NAME)
                    string(TOLOWER \"\${_qa_name}\" _qa_name)
                    if(_qa_name MATCHES \"^(license|copying)([.]|$)\")
                        set(_qa_ffi_license TRUE)
                    endif()
                endforeach()
                if(NOT _qa_ffi_license)
                    message(FATAL_ERROR \"Native libffi prefix lacks its supplied original license: \${_qa_abi}\")
                endif()
            endforeach()
            set(_qa_wine [==[${QA_NATIVE_BUNDLE_WINE_PREFIX}]==])
            if(NOT _qa_wine)
                set(_qa_wine \"\${_qa_bundle_root}/wine\")
            endif()
            if(NOT EXISTS \"\${_qa_wine}/bin/wine\" OR IS_DIRECTORY \"\${_qa_wine}/bin/wine\" OR
               NOT EXISTS \"\${_qa_wine}/COPYING.LIB\")
                message(FATAL_ERROR \"Complete native delivery requires the supplied Wine/WoW64 prefix and original license\")
            endif()
            file(GLOB_RECURSE _qa_wine_files LIST_DIRECTORIES FALSE \"\${_qa_wine}/lib*/*\")
            set(_qa_wine32 FALSE)
            set(_qa_wine64 FALSE)
            foreach(_qa_file IN LISTS _qa_wine_files)
                if(_qa_file MATCHES \"/i386-windows/ntdll[.]dll$\")
                    set(_qa_wine32 TRUE)
                elseif(_qa_file MATCHES \"/x86_64-windows/ntdll[.]dll$\")
                    set(_qa_wine64 TRUE)
                endif()
            endforeach()
            if(NOT _qa_wine32 OR NOT _qa_wine64)
                message(FATAL_ERROR \"Wine/WoW64 delivery lacks the actual 32-bit or 64-bit ntdll installation\")
            endif()
        endif()
    ")
    install(DIRECTORY "${QA_NATIVE_BUNDLE_ROOT}/"
        DESTINATION "${QA_NATIVE_RUNTIME_INSTALL_ROOT}" USE_SOURCE_PERMISSIONS)
    if(QA_NATIVE_BUNDLE_WINE_PREFIX)
        install(DIRECTORY "${QA_NATIVE_BUNDLE_WINE_PREFIX}/"
            DESTINATION "${QA_NATIVE_RUNTIME_INSTALL_ROOT}/wine" USE_SOURCE_PERMISSIONS)
    endif()
endif()
