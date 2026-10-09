include(GNUInstallDirs)
find_package(Threads REQUIRED)
find_package(ICU REQUIRED COMPONENTS uc)

add_library(qa_data STATIC
    src/core/common.c
    src/core/binary.c
    src/core/json.c
    src/core/number.c
    src/core/hash.c
    src/core/text.c
    src/persistence/source_values.c
    src/platform/services.c
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
    target_link_libraries(qa_data PRIVATE Advapi32 bcrypt)
endif()

add_library(qa_native STATIC
    src/compat/native/checkpoint.c
    src/compat/native/declaration.c
    src/compat/native/ffi.c
    src/compat/native/image.c
    src/compat/native/instance.c
    src/compat/native/memory.c
    src/compat/native/module.c
    src/compat/native/observe.c
    src/compat/native/profiles.c
    src/compat/native/process.c
    src/compat/native/region.c
    src/compat/native/region_scope.c)
target_link_libraries(qa_native PUBLIC qa_data PRIVATE ICU::uc)
if(UNIX)
    target_compile_definitions(qa_native PRIVATE _POSIX_C_SOURCE=200809L)
endif()
add_executable(qa-native-host src/compat/native/main.c)
set_target_properties(qa-native-host PROPERTIES ENABLE_EXPORTS ON)
target_link_libraries(qa-native-host PRIVATE qa_native)

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
install(TARGETS qa-native-host RUNTIME DESTINATION "${QA_NATIVE_INSTALL_DIR}")

include("${CMAKE_CURRENT_LIST_DIR}/NativeProfile.cmake")
