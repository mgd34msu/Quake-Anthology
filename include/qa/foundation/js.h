/*
 * JavaScript number semantics where C library functions and operators differ
 * from the operation the TypeScript runs. There is no corresponding .ts file:
 * these are the JS built-ins themselves, matched against Bun (JavaScriptCore),
 * which runs the TS oracle. tests/foundation/test_js_semantics.c pins every
 * helper to values printed by the JS expression under bun.
 *
 *  - qa_js_min/qa_js_max: Math.min/Math.max of two numbers. fmin/fmax drop NaN
 *    and leave the order of -0 and +0 open; JS returns NaN and orders -0 < +0.
 *  - qa_js_round: Math.round, ties toward +Infinity and -0 kept for [-0.5, -0];
 *    round()/lround() round ties away from zero, and floor(x + 0.5) is wrong at
 *    0.49999999999999994 and above 2^52.
 *  - qa_js_sign: Math.sign, NaN and -0 preserved.
 *  - qa_js_to_int32/uint32/int16/uint16/int8/uint8: ToInt32 etc., the `| 0`,
 *    `>>> 0`, shift and bitwise-operand conversion and typed-array stores: NaN
 *    and infinities become 0, values wrap modulo 2^N. A C cast of an
 *    out-of-range double is undefined behavior.
 *  - qa_js_to_uint8_clamp: Uint8ClampedArray store (round half to even).
 *  - qa_js_pow: Math.pow and `**` as JavaScriptCore computes them. Integer
 *    exponents 0..1000 use repeated squaring, and +-0.5 use sqrt, so results
 *    differ from glibc pow() in the last bit in most integer-exponent cases.
 *  - qa_js_hypot*: Math.hypot as JavaScriptCore computes it per arity: glibc
 *    hypot for two arguments, a scaled sum for three, a running-maximum fma sum
 *    for four or more. sqrt(x*x + y*y + z*z) and nested hypot() differ.
 *  - qa_js_parse_int/parse_float/string_to_number: parseInt, parseFloat and
 *    Number(string); strtol/strtod/atof accept "inf", "nan", hex floats, a sign
 *    before 0x, and differ on the empty string.
 *  - qa_js_to_fixed: Number.prototype.toFixed; printf("%.*f") rounds exact
 *    ties to even (0.5 -> "0") and prints "-0.00" and "nan".
 *  - qa_js_stable_sort/qa_js_sort: Array.prototype.sort is stable; qsort is not.
 *
 * Text is NUL-terminated UTF-8. JS white space (trim, parseInt, parseFloat,
 * Number) is ASCII TAB LF VT FF CR SP plus the UTF-8 forms of U+00A0, U+1680,
 * U+2000-U+200A, U+2028, U+2029, U+202F, U+205F, U+3000 and U+FEFF.
 * GPL-2.0-or-later.
 */
#ifndef QA_FOUNDATION_JS_H
#define QA_FOUNDATION_JS_H

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Math.min(a, b): NaN if either is NaN; -0 orders below +0. */
static inline double qa_js_min(double a, double b) {
  if (isnan(a) || isnan(b)) return NAN;
  if (a == b) return signbit(a) ? a : b;
  return a < b ? a : b;
}

/** Math.max(a, b): NaN if either is NaN; +0 orders above -0. */
static inline double qa_js_max(double a, double b) {
  if (isnan(a) || isnan(b)) return NAN;
  if (a == b) return signbit(a) ? b : a;
  return a > b ? a : b;
}

/** Math.min over binary32 operands; the result is exact in float. */
static inline float qa_js_minf(float a, float b) { return (float)qa_js_min(a, b); }
/** Math.max over binary32 operands; the result is exact in float. */
static inline float qa_js_maxf(float a, float b) { return (float)qa_js_max(a, b); }

/** Math.round: nearest integer, ties toward +Infinity, -0 for [-0.5, -0]. */
static inline double qa_js_round(double value) {
  if (!(fabs(value) < 4503599627370496.0)) return value; /* NaN, +-Inf, integers */
  double result = floor(value);
  if (value - result >= 0.5) result += 1.0;
  if (result == 0.0 && signbit(value)) return -0.0;
  return result;
}

/** Math.sign: NaN, -0 and +0 are returned unchanged. */
static inline double qa_js_sign(double value) {
  if (value > 0) return 1.0;
  if (value < 0) return -1.0;
  return value;
}

/** ToUint32: NaN and infinities to 0, truncate toward zero, modulo 2^32. */
static inline uint32_t qa_js_to_uint32(double value) {
  if (value >= 0.0 && value < 4294967296.0) return (uint32_t)value;
  if (!isfinite(value)) return 0;
  double magnitude = fmod(fabs(trunc(value)), 4294967296.0);
  uint32_t bits = (uint32_t)magnitude;
  return value < 0 ? (uint32_t)(0u - bits) : bits;
}

