/* Text-include at the end of actual pinned x86 fpu_helper.c. Binary80 values
 * use its genuine SoftFloat types/rounding, including on ARM64 hosts. */
#include "unicorn_abi_fp.h"
#include "uc_priv.h"

UNICORN_EXPORT unsigned qa_unicorn_abi_fp_revision(void)
{ return QA_UNICORN_ABI_FP_REVISION; }

UNICORN_EXPORT uc_err qa_unicorn_abi_fp(uc_engine *engine, bool wide,
    uint64_t *bits, bool push)
{
    if (!engine || !bits || !engine->init_done || engine->arch != UC_ARCH_X86 ||
        engine->mode != UC_MODE_32) return UC_ERR_ARG;
    CPUX86State *env = engine->cpu->env_ptr;
    unsigned slot = push ? (env->fpstt - 1) & 7 : env->fpstt;
    bool stack_fault = push ? !env->fptags[slot] : !!env->fptags[slot];
    floatx80 value;
    if (push) {
        env->fpus &= ~0x200;
        float_status status = env->fp_status;
        set_flush_to_zero(false, &status);
        set_flush_inputs_to_zero(false, &status);
        set_float_exception_flags(0, &status);
        value = wide ? float64_to_floatx80(*bits, &status) :
            float32_to_floatx80((uint32_t)*bits, &status);
        if (get_float_exception_flags(&status) & float_flag_invalid) {
            fpu_set_exception(env, FPUS_IE);
            if (!(env->fpuc & FPUS_IE)) return UC_ERR_EXCEPTION;
        }
    } else value = env->fpregs[slot].d;
    if (stack_fault) {
        env->fpus = (env->fpus & ~0x200) | FPUS_SF | (push ? 0x200 : 0);
        fpu_set_exception(env, FPUS_IE);
        if (!(env->fpuc & FPUS_IE)) return UC_ERR_EXCEPTION;
        value = make_floatx80(0xffff, UINT64_C(0xc000000000000000));
    }
    if (!floatx80_invalid_encoding(value) && floatx80_is_any_nan(value))
        value = floatx80_silence_nan(value, &env->fp_status);
    if (push) {
        env->fpregs[slot].d = value; env->fpstt = slot; env->fptags[slot] = 0;
    } else {
        /* The ABI oracle does not raise conversion flags while observing a
         * result. It consumes the returned x87 slot after decoding it. */
        float_status status = env->fp_status;
        set_flush_to_zero(false, &status);
        set_flush_inputs_to_zero(false, &status);
        *bits = wide ? floatx80_to_float64(value, &status) : floatx80_to_float32(value, &status);
        fpop(env);
    }
    return UC_ERR_OK;
}
