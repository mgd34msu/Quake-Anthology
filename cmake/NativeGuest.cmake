include(FetchContent)

function(qa_native_guest_write_source output contents)
    if(EXISTS "${output}")
        file(READ "${output}" current)
        if("${current}" STREQUAL "${contents}")
            return()
        endif()
    endif()
    file(WRITE "${output}" "${contents}")
endfunction()

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
    qa_native_guest_write_source("${output}" "#include \"${original}\"\n#include \"${extension}\"\n")
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
    qa_native_guest_write_source("${x86_extension}"
        "#include \"${x86_original}\"\n#include \"${guest_root}/unicorn_state.inc.c\"\n")
    qa_native_guest_replace_source(x86_64-softmmu "${x86_original}" "${x86_extension}")

    qa_native_guest_wrap_source(unicorn "${qa_unicorn_SOURCE_DIR}/uc.c"
        "${guest_root}/unicorn_map.inc.c" "${extension_root}/unicorn-core.c")
    qa_native_guest_wrap_source(x86_64-softmmu "${qa_unicorn_SOURCE_DIR}/qemu/exec.c"
        "${guest_root}/unicorn_map_exec.inc.c" "${extension_root}/unicorn-exec.c")
    qa_native_guest_wrap_source(x86_64-softmmu "${qa_unicorn_SOURCE_DIR}/qemu/softmmu/memory.c"
        "${guest_root}/unicorn_map_memory.inc.c" "${extension_root}/unicorn-memory.c")
    set(fpu_original "${qa_unicorn_SOURCE_DIR}/qemu/target/i386/fpu_helper.c")
    set(fpu_extension "${extension_root}/unicorn-fpu.c")
    file(READ "${fpu_original}" fpu)
    qa_native_guest_replace_text(fpu
        "void helper_frndint(CPUX86State *env)\n{\n    ST0 = floatx80_round_to_int(ST0, &env->fp_status);\n}"
        "static void qa_unicorn_frndint(CPUX86State *env, uintptr_t retaddr);\n\nvoid helper_frndint(CPUX86State *env)\n{\n    qa_unicorn_frndint(env, GETPC());\n}" 1)
    qa_native_guest_replace_text(fpu [=[void helper_fpatan(CPUX86State *env)
{
    double fptemp, fpsrcop;

    fpsrcop = floatx80_to_double(env, ST1);
    fptemp = floatx80_to_double(env, ST0);
    ST1 = double_to_floatx80(env, atan2(fpsrcop, fptemp));
    fpop(env);
}]=] [=[static void qa_unicorn_fpatan(CPUX86State *env, uintptr_t retaddr);

void helper_fpatan(CPUX86State *env)
{
    qa_unicorn_fpatan(env, GETPC());
}]=] 1)
    qa_native_guest_replace_text(fpu [=[void helper_fsin(CPUX86State *env)
{
    double fptemp = floatx80_to_double(env, ST0);

    if ((fptemp > MAXTAN) || (fptemp < -MAXTAN)) {
        env->fpus |= 0x400;
    } else {
        ST0 = double_to_floatx80(env, sin(fptemp));
        env->fpus &= ~0x400;  /* C2 <-- 0 */
        /* the above code is for |arg| < 2**53 only */
    }
}]=] [=[static void qa_unicorn_trig(CPUX86State *env, bool cosine, uintptr_t retaddr);

void helper_fsin(CPUX86State *env)
{
    qa_unicorn_trig(env, false, GETPC());
}]=] 1)
    qa_native_guest_replace_text(fpu [=[void helper_fcos(CPUX86State *env)
{
    double fptemp = floatx80_to_double(env, ST0);

    if ((fptemp > MAXTAN) || (fptemp < -MAXTAN)) {
        env->fpus |= 0x400;
    } else {
        ST0 = double_to_floatx80(env, cos(fptemp));
        env->fpus &= ~0x400;  /* C2 <-- 0 */
        /* the above code is for |arg| < 2**63 only */
    }
}]=] [=[void helper_fcos(CPUX86State *env)
{
    qa_unicorn_trig(env, true, GETPC());
}]=] 1)
    qa_native_guest_replace_text(fpu [=[void helper_fptan(CPUX86State *env)
{
    double fptemp = floatx80_to_double(env, ST0);

    if ((fptemp > MAXTAN) || (fptemp < -MAXTAN)) {
        env->fpus |= 0x400;
    } else {
        floatx80 one = { 0x8000000000000000LL, 0x3fff };
        fptemp = tan(fptemp);
        ST0 = double_to_floatx80(env, fptemp);
        fpush(env);
        ST0 = one;
        env->fpus &= ~0x400; /* C2 <-- 0 */
        /* the above code is for |arg| < 2**52 only */
    }
}]=] [=[static void qa_unicorn_trig_push(CPUX86State *env, bool tangent, uintptr_t retaddr);

void helper_fptan(CPUX86State *env)
{
    qa_unicorn_trig_push(env, true, GETPC());
}]=] 1)
    qa_native_guest_replace_text(fpu [=[void helper_fsincos(CPUX86State *env)
{
    double fptemp = floatx80_to_double(env, ST0);

    if ((fptemp > MAXTAN) || (fptemp < -MAXTAN)) {
        env->fpus |= 0x400;
    } else {
        ST0 = double_to_floatx80(env, sin(fptemp));
        fpush(env);
        ST0 = double_to_floatx80(env, cos(fptemp));
        env->fpus &= ~0x400;  /* C2 <-- 0 */
        /* the above code is for |arg| < 2**63 only */
    }
}]=] [=[void helper_fsincos(CPUX86State *env)
{
    qa_unicorn_trig_push(env, false, GETPC());
}]=] 1)
    qa_native_guest_replace_text(fpu [=[void helper_fmul_ST0_FT0(CPUX86State *env)
{
    ST0 = floatx80_mul(ST0, FT0, &env->fp_status);
}]=] [=[static void qa_unicorn_fmul(CPUX86State *env, int destination, int source, bool pop,
    int format, uint64_t bits, uintptr_t retaddr);

void helper_fmul_ST0_FT0(CPUX86State *env, int source)
{
    qa_unicorn_fmul(env, 0, source, false, 0, 0, GETPC());
}]=] 1)
    qa_native_guest_replace_text(fpu [=[void helper_fmul_STN_ST0(CPUX86State *env, int st_index)
{
    ST(st_index) = floatx80_mul(ST(st_index), ST0, &env->fp_status);
}]=] [=[void helper_fmul_STN_ST0(CPUX86State *env, int st_index, int pop)
{
    qa_unicorn_fmul(env, st_index, 0, pop != 0, 0, 0, GETPC());
}]=] 1)
    qa_native_guest_write_source("${fpu_extension}"
        "${fpu}\n#include \"${guest_root}/unicorn_abi_fp.inc.c\"\n")
    qa_native_guest_replace_source(x86_64-softmmu "${fpu_original}" "${fpu_extension}")

    file(READ "${qa_unicorn_SOURCE_DIR}/qemu/target/i386/helper.h" helpers)
    qa_native_guest_replace_text(helpers [=[DEF_HELPER_1(fmul_ST0_FT0, void, env)]=] [=[DEF_HELPER_2(fmul_ST0_FT0, void, env, int)]=] 1)
    qa_native_guest_replace_text(helpers [=[DEF_HELPER_2(fmul_STN_ST0, void, env, int)]=] [=[DEF_HELPER_3(fmul_STN_ST0, void, env, int, int)
DEF_HELPER_3(fmul_memory, void, env, i64, int)]=] 1)
    qa_native_guest_write_source("${extension_root}/helper.h" "${helpers}")
    set(translate_original "${qa_unicorn_SOURCE_DIR}/qemu/target/i386/translate.c")
    set(translate_extension "${extension_root}/unicorn-translate.c")
    file(READ "${translate_original}" translate)
    qa_native_guest_replace_text(translate [=[static void gen_helper_fp_arith_ST0_FT0(TCGContext *tcg_ctx, int op)
{
    switch (op) {
    case 0:
        gen_helper_fadd_ST0_FT0(tcg_ctx, tcg_ctx->cpu_env);
        break;
    case 1:
        gen_helper_fmul_ST0_FT0(tcg_ctx, tcg_ctx->cpu_env);
        break;
    case 2:
        gen_helper_fcom_ST0_FT0(tcg_ctx, tcg_ctx->cpu_env);
        break;
    case 3:
        gen_helper_fcom_ST0_FT0(tcg_ctx, tcg_ctx->cpu_env);
        break;
    case 4:
        gen_helper_fsub_ST0_FT0(tcg_ctx, tcg_ctx->cpu_env);
        break;
    case 5:
        gen_helper_fsubr_ST0_FT0(tcg_ctx, tcg_ctx->cpu_env);
        break;
    case 6:
        gen_helper_fdiv_ST0_FT0(tcg_ctx, tcg_ctx->cpu_env);
        break;
    case 7:
        gen_helper_fdivr_ST0_FT0(tcg_ctx, tcg_ctx->cpu_env);
        break;
    }
}]=] [=[static void gen_helper_fp_arith_ST0_FT0(TCGContext *tcg_ctx, int op, int source)
{
    switch (op) {
    case 0:
        gen_helper_fadd_ST0_FT0(tcg_ctx, tcg_ctx->cpu_env);
        break;
    case 1:
        gen_helper_fmul_ST0_FT0(tcg_ctx, tcg_ctx->cpu_env, tcg_const_i32(tcg_ctx, source));
        break;
    case 2:
        gen_helper_fcom_ST0_FT0(tcg_ctx, tcg_ctx->cpu_env);
        break;
    case 3:
        gen_helper_fcom_ST0_FT0(tcg_ctx, tcg_ctx->cpu_env);
        break;
    case 4:
        gen_helper_fsub_ST0_FT0(tcg_ctx, tcg_ctx->cpu_env);
        break;
    case 5:
        gen_helper_fsubr_ST0_FT0(tcg_ctx, tcg_ctx->cpu_env);
        break;
    case 6:
        gen_helper_fdiv_ST0_FT0(tcg_ctx, tcg_ctx->cpu_env);
        break;
    case 7:
        gen_helper_fdivr_ST0_FT0(tcg_ctx, tcg_ctx->cpu_env);
        break;
    }
}]=] 1)
    qa_native_guest_replace_text(translate [=[static void gen_helper_fp_arith_STN_ST0(TCGContext *tcg_ctx, int op, int opreg)
{
    TCGv_i32 tmp = tcg_const_i32(tcg_ctx, opreg);
    switch (op) {
    case 0:
        gen_helper_fadd_STN_ST0(tcg_ctx, tcg_ctx->cpu_env, tmp);
        break;
    case 1:
        gen_helper_fmul_STN_ST0(tcg_ctx, tcg_ctx->cpu_env, tmp);
        break;
    case 4:
        gen_helper_fsubr_STN_ST0(tcg_ctx, tcg_ctx->cpu_env, tmp);
        break;
    case 5:
        gen_helper_fsub_STN_ST0(tcg_ctx, tcg_ctx->cpu_env, tmp);
        break;
    case 6:
        gen_helper_fdivr_STN_ST0(tcg_ctx, tcg_ctx->cpu_env, tmp);
        break;
    case 7:
        gen_helper_fdiv_STN_ST0(tcg_ctx, tcg_ctx->cpu_env, tmp);
        break;
    }
}]=] [=[static void gen_helper_fp_arith_STN_ST0(TCGContext *tcg_ctx, int op, int opreg, bool pop)
{
    TCGv_i32 tmp = tcg_const_i32(tcg_ctx, opreg);
    switch (op) {
    case 0:
        gen_helper_fadd_STN_ST0(tcg_ctx, tcg_ctx->cpu_env, tmp);
        break;
    case 1:
        gen_helper_fmul_STN_ST0(tcg_ctx, tcg_ctx->cpu_env, tmp, tcg_const_i32(tcg_ctx, pop));
        break;
    case 4:
        gen_helper_fsubr_STN_ST0(tcg_ctx, tcg_ctx->cpu_env, tmp);
        break;
    case 5:
        gen_helper_fsub_STN_ST0(tcg_ctx, tcg_ctx->cpu_env, tmp);
        break;
    case 6:
        gen_helper_fdivr_STN_ST0(tcg_ctx, tcg_ctx->cpu_env, tmp);
        break;
    case 7:
        gen_helper_fdiv_STN_ST0(tcg_ctx, tcg_ctx->cpu_env, tmp);
        break;
    }
}]=] 1)
    qa_native_guest_replace_text(translate [=[                        switch(op >> 4) {
                        case 0:
                            tcg_gen_qemu_ld_i32(tcg_ctx, s->tmp2_i32, s->A0,
                                                s->mem_index, MO_LEUL);
                            gen_helper_flds_FT0(tcg_ctx, tcg_ctx->cpu_env, s->tmp2_i32);
                            break;
                        case 1:
                            tcg_gen_qemu_ld_i32(tcg_ctx, s->tmp2_i32, s->A0,
                                                s->mem_index, MO_LEUL);
                            gen_helper_fildl_FT0(tcg_ctx, tcg_ctx->cpu_env, s->tmp2_i32);
                            break;
                        case 2:
                            tcg_gen_qemu_ld_i64(tcg_ctx, s->tmp1_i64, s->A0,
                                                s->mem_index, MO_LEQ);
                            gen_helper_fldl_FT0(tcg_ctx, tcg_ctx->cpu_env, s->tmp1_i64);
                            break;
                        case 3:
                        default:
                            tcg_gen_qemu_ld_i32(tcg_ctx, s->tmp2_i32, s->A0,
                                                s->mem_index, MO_LESW);
                            gen_helper_fildl_FT0(tcg_ctx, tcg_ctx->cpu_env, s->tmp2_i32);
                            break;
                        }

                        gen_helper_fp_arith_ST0_FT0(tcg_ctx, op1);
]=] [=[                        if (op1 == 1) gen_helper_fwait(tcg_ctx, tcg_ctx->cpu_env);
                        switch(op >> 4) {
                        case 0:
                            tcg_gen_qemu_ld_i32(tcg_ctx, s->tmp2_i32, s->A0,
                                                s->mem_index, MO_LEUL);
                            if (op1 != 1) gen_helper_flds_FT0(tcg_ctx, tcg_ctx->cpu_env, s->tmp2_i32);
                            break;
                        case 1:
                            tcg_gen_qemu_ld_i32(tcg_ctx, s->tmp2_i32, s->A0,
                                                s->mem_index, MO_LEUL);
                            if (op1 != 1) gen_helper_fildl_FT0(tcg_ctx, tcg_ctx->cpu_env, s->tmp2_i32);
                            break;
                        case 2:
                            tcg_gen_qemu_ld_i64(tcg_ctx, s->tmp1_i64, s->A0,
                                                s->mem_index, MO_LEQ);
                            if (op1 != 1) gen_helper_fldl_FT0(tcg_ctx, tcg_ctx->cpu_env, s->tmp1_i64);
                            break;
                        case 3:
                        default:
                            tcg_gen_qemu_ld_i32(tcg_ctx, s->tmp2_i32, s->A0,
                                                s->mem_index, MO_LESW);
                            if (op1 != 1) gen_helper_fildl_FT0(tcg_ctx, tcg_ctx->cpu_env, s->tmp2_i32);
                            break;
                        }

                        if (op1 == 1) {
                            if ((op >> 4) != 2) tcg_gen_extu_i32_i64(tcg_ctx, s->tmp1_i64, s->tmp2_i32);
                            gen_helper_fmul_memory(tcg_ctx, tcg_ctx->cpu_env, s->tmp1_i64,
                                tcg_const_i32(tcg_ctx, op >> 4));
                        } else gen_helper_fp_arith_ST0_FT0(tcg_ctx, op1, -1);
]=] 1)
    qa_native_guest_replace_text(translate [=[                            gen_helper_fp_arith_STN_ST0(tcg_ctx, op1, opreg);
                            if (op >= 0x30)
                                gen_helper_fpop(tcg_ctx, tcg_ctx->cpu_env);]=] [=[                            gen_helper_fp_arith_STN_ST0(tcg_ctx, op1, opreg, op >= 0x30);
                            if (op >= 0x30 && op1 != 1)
                                gen_helper_fpop(tcg_ctx, tcg_ctx->cpu_env);]=] 1)
    qa_native_guest_replace_text(translate [=[                            gen_helper_fmov_FT0_STN(tcg_ctx, tcg_ctx->cpu_env, tcg_const_i32(tcg_ctx, opreg));
                            gen_helper_fp_arith_ST0_FT0(tcg_ctx, op1);]=] [=[                            if (op1 != 1) gen_helper_fmov_FT0_STN(tcg_ctx, tcg_ctx->cpu_env, tcg_const_i32(tcg_ctx, opreg));
                            gen_helper_fp_arith_ST0_FT0(tcg_ctx, op1, opreg);]=] 1)
    qa_native_guest_write_source("${translate_extension}" "${translate}")
    qa_native_guest_replace_source(x86_64-softmmu "${translate_original}" "${translate_extension}")
    target_include_directories(x86_64-softmmu BEFORE PRIVATE "${extension_root}")

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
    qa_native_guest_write_source("${store_extension}" "${stores}")
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
    qa_native_guest_write_source("${flush_extension}" "${flush}")
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
    qa_native_guest_write_source("${hash_extension}" "${hashes}")
    qa_native_guest_replace_source(unicorn-common "${hash_original}" "${hash_extension}")
    target_include_directories(unicorn PRIVATE "${guest_root}" "${project_headers}")
    target_include_directories(x86_64-softmmu PRIVATE "${guest_root}" "${project_headers}"
        "${qa_unicorn_SOURCE_DIR}/qemu/accel/tcg")
    target_sources(qa_native PRIVATE
        "${guest_root}/memory.c"
        "${guest_root}/file_memory.c"
        "${guest_root}/vm.c"
        "${guest_root}/cpu.c"
        "${guest_root}/cpu_clock.c"
        "${guest_root}/abi.c"
        "${guest_root}/scalar.c"
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
        "${guest_root}/sysv_platform.c"
        "${guest_root}/scan.c"
        "${guest_root}/sysv_scan.c"
        "${guest_root}/sysv_cxx.c"
        "${guest_root}/sysv_cxx_data.c"
        "${guest_root}/sysv_cxx_locale.c"
        "${guest_root}/sysv_iostream.c"
        "${guest_root}/sysv_process.c"
        "${guest_root}/sysv_process_save.c"
        "${guest_root}/sysv_program.c"
        "${guest_root}/sysv_program_kernel.c"
        "${guest_root}/sysv_program_save.c"
        "${guest_root}/windows_runtime.c"
        "${guest_root}/windows_process.c"
        "${guest_root}/windows_process_save.c"
        "${guest_root}/windows_kernel.c"
        "${guest_root}/windows_crt.c"
        "${guest_root}/windows_stdio.c"
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
