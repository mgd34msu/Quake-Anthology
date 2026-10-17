/* Private binary80 transcendental arithmetic, included by the actual x87
 * helper extension. Q256 needs five limbs: pi has 258 bits at this scale and
 * an admitted trigonometric input can have 319. Small results keep their
 * exponent separately instead of disappearing in a fixed-point conversion. */
#include "fpu/softfloat-macros.h"

typedef struct qa_x87_uint { uint64_t limb[5]; } qa_x87_uint;
typedef struct qa_x87_product { uint64_t limb[10]; } qa_x87_product;
typedef struct qa_unicorn_x87_math_result {
    floatx80 value;
    unsigned exceptions;
    bool rounded_up;
} qa_unicorn_x87_math_result;
typedef struct qa_x87_scaled {
    qa_x87_uint magnitude;
    int exponent;
    bool negative, remainder;
} qa_x87_scaled;
typedef struct qa_x87_trig_argument {
    floatx80 source;
    qa_x87_uint angle;
    int exponent;
    unsigned quadrant;
    bool tiny, reflected;
} qa_x87_trig_argument;

static const qa_x87_uint qa_x87_scale = {{ 0, 0, 0, 0, 1 }};
/* floor(pi * 2^256). Intel SDM 1, 8.3.8 defines the internal Pi from its
 * leading bits; it is not the binary80 FLDPI result. */
static const qa_x87_uint qa_x87_pi = {{
    UINT64_C(0x082efa98ec4e6c89), UINT64_C(0xa4093822299f31d0),
    UINT64_C(0x13198a2e03707344), UINT64_C(0x243f6a8885a308d3), 3
}};
static const qa_x87_uint qa_x87_source_pi = {{
    0, 0, 0, UINT64_C(0x243f6a8885a308d3), 3
}};

static bool qa_x87_nonzero(qa_x87_uint a)
{
    return (a.limb[0] | a.limb[1] | a.limb[2] | a.limb[3] | a.limb[4]) != 0;
}

static int qa_x87_compare(qa_x87_uint a, qa_x87_uint b)
{
    for (int i = 4; i >= 0; --i)
        if (a.limb[i] != b.limb[i]) return a.limb[i] > b.limb[i] ? 1 : -1;
    return 0;
}

static qa_x87_uint qa_x87_add(qa_x87_uint a, qa_x87_uint b)
{
    uint64_t carry = 0;
    for (unsigned i = 0; i < 5; ++i) {
        uint64_t first = a.limb[i] + b.limb[i];
        uint64_t next = first + carry;
        carry = (first < a.limb[i]) | (next < first);
        a.limb[i] = next;
    }
    return a;
}

static qa_x87_uint qa_x87_subtract(qa_x87_uint a, qa_x87_uint b)
{
    uint64_t borrow = 0;
    for (unsigned i = 0; i < 5; ++i) {
        uint64_t first = a.limb[i] - b.limb[i];
        uint64_t next = first - borrow;
        borrow = (a.limb[i] < b.limb[i]) | (first < borrow);
        a.limb[i] = next;
    }
    return a;
}

static qa_x87_uint qa_x87_shift(qa_x87_uint a, int shift)
{
    qa_x87_uint result = {{ 0 }};
    unsigned count = shift < 0 ? (unsigned)-shift : (unsigned)shift;
    unsigned words = count / 64, bits = count % 64;
    if (words >= 5) return result;
    for (unsigned i = 0; i + words < 5; ++i) {
        if (shift >= 0) {
            result.limb[i + words] |= a.limb[i] << bits;
            if (bits && i + words + 1 < 5)
                result.limb[i + words + 1] |= a.limb[i] >> (64 - bits);
        } else {
            result.limb[i] |= a.limb[i + words] >> bits;
            if (bits && i + words + 1 < 5)
                result.limb[i] |= a.limb[i + words + 1] << (64 - bits);
        }
    }
    return result;
}

static void qa_x87_product_add(qa_x87_product *a, unsigned word, uint64_t value)
{
    while (value && word < 10) {
        uint64_t previous = a->limb[word];
        a->limb[word] += value;
        value = a->limb[word++] < previous;
    }
}

static qa_x87_product qa_x87_multiply(qa_x87_uint a, qa_x87_uint b)
{
    qa_x87_product result = {{ 0 }};
    for (unsigned i = 0; i < 5; ++i) {
        for (unsigned j = 0; j < 5; ++j) {
            uint64_t high, low;
            mul64To128(a.limb[i], b.limb[j], &high, &low);
            qa_x87_product_add(&result, i + j, low);
            qa_x87_product_add(&result, i + j + 1, high);
        }
    }
    return result;
}