/** ToInt32: the `x | 0` conversion; NaN and infinities to 0, wraps modulo 2^32. */
static inline int32_t qa_js_to_int32(double value) {
  if (value > -2147483649.0 && value < 2147483648.0) return (int32_t)value;
  uint32_t bits = qa_js_to_uint32(value);
  return bits >= 0x80000000u ? (int32_t)(bits - 0x80000000u) - 0x7fffffff - 1 : (int32_t)bits;
}

/** ToUint16 (Uint16Array store). */
static inline uint16_t qa_js_to_uint16(double value) { return (uint16_t)qa_js_to_uint32(value); }
/** ToInt16 (Int16Array store). */
static inline int16_t qa_js_to_int16(double value) {
  uint16_t bits = qa_js_to_uint16(value);
  return bits >= 0x8000u ? (int16_t)((int32_t)bits - 0x10000) : (int16_t)bits;
}
/** ToUint8 (Uint8Array store). */
static inline uint8_t qa_js_to_uint8(double value) { return (uint8_t)qa_js_to_uint32(value); }
/** ToInt8 (Int8Array store). */
static inline int8_t qa_js_to_int8(double value) {
  uint8_t bits = qa_js_to_uint8(value);
  return bits >= 0x80u ? (int8_t)((int32_t)bits - 0x100) : (int8_t)bits;
}
/** ToUint8Clamp (Uint8ClampedArray store): NaN to 0, round half to even. */
static inline uint8_t qa_js_to_uint8_clamp(double value) {
  if (!(value > 0)) return 0;
  if (value >= 255) return 255;
  double f = floor(value);
  if (f + 0.5 < value) return (uint8_t)(f + 1);
  if (value < f + 0.5) return (uint8_t)f;
  return ((int)f % 2 == 1) ? (uint8_t)(f + 1) : (uint8_t)f;
}

/*
 * JS bitwise operators on numbers: both operands pass through ToInt32 (the shift
 * count through ToUint32, masked to 5 bits) and the result is a signed 32-bit
 * value, except `>>>`, which is unsigned. A C operator on int64_t or on a cast
 * double keeps bits above 2^31 and never turns bit 31 into a sign.
 */
static inline double qa_js_bit_and(double a, double b) { return (double)(qa_js_to_int32(a) & qa_js_to_int32(b)); }
static inline double qa_js_bit_or(double a, double b) { return (double)(qa_js_to_int32(a) | qa_js_to_int32(b)); }
static inline double qa_js_bit_xor(double a, double b) { return (double)(qa_js_to_int32(a) ^ qa_js_to_int32(b)); }
static inline double qa_js_bit_not(double a) { return (double)~qa_js_to_int32(a); }
/** a << b */
static inline double qa_js_shl(double a, double b) {
  uint32_t bits = qa_js_to_uint32(a) << (qa_js_to_uint32(b) & 31u);
  return (double)(bits >= 0x80000000u ? (int32_t)(bits - 0x80000000u) - 0x7fffffff - 1 : (int32_t)bits);
}
/** a >> b (sign-propagating) */
static inline double qa_js_shr(double a, double b) {
  int32_t value = qa_js_to_int32(a);
  uint32_t count = qa_js_to_uint32(b) & 31u;
  return (double)(value < 0 ? ~(~value >> count) : value >> count);
}
/** a >>> b */
static inline double qa_js_ushr(double a, double b) {
  return (double)(qa_js_to_uint32(a) >> (qa_js_to_uint32(b) & 31u));
}

/** Math.pow(base, exponent) and `base ** exponent` as JavaScriptCore computes them. */
double qa_js_pow(double base, double exponent);

/** Math.hypot(a, b). */
double qa_js_hypot2(double a, double b);
/** Math.hypot(a, b, c). */
double qa_js_hypot3(double a, double b, double c);
/** Math.hypot(...values); count 0 gives 0. */
double qa_js_hypot(size_t count, const double *values);

/** JS StrWhiteSpaceChar at `text`: the byte length of the character, or 0. */
size_t qa_js_white_space_length(const char *text);

/** parseInt(text, radix); radix 0 means undefined (10, or 16 after "0x"). */
double qa_js_parse_int(const char *text, int radix);
/** parseFloat(text). */
double qa_js_parse_float(const char *text);
/** Number(text) for a string operand (also unary +text). */
double qa_js_string_to_number(const char *text);

/* Largest qa_js_to_fixed text (sign, 21 integer digits, point, 100 digits, NUL). */
#define QA_JS_TO_FIXED_CAPACITY 128
/*
 * value.toFixed(digits) into `buffer`, NUL-terminated; returns the length.
 * `digits` must lie in 0..100 (TS RangeError otherwise). Values with magnitude
 * of 1e21 or more, NaN and infinities print as String(value).
 */
size_t qa_js_to_fixed(double value, int digits, char *buffer, size_t capacity);

/** Array.prototype.sort comparator over two elements, with a caller context. */
typedef int (*qa_js_compare_fn)(const void *a, const void *b, void *ctx);
/** Stable sort, as Array.prototype.sort with a consistent comparator. */
void qa_js_stable_sort(void *base, size_t count, size_t size, qa_js_compare_fn compare, void *ctx);
/** Stable drop-in for qsort(base, count, size, compare). */
void qa_js_sort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *));

#endif
