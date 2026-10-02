#ifndef QA_SOURCE_NUMBER_H
#define QA_SOURCE_NUMBER_H

#include <float.h>
#include <stdint.h>
#include <string.h>

/* ECMAScript Math.fround, including ties, overflow and signed zero. */
static inline double qa_source_fround(double value) {
    _Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
        "Source publication requires binary32 floats");
    _Static_assert(sizeof(double) == 8 && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024,
        "Source numbers require binary64 doubles");
    uint64_t raw;
    memcpy(&raw, &value, sizeof(raw));
    uint32_t bits = (uint32_t)(raw >> 32) & UINT32_C(0x80000000);
    uint32_t exponent = (uint32_t)(raw >> 52) & 2047u;
    uint64_t fraction = raw & UINT64_C(0xfffffffffffff);
    if (exponent == 2047u) {
        bits |= fraction ? UINT32_C(0x7fc00000) : UINT32_C(0x7f800000);
    } else if (exponent) {
        int power = (int)exponent - 1023;
        if (power > 127) bits |= UINT32_C(0x7f800000);
        else if (power >= -150) {
            uint64_t mantissa = fraction | (UINT64_C(1) << 52);
            unsigned shift = power >= -126 ? 29u : (unsigned)(-power - 97);
            uint64_t rounded = mantissa >> shift;
            uint64_t remainder = mantissa & ((UINT64_C(1) << shift) - 1);
            uint64_t half = UINT64_C(1) << (shift - 1);
            if (remainder > half || (remainder == half && (rounded & 1u))) ++rounded;
            if (power >= -126) {
                if (rounded == (UINT64_C(1) << 24)) { rounded >>= 1; ++power; }
                if (power > 127) bits |= UINT32_C(0x7f800000);
                else bits |= (uint32_t)(power + 127) << 23 |
                    ((uint32_t)rounded & UINT32_C(0x7fffff));
            } else bits |= (uint32_t)rounded;
        }
    }
    float result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

#endif