static qa_x87_product qa_x87_widen(qa_x87_uint a)
{
    qa_x87_product result = {{ 0 }};
    for (unsigned i = 0; i < 5; ++i) result.limb[i] = a.limb[i];
    return result;
}

/* All divisors here have at most 259 bits, so the shifted remainder fits
 * five limbs. Quotients used by the kernel also fit those same five limbs. */
static qa_x87_uint qa_x87_divide(qa_x87_product numerator, qa_x87_uint denominator,
    qa_x87_uint *remainder)
{
    qa_x87_uint result = {{ 0 }}, rest = {{ 0 }};
    int word = 9;
    while (word >= 0 && !numerator.limb[word]) --word;
    int bit = word < 0 ? -1 : word * 64 + 63 - clz64(numerator.limb[word]);
    for (; bit >= 0; --bit) {
        rest = qa_x87_shift(rest, 1);
        rest.limb[0] |= (numerator.limb[bit / 64] >> (bit % 64)) & 1;
        if (qa_x87_compare(rest, denominator) >= 0) {
            rest = qa_x87_subtract(rest, denominator);
            if (bit < 320) result.limb[bit / 64] |= UINT64_C(1) << (bit % 64);
        }
    }
    if (remainder) *remainder = rest;
    return result;
}

static qa_x87_uint qa_x87_divide_word(qa_x87_uint a, uint64_t divisor)
{
    uint64_t rest = 0;
    for (int i = 4; i >= 0; --i)
        a.limb[i] = udiv_qrnnd(&rest, rest, a.limb[i], divisor);
    return a;
}

static qa_x87_uint qa_x87_multiply_fraction(qa_x87_uint a, qa_x87_uint b)
{
    qa_x87_product product = qa_x87_multiply(a, b);
    qa_x87_uint result;
    for (unsigned i = 0; i < 5; ++i) result.limb[i] = product.limb[i + 4];
    return result;
}

static int qa_x87_top_bit(qa_x87_uint a)
{
    for (int i = 4; i >= 0; --i)
        if (a.limb[i]) return i * 64 + 63 - clz64(a.limb[i]);
    return -1;
}

static int qa_x87_operand_exponent(floatx80 source)
{
    unsigned encoded = source.high & 0x7fff;
    return (encoded ? (int)encoded : 1) - 16383 - 63;
}

/* The numerical kernel supplies magnitude * 2^exponent and any positive
 * division remainder. SoftFloat remains the sole binary80 rounding owner.
 * PC does not apply to transcendental instructions (SDM 1, 8.1.5.2). */
static qa_unicorn_x87_math_result qa_x87_pack(qa_x87_uint magnitude, int exponent,
    bool negative, bool remainder, uint16_t control, const float_status *original)
{
    qa_unicorn_x87_math_result result = { .exceptions = FPUS_PE };
    int top = qa_x87_top_bit(magnitude);
    if (top < 0) {
        result.value = make_floatx80(negative ? 0x8000 : 0, 0);
        return result;
    }
    qa_x87_uint normalized = qa_x87_shift(magnitude, 319 - top);
    uint64_t high = normalized.limb[4], low = normalized.limb[3];
    low |= remainder || (normalized.limb[0] | normalized.limb[1] | normalized.limb[2]);
    int biased = exponent + top + 16383;
    float_status status = *original;
    set_float_exception_flags(0, &status);
    set_float_detect_tininess(float_tininess_after_rounding, &status);

    /* For an unmasked tiny register result Intel stores the rounded value
     * scaled by 2^24576. Determine tininess with an unbounded exponent first;
     * an ordinary subnormal pack would lose that result (SDM 1, 8.5.5). */
    bool tiny = false;
    if (biased <= 0) {
        floatx80 scaled = roundAndPackFloatx80(80, negative, biased + 24576,
            high, low, &status);
        tiny = (scaled.high & 0x7fff) <= 24576;
        set_float_exception_flags(0, &status);
        if (tiny && !(control & FPUS_UE)) biased += 24576;
    }
    result.value = roundAndPackFloatx80(80, negative, biased, high, low, &status);
    if (tiny || (get_float_exception_flags(&status) & float_flag_underflow))
        result.exceptions |= FPUS_UE;

    float_status truncated = status;
    set_float_exception_flags(0, &truncated);
    set_float_rounding_mode(float_round_to_zero, &truncated);
    floatx80 toward_zero = roundAndPackFloatx80(80, negative, biased, high, low, &truncated);
    result.rounded_up = floatx80_lt_quiet(floatx80_abs(toward_zero),
        floatx80_abs(result.value), &truncated);
    return result;
}

