cmake_minimum_required(VERSION 3.20)
project(qa_native_profile LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

find_package(DynamoRIO CONFIG REQUIRED
    PATHS "${QA_DYNAMORIO_SDK}/cmake" NO_DEFAULT_PATH)
set(DynamoRIO_RPATH OFF)
add_library(qa-native-profile SHARED
    "${QA_SOURCE_ROOT}/src/compat/native/guest/profile/client.c"
    "${QA_SOURCE_ROOT}/src/compat/native/guest/profile/instruction.c")
target_include_directories(qa-native-profile PRIVATE "${QA_SOURCE_ROOT}/include")
target_compile_definitions(qa-native-profile PRIVATE QA_NATIVE_PROFILE_DYNAMORIO_CLIENT)
configure_DynamoRIO_client(qa-native-profile)
use_DynamoRIO_extension(qa-native-profile drmgr)
use_DynamoRIO_extension(qa-native-profile drutil)
set(qa_profile_clients qa-native-profile)
if(QA_DECLARED_REGIONS)
    add_library(qa-native-hooks SHARED
        "${QA_SOURCE_ROOT}/src/compat/native/instrument_client.c")
    target_include_directories(qa-native-hooks PRIVATE "${QA_SOURCE_ROOT}/include")
    target_compile_definitions(qa-native-hooks PRIVATE QA_NATIVE_DYNAMORIO_CLIENT)
    configure_DynamoRIO_client(qa-native-hooks)
    use_DynamoRIO_extension(qa-native-hooks drmgr)
    use_DynamoRIO_extension(qa-native-hooks drutil)
    list(APPEND qa_profile_clients qa-native-hooks)
endif()
foreach(qa_profile_client IN LISTS qa_profile_clients)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${qa_profile_client} PRIVATE
            -march=x86-64 -mtune=generic -mgeneral-regs-only)
    endif()
    set_target_properties(${qa_profile_client} PROPERTIES
        PREFIX ""
        SKIP_BUILD_RPATH FALSE
        BUILD_RPATH "${QA_DYNAMORIO_SDK}/lib64/release;${QA_DYNAMORIO_SDK}/ext/lib64/release"
        INSTALL_RPATH_USE_LINK_PATH FALSE
        INSTALL_RPATH "$ORIGIN/dynamorio/lib64/release;$ORIGIN/dynamorio/ext/lib64/release")
    install(TARGETS ${qa_profile_client} LIBRARY DESTINATION .)
endforeach()
