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

static qa_unicorn_unary_state qa_unicorn_unary_operand(CPUX86State *env,
    bool infinity_invalid, floatx80 *source, float_status *status)
{
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

static qa_unicorn_unary_state qa_unicorn_unary_begin(CPUX86State *env,
    uintptr_t retaddr, bool infinity_invalid, floatx80 *source, float_status *status)
{
    qa_unicorn_x87_begin(env, retaddr, status);
    return qa_unicorn_unary_operand(env, infinity_invalid, source, status);
}

static bool qa_unicorn_trig_outside(floatx80 source)
{
    unsigned exponent = source.high & 0x7fff;
    return exponent >= 0x403e && exponent < 0x7fff && !floatx80_invalid_encoding(source);
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
    if (qa_unicorn_trig_outside(source)) {
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

static void qa_unicorn_trig_push(CPUX86State *env, bool tangent, uintptr_t retaddr)
{
    float_status status;
    qa_unicorn_x87_begin(env, retaddr, &status);
    floatx80 source = ST0;
    bool empty = env->fptags[env->fpstt] != 0;
    if (!empty && qa_unicorn_trig_outside(source)) {
        env->fpus |= 0x400;
        return;
    }
    unsigned pushed = (env->fpstt - 1) & 7;
    bool overflow = env->fptags[pushed] == 0;
    if (empty || overflow) {
        env->fpus |= FPUS_SF;
        if (!empty) env->fpus |= 0x200;
        fpu_set_exception(env, FPUS_IE);
        if (!(env->fpuc & FPUS_IE)) return;
        ST0 = floatx80_default_nan(&status);
        env->fptags[env->fpstt] = 0;
        fpush(env);
        ST0 = floatx80_default_nan(&status);
        env->fpus &= ~0x400;
        return;
    }
    qa_unicorn_unary_state input = qa_unicorn_unary_operand(env, true, &source, &status);
    if (input == QA_UNICORN_UNARY_SUPPRESSED) return;
    env->fpus &= ~0x400;
    qa_unicorn_x87_math_result first_result = { 0 }, pushed_result = { 0 };
    if (input == QA_UNICORN_UNARY_HANDLED) {
        first_result.value = ST0;
        pushed_result.value = tangent ? int32_to_floatx80(1, &status) : ST0;
    } else if (floatx80_is_zero(source)) {
        first_result.value = source;
        pushed_result.value = int32_to_floatx80(1, &status);
    } else {
        qa_x87_trig_argument argument = qa_x87_reduce_trig(source);
        qa_x87_scaled first = qa_x87_sincos_coefficient(&argument, false);
        qa_x87_scaled second = qa_x87_sincos_coefficient(&argument, true);
        if (tangent) {
            first_result = qa_unicorn_x87_tangent(first, second, env->fpuc, &status);
            pushed_result.value = int32_to_floatx80(1, &status);
        } else {
            first_result = qa_x87_pack(first.magnitude, first.exponent, first.negative,
                first.remainder, env->fpuc, &status);
            pushed_result = qa_x87_pack(second.magnitude, second.exponent, second.negative,
                second.remainder, env->fpuc, &status);
        }
    }
    ST0 = first_result.value;
    fpush(env);
    ST0 = pushed_result.value;
    if (tangent ? first_result.rounded_up : pushed_result.rounded_up) env->fpus |= 0x200;
    unsigned exceptions = first_result.exceptions | pushed_result.exceptions;
    if (exceptions) fpu_set_exception(env, exceptions);
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

static unsigned qa_unicorn_arithmetic_exceptions(float_status *status)
{
    int flags = get_float_exception_flags(status);
    return ((flags & float_flag_invalid) ? FPUS_IE : 0) |
        ((flags & float_flag_divbyzero) ? FPUS_ZE : 0) |
        ((flags & float_flag_overflow) ? FPUS_OE : 0) |
        ((flags & float_flag_underflow) ? FPUS_UE : 0) |
        ((flags & float_flag_inexact) ? FPUS_PE : 0);
}

static floatx80 qa_unicorn_arithmetic_value(int operation, floatx80 left,
    floatx80 right, float_status *status)
{
    switch (operation) {
    case 0: return floatx80_add(left, right, status);
    case 1: return floatx80_mul(left, right, status);
    case 4: return floatx80_sub(left, right, status);
    case 5: return floatx80_sub(right, left, status);
    case 6: return floatx80_div(left, right, status);
    default: return floatx80_div(right, left, status);
    }
}

static int qa_unicorn_arithmetic_normalize(floatx80 *source)
{
    int shift = clz64(source->low);
    int exponent = qa_x87_operand_exponent(*source) + 63 - shift;
    source->low <<= shift;
    return exponent;
}

/* Change a product or quotient exponent without rounding either operand.
 * Normalize subnormal inputs, then distribute the requested shift so both
 * factors remain normal binary80 inputs to the existing SoftFloat kernel.
 * Wrapped exponent sums/differences lie within [-8314,8252], so they always
 * fit inside the two normal operand ranges. */
static void qa_unicorn_product_rescale(floatx80 *left, floatx80 *right,
    int adjustment, bool quotient)
{
    int left_exponent = qa_unicorn_arithmetic_normalize(left);
    int right_exponent = qa_unicorn_arithmetic_normalize(right);
    int right_sign = quotient ? -1 : 1;
    int combined = left_exponent + right_sign * right_exponent + adjustment;
    left_exponent += adjustment;
    if (left_exponent < -16382) left_exponent = -16382;
    if (left_exponent > 16383) left_exponent = 16383;
    right_exponent = right_sign * (combined - left_exponent);
    if (right_exponent < -16382) right_exponent = -16382;
    if (right_exponent > 16383) right_exponent = 16383;
    left_exponent = combined - right_sign * right_exponent;
    left->high = (left->high & 0x8000) | (left_exponent + 16383);
    right->high = (right->high & 0x8000) | (right_exponent + 16383);
}

/* Unlike a product, a sum requires the same scale on both operands. A tiny
 * sum can only come from low-exponent inputs, so its upward adjustment is
 * exact. For an overflowing sum the scaled dominant exponent is at least
 * -8194. Any addend shifted below the normal range is then over 8100 bits
 * smaller: its nonzero sign alone supplies SoftFloat's sticky rounding tail. */
static void qa_unicorn_sum_rescale(floatx80 *source, int adjustment)
{
    if (floatx80_is_zero(*source)) return;
    int exponent = qa_unicorn_arithmetic_normalize(source) + adjustment;
    if (exponent < -16382) {
        exponent = -16382;
        source->low = UINT64_C(0x8000000000000000);
    }
    source->high = (source->high & 0x8000) | (exponent + 16383);
}

/* source >= 0 names ST(source). A memory source carries its original raw
 * bits and the translator's op>>4 format: f32, i32, f64, i16. Converting it
 * here preserves SNaN and denormal admission before the destination changes.
 * Operations use the translator's selectors: 0 add, 1 multiply, 4 subtract,
 * 5 reverse subtract, 6 divide, 7 reverse divide. Register, memory, integer
 * and popping forms share admission, computation and publication. */
static void qa_unicorn_arithmetic(CPUX86State *env, int operation,
    int destination, int source, bool pop, int format, uint64_t bits,
    uintptr_t retaddr)
{
    float_status status;
    qa_unicorn_x87_begin(env, retaddr, &status);
    set_float_detect_tininess(float_tininess_after_rounding, &status);
    unsigned destination_slot = (env->fpstt + destination) & 7;
    bool empty = env->fptags[destination_slot] ||
        (source >= 0 && env->fptags[(env->fpstt + source) & 7]);
    floatx80 left = ST(destination), right, value;
    unsigned exceptions = 0;
    bool rounded_up = false, memory_denormal = false;
    if (empty) {
        env->fpus |= FPUS_SF;
        exceptions = FPUS_IE;
        value = floatx80_default_nan(&status);
        goto finish;
    }
    if (source >= 0) right = ST(source);
    else {
        switch (format) {
        case 0:
            memory_denormal = !(bits & UINT64_C(0x7f800000)) &&
                (bits & UINT64_C(0x007fffff));
            right = float32_to_floatx80((uint32_t)bits, &status);
            break;
        case 1:
            right = int32_to_floatx80((int32_t)(uint32_t)bits, &status);
            break;
        case 2:
            memory_denormal = !(bits & UINT64_C(0x7ff0000000000000)) &&
                (bits & UINT64_C(0x000fffffffffffff));
            right = float64_to_floatx80(bits, &status);
            break;
        default:
            right = int32_to_floatx80((int16_t)(uint16_t)bits, &status);
            break;
        }
    }
    if (floatx80_invalid_encoding(left) || floatx80_invalid_encoding(right) ||
        floatx80_is_any_nan(left) || floatx80_is_any_nan(right)) {
        value = qa_unicorn_arithmetic_value(operation, left, right, &status);
        exceptions = qa_unicorn_arithmetic_exceptions(&status);
        goto finish;
    }
    /* Real ZE takes precedence over DE, even for a denormal numerator.
     * SoftFloat distinguishes finite/zero from invalid 0/0 and infinity/0;
     * unlike the old helper, merely having a zero divisor does not set ZE. */
    if ((operation == 6 && floatx80_is_zero(right)) ||
        (operation == 7 && floatx80_is_zero(left))) {
        value = qa_unicorn_arithmetic_value(operation, left, right, &status);
        exceptions = qa_unicorn_arithmetic_exceptions(&status);
        goto finish;
    }
    bool denormal_left = qa_unicorn_x87_denormal(&left);
    bool denormal_right = qa_unicorn_x87_denormal(&right);
    if (memory_denormal || denormal_left || denormal_right) {
        fpu_set_exception(env, FPUS_DE);
        if (!(env->fpuc & FPUS_DE)) return;
    }
    set_float_exception_flags(0, &status);
    value = qa_unicorn_arithmetic_value(operation, left, right, &status);
    exceptions = qa_unicorn_arithmetic_exceptions(&status);
    if (!(exceptions & (FPUS_IE | FPUS_ZE))) {
        /* IEEE SoftFloat raises UE for tiny AND inexact results. Unmasked
         * x87 UE also reports an exact nonzero tiny register result. */
        bool tiny = (exceptions & FPUS_UE) ||
            (floatx80_is_zero_or_denormal(value) && !floatx80_is_zero(value));
        /* At reduced PC, masked denormal rounding can reach minimum-normal
         * even when unlimited-exponent rounding is still tiny. Decide that
         * boundary using the scaled computation, not the masked result. */
        bool minimum_normal = (value.high & 0x7fff) == 1 &&
            value.low == UINT64_C(0x8000000000000000);
        bool check_underflow = !(env->fpuc & FPUS_UE) && (tiny || minimum_normal);
        unsigned wrapped = (exceptions & FPUS_OE) && !(env->fpuc & FPUS_OE) ? FPUS_OE : 0;
        if (wrapped || check_underflow) {
            floatx80 scaled_left = left, scaled_right = right;
            int adjustment = wrapped ? -24576 : 24576;
            if (operation == 1 || operation == 6 || operation == 7) {
                qa_unicorn_product_rescale(operation == 7 ? &scaled_right : &scaled_left,
                    operation == 7 ? &scaled_left : &scaled_right, adjustment, operation != 1);
            } else {
                qa_unicorn_sum_rescale(&scaled_left, adjustment);
                qa_unicorn_sum_rescale(&scaled_right, adjustment);
            }
            float_status scaled_status = status;
            set_float_exception_flags(0, &scaled_status);
            floatx80 scaled_value = qa_unicorn_arithmetic_value(operation,
                scaled_left, scaled_right, &scaled_status);
            if (check_underflow) {
                exceptions &= ~FPUS_UE;
                if ((scaled_value.high & 0x7fff) < 0x6001) wrapped = FPUS_UE;
            }
            if (wrapped) {
                left = scaled_left;
                right = scaled_right;
                status = scaled_status;
                value = scaled_value;
                exceptions = wrapped | qa_unicorn_arithmetic_exceptions(&status);
            }
        }
        if (exceptions & FPUS_PE) {
            float_status truncated = status;
            set_float_exception_flags(0, &truncated);
            set_float_rounding_mode(float_round_to_zero, &truncated);
            floatx80 toward_zero = qa_unicorn_arithmetic_value(operation, left, right, &truncated);
            rounded_up = floatx80_lt_quiet(floatx80_abs(toward_zero),
                floatx80_abs(value), &truncated);
        }
    }
finish:
    /* IE, ZE and DE are pre-computation exceptions. OE/UE/PE register results
     * are stored, including the pop, before their deferred handler runs. */
    if (exceptions & (FPUS_IE | FPUS_ZE)) {
        unsigned pre_computation = exceptions & (FPUS_IE | FPUS_ZE);
        fpu_set_exception(env, pre_computation);
        if (pre_computation & ~env->fpuc) return;
    }
    ST(destination) = value;
    env->fptags[destination_slot] = 0;
    if (pop) fpop(env);
    if (rounded_up) env->fpus |= 0x200;
    if (exceptions & ~(FPUS_IE | FPUS_ZE))
        fpu_set_exception(env, exceptions & ~(FPUS_IE | FPUS_ZE));
}

void helper_farithmetic_memory(CPUX86State *env, uint64_t bits, int format,
    int operation)
{
    qa_unicorn_arithmetic(env, operation, 0, -1, false, format, bits, GETPC());
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
