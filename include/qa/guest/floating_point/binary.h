/*
 * Port of src/guest/floating-point/binary.ts: exact IEEE binary32/64/80
 * values as sign + bigint coefficient + exponent. TS bigint becomes
 * qa_bigint (sign-magnitude, base 2^32, heap limbs); every qa_binary_value
 * owns its coefficient/payload and must be released with
 * qa_binary_value_free(). Division truncates toward zero and shifts follow
 * JS semantics, exactly as the TS bigint operators do.
 * The Numeric* types and fault channel come from
 * src/guest/floating-point/contracts.ts (not yet its own C module): the TS
 * NumericFault throw becomes a longjmp to the innermost handler installed
 * in the execution context, or an abort when no handler exists (mirrors an
 * uncaught throw reaching the top-level crash handler). GPL-2.0-or-later.
 */
#ifndef QA_GUEST_FLOATING_POINT_BINARY_H
#define QA_GUEST_FLOATING_POINT_BINARY_H

#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/guest/core/registers.h"

/* --- qa_bigint: JS bigint semantics --- */
typedef struct qa_bigint {
  int sign; /* -1, 0, +1 (0 iff len==0) */
  uint32_t *limbs; /* little-endian magnitude, normalized */
  size_t len;
  size_t cap;
} qa_bigint;

void qa_bigint_init(qa_bigint *v);
void qa_bigint_free(qa_bigint *v);
void qa_bigint_set_u64(qa_bigint *v, uint64_t x);
void qa_bigint_set_i64(qa_bigint *v, int64_t x);
/* Parses an unsigned hex magnitude (optional leading '-'). */
void qa_bigint_set_hex(qa_bigint *v, const char *hex);
void qa_bigint_clone(qa_bigint *dst, const qa_bigint *src);
bool qa_bigint_is_zero(const qa_bigint *v);
int qa_bigint_cmp(const qa_bigint *a, const qa_bigint *b);
int qa_bigint_cmp_abs(const qa_bigint *a, const qa_bigint *b);
size_t qa_bigint_bit_length(const qa_bigint *v); /* of magnitude; JS toString(2).length for >=0 */
bool qa_bigint_test_bit(const qa_bigint *v, size_t bit);
/* Low 64 bits (JS BigInt.asUintN(64, v)) / sign-extended (asIntN). */
uint64_t qa_bigint_as_uint_n(const qa_bigint *v, int width);
int64_t qa_bigint_as_int_n(const qa_bigint *v, int width);
void qa_bigint_neg(qa_bigint *out, const qa_bigint *a);
void qa_bigint_add(qa_bigint *out, const qa_bigint *a, const qa_bigint *b);
void qa_bigint_sub(qa_bigint *out, const qa_bigint *a, const qa_bigint *b);
void qa_bigint_mul(qa_bigint *out, const qa_bigint *a, const qa_bigint *b);
/* Truncating divmod (JS / and %); q or r may be NULL. Aborts on /0 (TS RangeError). */
void qa_bigint_divmod(qa_bigint *q, qa_bigint *r, const qa_bigint *a, const qa_bigint *b);
void qa_bigint_shl(qa_bigint *out, const qa_bigint *a, int64_t shift); /* negative shift = shr */
void qa_bigint_shr(qa_bigint *out, const qa_bigint *a, int64_t shift); /* arithmetic (floor) */
void qa_bigint_and(qa_bigint *out, const qa_bigint *a, const qa_bigint *b);
void qa_bigint_or(qa_bigint *out, const qa_bigint *a, const qa_bigint *b);
void qa_bigint_pow_u32(qa_bigint *out, uint32_t base, uint32_t exp);
/* Decimal/hex text of the magnitude with '-' prefix when negative.
 * Returns malloc'd string; caller frees. Mirrors bigint.toString(base). */
char *qa_bigint_to_string(const qa_bigint *v, int base);

/* --- binary.ts types --- */
typedef int qa_fp_sign; /* TS Sign = 0 | 1 */
typedef enum qa_fp_rounding {
  QA_FP_NEAREST = 0, QA_FP_DOWN, QA_FP_UP, QA_FP_ZERO
} qa_fp_rounding;
const char *qa_fp_rounding_name(qa_fp_rounding mode);

typedef enum qa_binary_kind {
  QA_BINARY_FINITE = 0, QA_BINARY_INFINITY, QA_BINARY_NAN, QA_BINARY_UNSUPPORTED
} qa_binary_kind;
const char *qa_binary_kind_name(qa_binary_kind kind);

typedef struct qa_binary_value {
  qa_binary_kind kind;
  qa_fp_sign sign;
  qa_bigint coefficient; /* finite only */
  int64_t exponent;      /* finite only */
  bool denormal;         /* finite only */
  qa_bigint payload;     /* nan only */
  bool signaling;        /* nan only */
} qa_binary_value;

typedef struct qa_binary_result {
  qa_binary_value value;
  int flags;
  bool rounded_up;
} qa_binary_result;

typedef struct qa_binary_format {
  int precision;
  int minimum_exponent;
  int maximum_exponent;
} qa_binary_format;

extern const qa_binary_format qa_binary32_format;
extern const qa_binary_format qa_binary64_format;
extern const qa_binary_format qa_binary80_format;

enum {
  QA_FP_INVALID = 1, QA_FP_DENORMAL_OPERAND = 2, QA_FP_ZERO_DIVIDE = 4,
  QA_FP_OVERFLOW = 8, QA_FP_UNDERFLOW = 16, QA_FP_PRECISION = 32
};

void qa_binary_value_free(qa_binary_value *v);
void qa_binary_value_clone(qa_binary_value *dst, const qa_binary_value *src);
void qa_binary_result_free(qa_binary_result *r);

