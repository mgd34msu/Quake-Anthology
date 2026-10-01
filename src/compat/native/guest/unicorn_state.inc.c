/* Include in Unicorn 2.1.4 qemu/target/i386/unicorn.c after its CPU headers.
 * CPUX86State and SegmentCache are the dependency's actual compiled types.
 * No copied private layout, host pointer, or opaque context is persisted. */
#include "unicorn_state.h"

_Static_assert(UC_API_MAJOR == 2 && UC_API_MINOR == 1 &&
    UC_API_PATCH == 4 && UC_API_EXTRA == 255, "native guest requires pinned Unicorn 2.1.4");

UNICORN_EXPORT unsigned qa_unicorn_state_revision(void)
{
    return QA_UNICORN_STATE_REVISION;
}

UNICORN_EXPORT uc_err qa_unicorn_x86_state(uc_engine *engine,
    qa_native_guest_cpu *state, bool writing)
{
    if (!engine || !state || engine->arch != UC_ARCH_X86 ||
        (engine->mode != UC_MODE_32 && engine->mode != UC_MODE_64)) return UC_ERR_ARG;
    /* This public read initializes the actual engine without executing code. */
    uint32_t scratch = 0;
    size_t scratch_bytes = sizeof(scratch);
    uc_err code = uc_reg_read2(engine, UC_X86_REG_EAX, &scratch, &scratch_bytes);
    if (code != UC_ERR_OK) return code;
    if (scratch_bytes != sizeof(scratch)) return UC_ERR_ARG;
    CPUX86State *env = engine->cpu->env_ptr;
    static const int order[6] = {R_CS, R_DS, R_ES, R_SS, R_FS, R_GS};
    SegmentCache *descriptors[4] = {&env->gdt, &env->idt, &env->ldt, &env->tr};
    for (size_t i = 0; i < 10; ++i) {
        SegmentCache *actual = i < 6 ? &env->segs[order[i]] : descriptors[i - 6];
        qa_native_guest_table *saved = i < 6 ? &state->segments[i] : &state->tables[i - 6];
        if (writing) {
            actual->selector = saved->selector;
            actual->base = saved->base;
            actual->limit = saved->limit;
            actual->flags = saved->flags;
        } else {
            *saved = (qa_native_guest_table){(uint16_t)actual->selector,
                actual->base, actual->limit, actual->flags};
        }
    }
    for (size_t i = 0; i < 9; ++i) {
        if (writing) env->cr[i] = state->control[i];
        else state->control[i] = env->cr[i];
    }
    for (size_t i = 0; i < 8; ++i) {
        if (writing) env->dr[i] = state->debug[i];
        else state->debug[i] = env->dr[i];
    }
    if (writing) {
        env->efer = state->efer;
        env->xcr0 = state->xcr0;
        env->xstate_bv = state->xstate_bv;
        /* reg_reset sets execution widths independently of zeroed segment
         * flags. Preserve the actual flags rather than reloading selectors. */
        env->hflags = state->execution_flags;
        env->hflags2 = state->execution_flags2;
        memcpy(&env->a20_mask, &state->a20_mask, sizeof(state->a20_mask));
        code = uc_ctl_flush_tlb(engine);
        if (code == UC_ERR_OK) code = uc_ctl_flush_tb(engine);
        return code;
    }
    state->efer = env->efer;
    state->xcr0 = env->xcr0;
    state->xstate_bv = env->xstate_bv;
    state->execution_flags = env->hflags;
    state->execution_flags2 = env->hflags2;
    memcpy(&state->a20_mask, &env->a20_mask, sizeof(state->a20_mask));
    return UC_ERR_OK;
}
