cmake_minimum_required(VERSION 3.20)
project(quake_anthology VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(QA_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(QA_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

add_library(qa_compile_options INTERFACE)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(qa_compile_options INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wstrict-prototypes
        -ffp-contract=off)
    if(QA_WARNINGS_AS_ERRORS)
        target_compile_options(qa_compile_options INTERFACE -Werror)
    endif()
    if(QA_SANITIZERS)
        target_compile_options(qa_compile_options INTERFACE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(qa_compile_options INTERFACE
            -fsanitize=address,undefined)
    endif()
endif()

find_package(ZLIB REQUIRED)
find_package(PNG REQUIRED)
find_package(JPEG REQUIRED)
find_package(ICU REQUIRED COMPONENTS uc)
find_library(QA_GIF_LIBRARY NAMES gif REQUIRED)

add_library(qa_core STATIC
    src/core/common.c
    src/core/binary.c
    src/core/arena.c
    src/core/json.c
    src/core/strings.c
    src/core/text.c
    src/platform/file.c)
target_include_directories(qa_core PUBLIC include)
target_link_libraries(qa_core PUBLIC qa_compile_options m PRIVATE ICU::uc)
target_compile_definitions(qa_core PRIVATE _POSIX_C_SOURCE=200809L)

add_library(qa_content STATIC
    src/content/archive.c
    src/formats/bsp.c
    src/formats/image/image.c
    src/formats/image/indexed.c
    src/formats/image/raster.c
    src/formats/image/png.c
    src/formats/image/jpeg.c
    src/formats/image/gif.c
    src/formats/image/wad.c
    src/formats/model/model.c
    src/formats/model/alias.c
    src/formats/model/normals.c
    src/formats/model/md3.c
    src/formats/model/md4.c
    src/formats/model/md5.c
    src/formats/model/text.c
    src/formats/model/metadata.c
    src/formats/model/scales.c
    src/formats/model/lod.c
    src/formats/model/transform.c)
target_link_libraries(qa_content PUBLIC qa_core PRIVATE
    ZLIB::ZLIB PNG::PNG JPEG::JPEG ${QA_GIF_LIBRARY})

add_library(qa_world STATIC src/world/actors.c)
target_link_libraries(qa_world PUBLIC qa_core)

add_library(qa_gameplay STATIC src/gameplay/operation.c)
target_link_libraries(qa_gameplay PUBLIC qa_world)

add_executable(quake-anthology src/main.c)
target_link_libraries(quake-anthology PRIVATE qa_content)
target_compile_definitions(quake-anthology PRIVATE QA_VERSION="${PROJECT_VERSION}")

include(CTest)
if(BUILD_TESTING)
    add_executable(core_test tests/core_test.c)
    target_link_libraries(core_test PRIVATE qa_core)
    add_test(NAME core COMMAND core_test)

    add_executable(archive_test tests/archive_test.c)
    target_link_libraries(archive_test PRIVATE qa_content ZLIB::ZLIB)
    add_test(NAME archive COMMAND archive_test)

    add_executable(bsp_test tests/bsp_test.c)
    target_link_libraries(bsp_test PRIVATE qa_content)
    add_test(NAME bsp COMMAND bsp_test)

    add_executable(image_test tests/image_test.c)
    target_link_libraries(image_test PRIVATE qa_content)
    add_test(NAME image COMMAND image_test)

    add_executable(model_test tests/model_test.c)
    target_link_libraries(model_test PRIVATE qa_content)
    add_test(NAME model COMMAND model_test)
endif()

install(TARGETS quake-anthology RUNTIME DESTINATION bin)
