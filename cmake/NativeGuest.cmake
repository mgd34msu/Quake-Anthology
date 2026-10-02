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

function(qa_native_guest_wrap_source target original extension output)
    file(WRITE "${output}" "#include \"${original}\"\n#include \"${extension}\"\n")
    qa_native_guest_replace_source(${target} "${original}" "${output}")
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

    if(NOT TARGET unicorn OR NOT TARGET unicorn-common OR NOT TARGET x86_64-softmmu)
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

    qa_native_guest_wrap_source(unicorn "${qa_unicorn_SOURCE_DIR}/uc.c"
        "${guest_root}/unicorn_map.inc.c" "${extension_root}/unicorn-core.c")
    qa_native_guest_wrap_source(x86_64-softmmu "${qa_unicorn_SOURCE_DIR}/qemu/exec.c"
        "${guest_root}/unicorn_map_exec.inc.c" "${extension_root}/unicorn-exec.c")
    qa_native_guest_wrap_source(x86_64-softmmu "${qa_unicorn_SOURCE_DIR}/qemu/softmmu/memory.c"
        "${guest_root}/unicorn_map_memory.inc.c" "${extension_root}/unicorn-memory.c")
    qa_native_guest_wrap_source(x86_64-softmmu "${qa_unicorn_SOURCE_DIR}/qemu/target/i386/fpu_helper.c"
        "${guest_root}/unicorn_abi_fp.inc.c" "${extension_root}/unicorn-fpu.c")

    set(store_original "${qa_unicorn_SOURCE_DIR}/qemu/accel/tcg/cputlb.c")
    set(store_extension "${extension_root}/unicorn-cputlb.c")
    file(READ "${store_original}" stores)
    qa_native_guest_replace_text(stores
        "static void tlb_flush_one_mmuidx_locked("
        "#include \"${guest_root}/unicorn_map_tlb.inc.c\"\n\nstatic void tlb_flush_one_mmuidx_locked(" 1)
    qa_native_guest_replace_text(stores
        "tlb_mmu_resize_locked(env->uc, desc, fast, now);"
        "qa_unicorn_memory_tlb_resize(env, desc, fast, now);" 1)
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

    set(flush_original "${qa_unicorn_SOURCE_DIR}/qemu/accel/tcg/translate-all.c")
    set(flush_extension "${extension_root}/unicorn-translate-all.c")
    file(READ "${flush_original}" flush)
    qa_native_guest_replace_text(flush
        "static void do_tb_flush("
        "#include \"${guest_root}/unicorn_map_flush.inc.c\"\n\nstatic void do_tb_flush(" 1)
    qa_native_guest_replace_text(flush
        "qht_reset_size(cpu->uc, &cpu->uc->tcg_ctx->tb_ctx.htable, CODE_GEN_HTABLE_SIZE);"
        "qa_unicorn_memory_tb_reset(cpu);" 1)
    file(WRITE "${flush_extension}" "${flush}")
    qa_native_guest_replace_source(x86_64-softmmu "${flush_original}" "${flush_extension}")

    set(hash_original "${qa_unicorn_SOURCE_DIR}/glib_compat/glib_compat.c")
    set(hash_extension "${extension_root}/unicorn-glib.c")
    file(READ "${hash_original}" hashes)
    qa_native_guest_replace_text(hashes
        "void g_hash_table_destroy (GHashTable *hash_table)"
        "#include \"${guest_root}/unicorn_map_hash.inc.c\"\n\nvoid g_hash_table_destroy (GHashTable *hash_table)" 1)
    qa_native_guest_replace_text(hashes
        "    g_hash_table_remove_all (hash_table);\n    g_hash_table_unref (hash_table);"
        "    if (qa_unicorn_hash_destroy_single_owner(hash_table)) return;\n    g_hash_table_remove_all (hash_table);\n    g_hash_table_unref (hash_table);" 1)
    file(WRITE "${hash_extension}" "${hashes}")
    qa_native_guest_replace_source(unicorn-common "${hash_original}" "${hash_extension}")
    target_include_directories(unicorn PRIVATE "${guest_root}" "${project_headers}")
    target_include_directories(x86_64-softmmu PRIVATE "${guest_root}" "${project_headers}"
        "${qa_unicorn_SOURCE_DIR}/qemu/accel/tcg")
    target_sources(qa_native PRIVATE
        "${guest_root}/memory.c"
        "${guest_root}/file_memory.c"
        "${guest_root}/vm.c"
        "${guest_root}/cpu.c"
        "${guest_root}/abi.c"
        "${guest_root}/host_memory.c"
        "${guest_root}/host_x86_64.c"
        "${guest_root}/host_child.c"
        "${guest_root}/native_cpu_codec.c"
        "${guest_root}/native_backend.c"
        "${guest_root}/runtime_import.c"
        "${guest_root}/runtime_resource.c"
        "${guest_root}/checkpoint.c"
        "${guest_root}/pe.c"
        "${guest_root}/pe_bind.c"
        "${guest_root}/elf.c"
        "${guest_root}/elf_memory.c"
        "${guest_root}/elf_program.c"
        "${guest_root}/elf_loader.c"
        "${guest_root}/elf_unwind.c"
        "${guest_root}/elf_unwind_save.c"
        "${guest_root}/elf_bind.c"
        "${guest_root}/elf_publish.c"
        "${guest_root}/sysv_runtime.c"
        "${guest_root}/sysv_libc.c"
        "${guest_root}/sysv_libc_descriptors.c"
        "${guest_root}/sysv_libc_format.c"
        "${guest_root}/sysv_libc_format_float.c"
        "${guest_root}/sysv_stdio.c"
        "${guest_root}/sysv_cxx.c"
        "${guest_root}/sysv_cxx_data.c"
        "${guest_root}/sysv_cxx_locale.c"
        "${guest_root}/sysv_iostream.c"
        "${guest_root}/sysv_process.c"
        "${guest_root}/sysv_process_save.c"
        "${guest_root}/windows_runtime.c"
        "${guest_root}/windows_process.c"
        "${guest_root}/windows_process_save.c"
        "${guest_root}/windows_kernel.c"
        "${guest_root}/windows_crt.c"
        "${guest_root}/windows_msvc.c"
        "${guest_root}/pe_memory.c")
    target_sources(qa_native PRIVATE
        "${guest_root}/profile/artifact.c"
        "${guest_root}/profile/instruction.c"
        "${guest_root}/profile/guard.c"
        "${guest_root}/profile/cpu.c")
    if(QA_NATIVE_PLATFORM STREQUAL "linux" AND QA_NATIVE_ARCH STREQUAL "x86_64")
        target_sources(qa_native PRIVATE "${guest_root}/host_x86_64.S")
    endif()
    target_link_libraries(qa_native PRIVATE unicorn)
    install(FILES
        "${qa_unicorn_SOURCE_DIR}/COPYING"
        "${qa_unicorn_SOURCE_DIR}/COPYING.LGPL2"
        "${qa_unicorn_SOURCE_DIR}/COPYING_GLIB"
        "${qa_unicorn_SOURCE_DIR}/AUTHORS.TXT"
        DESTINATION "${QA_NATIVE_INSTALL_DIR}/unicorn")
endfunction()

qa_native_guest_dependency()