qa_fp_rounding qa_fp_rounding_from_bits(int bits); /* TS rounding() */
qa_binary_format qa_fp_format_for(int width);      /* 32|64|80 */
qa_binary_value qa_fp_zero(qa_fp_sign sign);
qa_binary_value qa_fp_indefinite(void);
qa_binary_value qa_fp_from_integer(const qa_bigint *value);
qa_binary_value qa_fp_from_i64(int64_t value);
qa_binary_value qa_fp_negate(const qa_binary_value *value);
qa_binary_value qa_fp_quiet(const qa_binary_value *value);

/* readBits/writeBits: little-endian bytes <-> bigint (write truncates to
 * byte_length*8 bits, matching the DataView setters at every TS call site). */
void qa_fp_read_bits(qa_bigint *out, const uint8_t *bytes, size_t len);
void qa_fp_write_bits(uint8_t *out, const qa_bigint *value, size_t byte_length);

qa_binary_value qa_fp_decode_binary32(uint32_t bits);
qa_binary_value qa_fp_decode_binary(const qa_bigint *bits, int width);
void qa_fp_round_rational(qa_binary_result *out, qa_fp_sign sign, const qa_bigint *numerator,
                          const qa_bigint *denominator, int64_t exponent, qa_binary_format format,
                          qa_fp_rounding mode);
void qa_fp_convert_binary(qa_binary_result *out, const qa_binary_value *value, qa_binary_format format,
                          qa_fp_rounding mode);
void qa_fp_encode_binary(qa_bigint *out, const qa_binary_value *value, int width);

typedef enum qa_binary_operation { QA_FP_ADD = 0, QA_FP_SUBTRACT, QA_FP_MULTIPLY, QA_FP_DIVIDE } qa_binary_operation;
typedef enum qa_fp_nan_selection { QA_FP_NAN_X87 = 0, QA_FP_NAN_SSE } qa_fp_nan_selection;
void qa_fp_arithmetic(qa_binary_result *out, qa_binary_operation op, const qa_binary_value *left,
                      const qa_binary_value *right, qa_binary_format format, qa_fp_rounding mode,
                      qa_fp_nan_selection selection);

typedef enum qa_fp_comparison { QA_FP_LESS = 0, QA_FP_EQUAL, QA_FP_GREATER, QA_FP_UNORDERED } qa_fp_comparison;
const char *qa_fp_comparison_name(qa_fp_comparison c);
qa_fp_comparison qa_fp_compare_binary(const qa_binary_value *left, const qa_binary_value *right);

typedef struct qa_fp_integer_conversion { qa_bigint value; int flags; bool rounded_up; } qa_fp_integer_conversion;
void qa_fp_integer_conversion_apply(qa_fp_integer_conversion *out, const qa_binary_value *value, int width,
                                    qa_fp_rounding mode);
void qa_fp_round_integral(qa_binary_result *out, const qa_binary_value *value, qa_fp_rounding mode);
void qa_fp_square_root(qa_binary_result *out, const qa_binary_value *value, qa_binary_format format,
                       qa_fp_rounding mode);

/* --- contracts.ts: numeric execution --- */
typedef enum qa_numeric_operand_kind { QA_OPERAND_MEMORY = 0, QA_OPERAND_REGISTER } qa_numeric_operand_kind;
typedef struct qa_numeric_operand {
  qa_numeric_operand_kind kind;
  qa_guest_address address; /* memory */
  int index;                /* register */
} qa_numeric_operand;

typedef enum qa_numeric_prefix { QA_PREFIX_NONE = 0, QA_PREFIX_66, QA_PREFIX_F2, QA_PREFIX_F3 } qa_numeric_prefix;
const char *qa_numeric_prefix_name(qa_numeric_prefix prefix);

typedef struct qa_numeric_instruction {
  int opcode;
  int secondary_opcode; bool has_secondary_opcode;
  int modrm; bool has_modrm;
  qa_numeric_operand operand; bool has_operand;
  int register_index;
  qa_numeric_prefix prefix;
  int operand_bits; /* 16|32|64 */
  int immediate; bool has_immediate;
} qa_numeric_instruction;

typedef enum qa_numeric_result_kind {
  QA_NUMERIC_EXECUTED = 0, QA_NUMERIC_UNSUPPORTED, QA_NUMERIC_EXCEPTION
} qa_numeric_result_kind;
typedef struct qa_numeric_result {
  qa_numeric_result_kind kind;
  int vector;
  char detail[160];
} qa_numeric_result;

/* Throw channel: kind + jmp target installed by the innermost handler. */
enum { QA_THROW_NONE = 0, QA_THROW_FAULT = 1, QA_THROW_UNSUPPORTED = 2, QA_THROW_DEFERRED = 3 };
typedef struct qa_numeric_context {
  qa_guest_processor_state *state;
  qa_mapped_guest_memory *memory;
  qa_numeric_instruction instruction;
  jmp_buf *throw_buf;
  int throw_kind;
  int throw_vector;
  char throw_detail[160];
} qa_numeric_context;

void qa_numeric_context_init(qa_numeric_context *ctx, qa_guest_processor_state *state,
                             qa_mapped_guest_memory *memory, const qa_numeric_instruction *instruction);
/* TS `throw new NumericFault(vector, detail)`. */
void qa_numeric_fault(qa_numeric_context *ctx, int vector, const char *detail);
/* TS private UnsupportedX87/UnsupportedSse/DeferredX87Exception throws. */
void qa_numeric_throw_unsupported(qa_numeric_context *ctx, const char *detail);
void qa_numeric_throw_deferred(qa_numeric_context *ctx);
void qa_numeric_set_result(qa_numeric_result *out, qa_numeric_result_kind kind, int vector, const char *detail);

#endif
