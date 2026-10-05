include(GNUInstallDirs)
find_package(Threads REQUIRED)
find_package(ICU REQUIRED COMPONENTS uc)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBFFI REQUIRED IMPORTED_TARGET libffi)

add_library(qa_data STATIC
    src/core/common.c
    src/core/binary.c
    src/core/json.c
    src/core/number.c
    src/core/hash.c
    src/core/text.c
    src/persistence/source_values.c
    src/platform/filesystem.c
    src/platform/mapping.c)
target_include_directories(qa_data PUBLIC include)
target_link_libraries(qa_data PUBLIC qa_compile_options Threads::Threads PRIVATE ICU::uc)
if(UNIX)
    target_sources(qa_data PRIVATE src/platform/file.c src/platform/filesystem_posix.c)
    target_link_libraries(qa_data PUBLIC m)
    target_compile_definitions(qa_data PRIVATE _POSIX_C_SOURCE=200809L)
elseif(WIN32)
    target_sources(qa_data PRIVATE src/platform/file_windows.c src/platform/filesystem_windows.c)
    target_link_libraries(qa_data PRIVATE Advapi32)
endif()

add_library(qa_native STATIC
    src/compat/native/checkpoint.c
    src/compat/native/declaration.c
    src/compat/native/direct.c
    src/compat/native/ffi.c
    src/compat/native/image.c
    src/compat/native/instance.c
    src/compat/native/memory.c
    src/compat/native/module.c
    src/compat/native/observe.c
    src/compat/native/profiles.c
    src/compat/native/process.c
    src/compat/native/protocol.c
    src/compat/native/region.c
    src/compat/native/region_scope.c
    src/compat/native/runner_child.c
    src/compat/native/runner_host.c
    src/compat/native/variadic.c)
target_link_libraries(qa_native PUBLIC qa_data PRIVATE PkgConfig::LIBFFI ICU::uc ${CMAKE_DL_LIBS})
if(UNIX)
    target_compile_definitions(qa_native PRIVATE _POSIX_C_SOURCE=200809L)
endif()
add_executable(qa-native-runner src/compat/native/main.c)
set_target_properties(qa-native-runner PROPERTIES ENABLE_EXPORTS ON)
target_link_libraries(qa-native-runner PRIVATE qa_native)

if(WIN32)
    set(QA_NATIVE_PLATFORM windows)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(QA_NATIVE_PLATFORM linux)
else()
    message(FATAL_ERROR "Native compatibility helpers require Linux or Windows")
endif()
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" QA_NATIVE_PROCESSOR)
if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND QA_NATIVE_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(QA_NATIVE_ARCH aarch64)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8 AND QA_NATIVE_PROCESSOR MATCHES "^(x86_64|amd64|x64)$")
    set(QA_NATIVE_ARCH x86_64)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4 AND QA_NATIVE_PROCESSOR MATCHES "^(i[3-6]86|x86|x86_64|amd64|x64)$")
    set(QA_NATIVE_ARCH i386)
else()
    message(FATAL_ERROR "Unsupported native helper architecture or pointer width")
endif()
set(QA_NATIVE_RUNTIME_INSTALL_ROOT "${CMAKE_INSTALL_LIBEXECDIR}/quake-anthology" CACHE STRING
    "Native runtime destination relative to the installation prefix")
set(QA_NATIVE_INSTALL_DIR "${QA_NATIVE_RUNTIME_INSTALL_ROOT}/${QA_NATIVE_PLATFORM}-${QA_NATIVE_ARCH}")
if(QA_NATIVE_PLATFORM STREQUAL "linux" AND QA_NATIVE_ARCH STREQUAL "x86_64")
    enable_language(ASM)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/NativeGuest.cmake")
if(DEFINED QA_NATIVE_EXPECTED_PLATFORM AND NOT QA_NATIVE_PLATFORM STREQUAL QA_NATIVE_EXPECTED_PLATFORM)
    message(FATAL_ERROR "Native helper toolchain produced a different operating system")
endif()
if(DEFINED QA_NATIVE_EXPECTED_ARCH AND NOT QA_NATIVE_ARCH STREQUAL QA_NATIVE_EXPECTED_ARCH)
    message(FATAL_ERROR "Native helper toolchain produced a different architecture")
endif()
install(TARGETS qa-native-runner RUNTIME DESTINATION "${QA_NATIVE_INSTALL_DIR}")