static qa_x87_uint qa_x87_series(qa_x87_uint angle, bool cosine)
{
    qa_x87_uint squared = qa_x87_multiply_fraction(angle, angle);
    qa_x87_uint term = cosine ? qa_x87_scale : angle, sum = term;
    /* |angle| <= pi/4. Each recurrence truncates by less than one Q256
     * quantum; the alternating remainder decreases to that same scale. */
    for (uint64_t index = 1; qa_x87_nonzero(term); ++index) {
        uint64_t twice = index * 2;
        uint64_t divisor = cosine ? (twice - 1) * twice : twice * (twice + 1);
        term = qa_x87_divide_word(qa_x87_multiply_fraction(term, squared), divisor);
        sum = index & 1 ? qa_x87_subtract(sum, term) : qa_x87_add(sum, term);
    }
    return sum;
}

static qa_x87_trig_argument qa_x87_reduce_trig(floatx80 source)
{
    qa_x87_uint coefficient = {{ source.low, 0, 0, 0, 0 }};
    qa_x87_trig_argument argument = {
        .source = source, .exponent = qa_x87_operand_exponent(source)
    };
    argument.tiny = argument.exponent + 63 - clz64(source.low) <= -128;
    if (argument.tiny) return argument;
    qa_x87_uint period = qa_x87_shift(qa_x87_source_pi, 1);
    qa_x87_divide(qa_x87_widen(qa_x87_shift(coefficient, argument.exponent + 256)),
        period, &argument.angle);
    qa_x87_uint half_pi = qa_x87_shift(qa_x87_source_pi, -1);
    while (qa_x87_compare(argument.angle, half_pi) >= 0) {
        argument.angle = qa_x87_subtract(argument.angle, half_pi);
        ++argument.quadrant;
    }
    argument.reflected = qa_x87_compare(argument.angle, qa_x87_shift(half_pi, -1)) > 0;
    if (argument.reflected) argument.angle = qa_x87_subtract(half_pi, argument.angle);
    argument.angle = qa_x87_divide(qa_x87_multiply(argument.angle, qa_x87_pi),
        qa_x87_source_pi, NULL);
    return argument;
}

static qa_x87_scaled qa_x87_sincos_coefficient(const qa_x87_trig_argument *argument,
    bool cosine)
{
    qa_x87_scaled result = {
        .exponent = -256, .negative = (argument->source.high & 0x8000) && !cosine
    };
    if (argument->tiny) {
        if (cosine) {
            const qa_x87_uint one = {{ 1, 0, 0, 0, 0 }};
            result.magnitude = qa_x87_subtract(qa_x87_scale, one);
        } else {
            /* sin(x*pi/Pi) has the sign and scale of x even below Q256. The
             * cubic term here is below the retained guard precision. */
            qa_x87_uint coefficient = {{ argument->source.low, 0, 0, 0, 0 }};
            qa_x87_uint divisor = qa_x87_shift(qa_x87_source_pi, -190), rest;
            result.magnitude = qa_x87_divide(qa_x87_multiply(coefficient, qa_x87_pi),
                divisor, &rest);
            result.exponent = argument->exponent - 190;
            result.remainder = qa_x87_nonzero(rest);
        }
        return result;
    }
    result.negative ^= ((argument->quadrant + (cosine ? 1 : 0)) & 2) != 0;
    bool evaluate_cosine = cosine ^ (argument->quadrant & 1) ^ argument->reflected;
    result.magnitude = qa_x87_series(argument->angle, evaluate_cosine);
    return result;
}

static qa_unicorn_x87_math_result qa_unicorn_x87_sincos(floatx80 source,
    bool cosine, uint16_t control, const float_status *status)
{
    qa_x87_trig_argument argument = qa_x87_reduce_trig(source);
    qa_x87_scaled value = qa_x87_sincos_coefficient(&argument, cosine);
    return qa_x87_pack(value.magnitude, value.exponent, value.negative,
        value.remainder, control, status);
}

static qa_unicorn_x87_math_result qa_unicorn_x87_tangent(qa_x87_scaled sine,
    qa_x87_scaled cosine, uint16_t control, const float_status *status)
{
    int sine_top = qa_x87_top_bit(sine.magnitude);
    int cosine_top = qa_x87_top_bit(cosine.magnitude);
    qa_x87_uint numerator = qa_x87_shift(sine.magnitude, 255 - sine_top);
    qa_x87_uint denominator = qa_x87_shift(cosine.magnitude, 255 - cosine_top), rest;
    /* Divide before binary80 rounding, retaining the quotient's exponent.
     * Near a pole its Q256 representation can exceed the five-limb range.
     * An admitted nonzero binary80 input cannot equal an odd multiple of
     * internal Pi/2: that dyadic constant requires 66 significant bits. */
    qa_x87_uint quotient = qa_x87_divide(qa_x87_multiply(numerator, qa_x87_scale),
        denominator, &rest);
    int exponent = sine.exponent - cosine.exponent + sine_top - cosine_top - 256;
    return qa_x87_pack(quotient, exponent, sine.negative ^ cosine.negative,
        qa_x87_nonzero(rest) || sine.remainder || cosine.remainder, control, status);
}

