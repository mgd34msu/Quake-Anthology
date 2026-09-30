include(GNUInstallDirs)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBFFI REQUIRED IMPORTED_TARGET libffi)

add_library(qa_data STATIC
    src/core/common.c
    src/core/binary.c
    src/core/json.c
    src/core/number.c
    src/core/hash.c
    src/platform/filesystem.c
    src/platform/mapping.c)
target_include_directories(qa_data PUBLIC include)
target_link_libraries(qa_data PUBLIC qa_compile_options Threads::Threads)
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
    src/compat/native/protocol.c
    src/compat/native/region.c
    src/compat/native/runner_child.c
    src/compat/native/runner_host.c
    src/compat/native/variadic.c)
target_link_libraries(qa_native PUBLIC qa_data PRIVATE PkgConfig::LIBFFI ${CMAKE_DL_LIBS})
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
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(QA_NATIVE_ARCH x86_64)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(QA_NATIVE_ARCH i386)
else()
    message(FATAL_ERROR "Unsupported native helper pointer width")
endif()
set(QA_NATIVE_INSTALL_DIR "${CMAKE_INSTALL_LIBEXECDIR}/quake-anthology/${QA_NATIVE_PLATFORM}-${QA_NATIVE_ARCH}")
install(TARGETS qa-native-runner RUNTIME DESTINATION "${QA_NATIVE_INSTALL_DIR}")

option(QA_NATIVE_DYNAMORIO "Build declared-region instrumentation for this helper ABI" OFF)
if(QA_NATIVE_DYNAMORIO)
    find_package(DynamoRIO CONFIG REQUIRED)
    add_library(qa-native-hooks SHARED src/compat/native/instrument_client.c)
    target_compile_definitions(qa-native-hooks PRIVATE QA_NATIVE_DYNAMORIO_CLIENT)
    configure_DynamoRIO_client(qa-native-hooks)
    use_DynamoRIO_extension(qa-native-hooks drmgr)
    use_DynamoRIO_extension(qa-native-hooks drutil)
    set_target_properties(qa-native-hooks PROPERTIES PREFIX "")
    install(TARGETS qa-native-hooks
        LIBRARY DESTINATION "${QA_NATIVE_INSTALL_DIR}"
        RUNTIME DESTINATION "${QA_NATIVE_INSTALL_DIR}")
endif()