set(QA_NATIVE_LIBFFI_PREFIX "" CACHE PATH "Complete target libffi installation prefix to deliver")
if(QA_NATIVE_LIBFFI_PREFIX)
    get_filename_component(QA_NATIVE_LIBFFI_PREFIX "${QA_NATIVE_LIBFFI_PREFIX}" ABSOLUTE)
    if(NOT IS_DIRECTORY "${QA_NATIVE_LIBFFI_PREFIX}")
        message(FATAL_ERROR "Native libffi delivery requires its actual installation prefix")
    endif()
    install(DIRECTORY "${QA_NATIVE_LIBFFI_PREFIX}/"
        DESTINATION "${QA_NATIVE_INSTALL_DIR}/libffi" USE_SOURCE_PERMISSIONS)
    if(WIN32)
        file(GLOB QA_NATIVE_LIBFFI_DLLS "${QA_NATIVE_LIBFFI_PREFIX}/bin/*.dll")
        if(QA_NATIVE_LIBFFI_DLLS)
            install(FILES ${QA_NATIVE_LIBFFI_DLLS} DESTINATION "${QA_NATIVE_INSTALL_DIR}")
        endif()
    else()
        set(QA_NATIVE_LIBFFI_RPATHS "")
        foreach(QA_NATIVE_LIBFFI_DIRECTORY IN LISTS LIBFFI_LIBRARY_DIRS)
            file(RELATIVE_PATH QA_NATIVE_LIBFFI_RELATIVE "${QA_NATIVE_LIBFFI_PREFIX}" "${QA_NATIVE_LIBFFI_DIRECTORY}")
            if(IS_ABSOLUTE "${QA_NATIVE_LIBFFI_RELATIVE}" OR QA_NATIVE_LIBFFI_RELATIVE MATCHES "^\\.\\.(/|$)")
                message(FATAL_ERROR "Selected libffi library directory leaves its delivered prefix")
            endif()
            list(APPEND QA_NATIVE_LIBFFI_RPATHS "$ORIGIN/libffi/${QA_NATIVE_LIBFFI_RELATIVE}")
        endforeach()
        set_target_properties(qa-native-runner PROPERTIES INSTALL_RPATH "${QA_NATIVE_LIBFFI_RPATHS}")
    endif()
endif()

option(QA_NATIVE_DYNAMORIO "Build declared-region instrumentation for this helper ABI" OFF)
include("${CMAKE_CURRENT_LIST_DIR}/NativeProfile.cmake")
if(QA_NATIVE_DYNAMORIO AND NOT (QA_NATIVE_PLATFORM STREQUAL "linux" AND QA_NATIVE_ARCH STREQUAL "x86_64"))
    find_package(DynamoRIO CONFIG REQUIRED)
    if(UNIX)
        set(DynamoRIO_RPATH ON)
    else()
        # Windows SDK .drpath files contain absolute build paths. Deliver the
        # actual imported extension DLLs beside the client instead.
        set(DynamoRIO_RPATH OFF)
    endif()
    add_library(qa-native-hooks SHARED src/compat/native/instrument_client.c)
    target_compile_definitions(qa-native-hooks PRIVATE QA_NATIVE_DYNAMORIO_CLIENT)
    configure_DynamoRIO_client(qa-native-hooks)
    use_DynamoRIO_extension(qa-native-hooks drmgr)
    use_DynamoRIO_extension(qa-native-hooks drutil)
    set_target_properties(qa-native-hooks PROPERTIES PREFIX "")
    math(EXPR QA_NATIVE_BITS "${CMAKE_SIZEOF_VOID_P} * 8")
    if(UNIX)
        set_target_properties(qa-native-hooks PROPERTIES
            SKIP_BUILD_RPATH FALSE
            INSTALL_RPATH "$ORIGIN/dynamorio/lib${QA_NATIVE_BITS}/release;$ORIGIN/dynamorio/ext/lib${QA_NATIVE_BITS}/release")
    else()
        install(FILES "$<TARGET_FILE:drmgr>" "$<TARGET_FILE:drutil>"
            DESTINATION "${QA_NATIVE_INSTALL_DIR}")
    endif()
    get_filename_component(QA_NATIVE_DYNAMORIO_PREFIX "${DynamoRIO_DIR}/.." ABSOLUTE)
    if(NOT IS_DIRECTORY "${QA_NATIVE_DYNAMORIO_PREFIX}/lib${QA_NATIVE_BITS}" OR
       NOT IS_DIRECTORY "${QA_NATIVE_DYNAMORIO_PREFIX}/ext/lib${QA_NATIVE_BITS}" OR
       NOT EXISTS "${QA_NATIVE_DYNAMORIO_PREFIX}/License.txt")
        message(FATAL_ERROR "Native instrumentation delivery requires the complete installed DynamoRIO SDK")
    endif()
    install(DIRECTORY "${QA_NATIVE_DYNAMORIO_PREFIX}/"
        DESTINATION "${QA_NATIVE_INSTALL_DIR}/dynamorio" USE_SOURCE_PERMISSIONS)
    install(TARGETS qa-native-hooks
        LIBRARY DESTINATION "${QA_NATIVE_INSTALL_DIR}"
        RUNTIME DESTINATION "${QA_NATIVE_INSTALL_DIR}")
endif()
