include(FetchContent)

function(qa_native_guest_replace_source target original replacement)
    get_target_property(source_root ${target} SOURCE_DIR)
    get_target_property(sources ${target} SOURCES)
    set(replaced 0)
    set(updated "")
    foreach(source IN LISTS sources)
        get_filename_component(absolute "${source}" ABSOLUTE BASE_DIR "${source_root}")
        if(absolute STREQUAL original)
            list(APPEND updated "${replacement}")
            math(EXPR replaced "${replaced} + 1")
        else()
            list(APPEND updated "${source}")
        endif()
    endforeach()
    if(NOT replaced EQUAL 1)
        message(FATAL_ERROR "Pinned native guest source is absent or duplicated: ${original}")
    endif()
    set_property(TARGET ${target} PROPERTY SOURCES "${updated}")
endfunction()

function(qa_native_guest_replace_text variable before after expected)
    set(source "${${variable}}")
    string(REPLACE "${before}" "" removed "${source}")
    string(LENGTH "${source}" source_length)
    string(LENGTH "${removed}" removed_length)
    string(LENGTH "${before}" match_length)
    math(EXPR matches "(${source_length} - ${removed_length}) / ${match_length}")
    if(NOT matches EQUAL expected)
        message(FATAL_ERROR "Pinned native guest extension has a different source boundary")
    endif()
    string(REPLACE "${before}" "${after}" updated "${source}")
    set(${variable} "${updated}" PARENT_SCOPE)
endfunction()

function(qa_native_guest_dependency)
    get_filename_component(guest_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/compat/native/guest" ABSOLUTE)
    get_filename_component(project_headers "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../include" ABSOLUTE)
    set(BUILD_SHARED_LIBS OFF)
    set(CMAKE_C_EXTENSIONS ON)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(UNICORN_LEGACY_STATIC_ARCHIVE OFF)
    set(UNICORN_FUZZ OFF)
    set(UNICORN_LOGGING OFF)
    set(UNICORN_BUILD_TESTS OFF)
    set(UNICORN_INSTALL OFF)
    set(UNICORN_TRACER OFF)
    set(UNICORN_ARCH "x86" CACHE STRING "Native guest dependency architectures" FORCE)
    FetchContent_Declare(qa_unicorn
        GIT_REPOSITORY https://github.com/unicorn-engine/unicorn.git
        GIT_TAG 8028ec436f2d9376525352dd38ed9ed6b9f6be10
        GIT_PROGRESS FALSE)
    FetchContent_MakeAvailable(qa_unicorn)

    if(NOT TARGET unicorn OR NOT TARGET x86_64-softmmu)
        message(FATAL_ERROR "Native guests require the pinned Unicorn x86 dependency")
    endif()
    get_target_property(unicorn_type unicorn TYPE)
    if(NOT unicorn_type STREQUAL "STATIC_LIBRARY")
        message(FATAL_ERROR "Native guest dependency must retain its static private extensions")
    endif()

    set(extension_root "${CMAKE_CURRENT_BINARY_DIR}/native-guest")
    file(MAKE_DIRECTORY "${extension_root}")
    set(x86_original "${qa_unicorn_SOURCE_DIR}/qemu/target/i386/unicorn.c")
    set(x86_extension "${extension_root}/unicorn-x86.c")
    file(WRITE "${x86_extension}"
        "#include \"${x86_original}\"\n#include \"${guest_root}/unicorn_state.inc.c\"\n")
    qa_native_guest_replace_source(x86_64-softmmu "${x86_original}" "${x86_extension}")

    set(store_original "${qa_unicorn_SOURCE_DIR}/qemu/accel/tcg/cputlb.c")
    set(store_extension "${extension_root}/unicorn-cputlb.c")
    file(READ "${store_original}" stores)
    qa_native_guest_replace_text(stores
        "static inline void\nstore_helper("
        "#include \"${guest_root}/unicorn_store.inc.c\"\n\nstatic inline void\nstore_helper(" 1)
    qa_native_guest_replace_text(stores
        "store_memop(haddr, val, op ^ MO_BSWAP);"
        "qa_unicorn_ram_store(env, paddr, haddr, val, op ^ MO_BSWAP);" 1)
    qa_native_guest_replace_text(stores
        "store_memop(haddr, val, op);"
        "qa_unicorn_ram_store(env, paddr, haddr, val, op);" 2)
    file(WRITE "${store_extension}" "${stores}")
    qa_native_guest_replace_source(x86_64-softmmu "${store_original}" "${store_extension}")
    target_include_directories(x86_64-softmmu PRIVATE "${guest_root}" "${project_headers}")
    target_sources(qa_native PRIVATE
        "${guest_root}/memory.c"
        "${guest_root}/cpu.c"
        "${guest_root}/checkpoint.c"
        "${guest_root}/pe.c")
    target_link_libraries(qa_native PRIVATE unicorn)
    install(FILES
        "${qa_unicorn_SOURCE_DIR}/COPYING"
        "${qa_unicorn_SOURCE_DIR}/COPYING.LGPL2"
        "${qa_unicorn_SOURCE_DIR}/COPYING_GLIB"
        "${qa_unicorn_SOURCE_DIR}/AUTHORS.TXT"
        DESTINATION "${QA_NATIVE_INSTALL_DIR}/unicorn")
endfunction()

qa_native_guest_dependency()