static qa_x87_uint qa_x87_atan_series(qa_x87_uint argument)
{
    qa_x87_uint squared = qa_x87_multiply_fraction(argument, argument);
    qa_x87_uint power = argument, angle = argument;
    /* The reduction below gives |argument| <= 1/2. */
    for (uint64_t index = 1; qa_x87_nonzero(power); ++index) {
        power = qa_x87_multiply_fraction(power, squared);
        qa_x87_uint term = qa_x87_divide_word(power, index * 2 + 1);
        if (!qa_x87_nonzero(term)) break;
        angle = index & 1 ? qa_x87_subtract(angle, term) : qa_x87_add(angle, term);
    }
    return angle;
}

static qa_unicorn_x87_math_result qa_unicorn_x87_atan2(floatx80 y, floatx80 x,
    uint16_t control, const float_status *status)
{
    bool negative = (y.high & 0x8000) != 0;
    bool negative_x = (x.high & 0x8000) != 0;
    bool infinite_y = floatx80_is_infinity(y), infinite_x = floatx80_is_infinity(x);
    qa_x87_uint angle;
    if (floatx80_is_zero(y) || infinite_x) {
        if (infinite_y) {
            angle = qa_x87_shift(qa_x87_pi, -2);
            if (negative_x) angle = qa_x87_subtract(qa_x87_pi, angle);
        } else if (negative_x) angle = qa_x87_pi;
        else {
            qa_unicorn_x87_math_result zero = {
                .value = make_floatx80(negative ? 0x8000 : 0, 0)
            };
            return zero;
        }
        return qa_x87_pack(angle, -256, negative, false, control, status);
    }
    if (infinite_y || floatx80_is_zero(x))
        return qa_x87_pack(qa_x87_shift(qa_x87_pi, -1), -256,
            negative, false, control, status);

    /* Separate normalized mantissas and exponents: a valid ratio can be far
     * below the smallest binary80 number, or above its largest one. */
    int y_shift = clz64(y.low), x_shift = clz64(x.low);
    uint64_t numerator = y.low << y_shift, denominator = x.low << x_shift;
    int exponent = qa_x87_operand_exponent(y) + 63 - y_shift -
        (qa_x87_operand_exponent(x) + 63 - x_shift);
    bool reciprocal = exponent > 0 || (!exponent && numerator > denominator);
    if (reciprocal) {
        uint64_t temporary = numerator;
        numerator = denominator; denominator = temporary;
        exponent = -exponent;
    }
    qa_x87_uint dividend = {{ numerator, 0, 0, 0, 0 }};
    qa_x87_uint divisor = {{ denominator, 0, 0, 0, 0 }}, rest;
    qa_x87_uint ratio = qa_x87_divide(
        qa_x87_widen(qa_x87_shift(dividend, 256)), divisor, &rest);
    if (!negative_x && !reciprocal &&
        (exponent < -66 || (exponent == -66 && numerator <= denominator))) {
        /* atan(r) is strictly below r. Only an exact dyadic ratio can land
         * on a binary80 rounding boundary: a non-dyadic ratio of these two
         * 64-bit mantissas is separated from it by far more than one retained
         * Q256 quantum. For r <= 2^-66, r^3/3 is also smaller than that
         * separation, including nearest-mode midpoints. Preserve the below-
         * boundary side without flushing r when its cubic falls below Q256. */
        if (!qa_x87_nonzero(rest)) {
            const qa_x87_uint one = {{ 1, 0, 0, 0, 0 }};
            ratio = qa_x87_subtract(ratio, one);
        }
        return qa_x87_pack(ratio, exponent - 256, negative, true, control, status);
    }
    ratio = qa_x87_shift(ratio, exponent);
    bool reduced = qa_x87_compare(ratio, qa_x87_shift(qa_x87_scale, -1)) > 0;
    if (reduced) {
        ratio = qa_x87_divide(qa_x87_multiply(
            qa_x87_subtract(qa_x87_scale, ratio), qa_x87_scale),
            qa_x87_add(qa_x87_scale, ratio), NULL);
    }
    angle = qa_x87_atan_series(ratio);
    if (reduced) angle = qa_x87_subtract(qa_x87_shift(qa_x87_pi, -2), angle);
    if (reciprocal) angle = qa_x87_subtract(qa_x87_shift(qa_x87_pi, -1), angle);
    if (negative_x) angle = qa_x87_subtract(qa_x87_pi, angle);
    return qa_x87_pack(angle, -256, negative, false, control, status);
}
