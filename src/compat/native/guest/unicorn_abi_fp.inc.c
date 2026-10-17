/* Text-include at the end of actual pinned x86 fpu_helper.c. Binary80 values
 * use its genuine SoftFloat types/rounding, including on ARM64 hosts. */
#include "unicorn_abi_fp.h"
#include "uc_priv.h"
#include "unicorn_abi_fp_math.inc.c"

typedef enum qa_unicorn_unary_state {
    QA_UNICORN_UNARY_READY, QA_UNICORN_UNARY_HANDLED, QA_UNICORN_UNARY_SUPPRESSED
} qa_unicorn_unary_state;

static void qa_unicorn_x87_begin(CPUX86State *env, uintptr_t retaddr,
    float_status *status)
{
    if (env->fpus & FPUS_SE) fpu_raise_exception(env, retaddr);
    env->fpus &= ~0x200;
    *status = env->fp_status;
    set_flush_to_zero(false, status);
    set_flush_inputs_to_zero(false, status);
    set_float_exception_flags(0, status);
}

static bool qa_unicorn_x87_denormal(floatx80 *source)
{
    if (!floatx80_is_zero_or_denormal(*source) || floatx80_is_zero(*source)) return false;
    /* Intel SDM 1, 8.2.2: pseudo-denormals raise DE and use exponent 1. */
    if (source->low & UINT64_C(0x8000000000000000)) source->high |= 1;
    return true;
}

static qa_unicorn_unary_state qa_unicorn_unary_begin(CPUX86State *env,
    uintptr_t retaddr, bool infinity_invalid, floatx80 *source, float_status *status)
{
    qa_unicorn_x87_begin(env, retaddr, status);
    bool empty = env->fptags[env->fpstt] != 0;
    *source = ST0;
    bool special = !empty && (floatx80_invalid_encoding(*source) || floatx80_is_any_nan(*source));
    bool invalid_infinity = !empty && !special && infinity_invalid && floatx80_is_infinity(*source);
    if (empty || special || invalid_infinity) {
        floatx80 result = empty || invalid_infinity ? floatx80_default_nan(status) :
            floatx80_round_to_int(*source, status);
        int exception = empty || invalid_infinity ||
            (get_float_exception_flags(status) & float_flag_invalid) ? FPUS_IE : 0;
        if (empty) env->fpus |= FPUS_SF;
        if (exception) fpu_set_exception(env, exception);
        if (exception & ~env->fpuc) return QA_UNICORN_UNARY_SUPPRESSED;
        ST0 = result;
        env->fptags[env->fpstt] = 0;
        return QA_UNICORN_UNARY_HANDLED;
    }
    if (qa_unicorn_x87_denormal(source)) {
        fpu_set_exception(env, FPUS_DE);
        if (!(env->fpuc & FPUS_DE)) return QA_UNICORN_UNARY_SUPPRESSED;
    }
    return QA_UNICORN_UNARY_READY;
}

static void qa_unicorn_frndint(CPUX86State *env, uintptr_t retaddr)
{
    floatx80 source;
    float_status status;
    if (qa_unicorn_unary_begin(env, retaddr, false, &source, &status) != QA_UNICORN_UNARY_READY)
        return;
    floatx80 rounded = floatx80_round_to_int(source, &status);
    int flags = get_float_exception_flags(&status);

    /* Unlike IE/DE, unmasked PE commits the rounded result (SDM 1, 8.5.6). */
    ST0 = rounded;
    if (flags & float_flag_inexact) {
        if (floatx80_lt_quiet(floatx80_abs(source), floatx80_abs(rounded), &status))
            env->fpus |= 0x200;
        fpu_set_exception(env, FPUS_PE);
    }
}

static void qa_unicorn_trig(CPUX86State *env, bool cosine, uintptr_t retaddr)
{
    floatx80 source;
    float_status status;
    qa_unicorn_unary_state input = qa_unicorn_unary_begin(env, retaddr, true, &source, &status);
    if (input == QA_UNICORN_UNARY_SUPPRESSED) return;
    if (input == QA_UNICORN_UNARY_HANDLED) {
        env->fpus &= ~0x400;
        return;
    }
    const floatx80 limit = make_floatx80(0x403e, UINT64_C(0x8000000000000000));
    if (floatx80_le_quiet(limit, floatx80_abs(source), &status)) {
        env->fpus |= 0x400;
        return;
    }
    env->fpus &= ~0x400;
    if (floatx80_is_zero(source)) {
        ST0 = cosine ? int32_to_floatx80(1, &status) : source;
        return;
    }
    qa_unicorn_x87_math_result result = qa_unicorn_x87_sincos(source,
        cosine, env->fpuc, &status);
    ST0 = result.value;
    if (result.rounded_up) env->fpus |= 0x200;
    fpu_set_exception(env, result.exceptions);
}

static void qa_unicorn_fpatan(CPUX86State *env, uintptr_t retaddr)
{
    float_status status;
    qa_unicorn_x87_begin(env, retaddr, &status);
    unsigned destination = (env->fpstt + 1) & 7;
    floatx80 x = ST0, y = ST1;
    bool empty = env->fptags[env->fpstt] || env->fptags[destination];
    bool unsupported = floatx80_invalid_encoding(x) || floatx80_invalid_encoding(y);
    if (empty || unsupported || floatx80_is_any_nan(x) || floatx80_is_any_nan(y)) {
        floatx80 value = empty || unsupported ? floatx80_default_nan(&status) :
            propagateFloatx80NaN(y, x, &status);
        unsigned exception = empty || unsupported ||
            (get_float_exception_flags(&status) & float_flag_invalid) ? FPUS_IE : 0;
        if (empty) env->fpus |= FPUS_SF;
        if (exception) fpu_set_exception(env, exception);
        if (exception & ~env->fpuc) return;
        ST1 = value;
        env->fptags[destination] = 0;
        fpop(env);
        return;
    }
    bool denormal_x = qa_unicorn_x87_denormal(&x);
    bool denormal_y = qa_unicorn_x87_denormal(&y);
    if (denormal_x || denormal_y) {
        fpu_set_exception(env, FPUS_DE);
        if (!(env->fpuc & FPUS_DE)) return;
    }
    qa_unicorn_x87_math_result result = qa_unicorn_x87_atan2(y, x, env->fpuc, &status);
    ST1 = result.value;
    env->fptags[destination] = 0;
    fpop(env);
    if (result.rounded_up) env->fpus |= 0x200;
    if (result.exceptions) fpu_set_exception(env, result.exceptions);
}

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
