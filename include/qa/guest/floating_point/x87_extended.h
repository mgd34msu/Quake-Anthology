/*
 * Port of src/guest/floating-point/x87-extended.ts: F2XM1, FYL2X, FYL2XP1,
 * FSCALE, FPREM/FPREM1, FXTRACT, FSINCOS and FPTAN over the exact binary.h
 * values, evaluated in fixed point with integer guard bits (no binary64
 * operation participates). Every result owns its value; free it with
 * qa_binary_result_free / qa_binary_value_free. GPL-2.0-or-later.
 */
#ifndef QA_GUEST_FLOATING_POINT_X87_EXTENDED_H
#define QA_GUEST_FLOATING_POINT_X87_EXTENDED_H

#include "qa/guest/floating_point/binary.h"

/* FSCALE: ST(0) * 2^trunc(ST(1)). */
void qa_x87_scale(qa_binary_result *out, const qa_binary_value *value, const qa_binary_value *exponent,
                  qa_fp_rounding mode);

typedef struct qa_x87_remainder_result {
  qa_binary_result result;
  bool complete;
  int quotient;
} qa_x87_remainder_result;
/* FPREM (truncating) and FPREM1 (IEEE nearest-even); an exponent gap of 64 or
 * more reduces by 32 bits per execution. */
void qa_x87_remainder(qa_x87_remainder_result *out, const qa_binary_value *dividend, const qa_binary_value *divisor,
                      bool ieee, qa_fp_rounding mode);

/* F2XM1: 2^x - 1. */
void qa_x87_two_power_minus_one(qa_binary_result *out, const qa_binary_value *value, qa_fp_rounding mode);
/* FYL2X: ST(1) * log2(ST(0)). */
void qa_x87_log2(qa_binary_result *out, const qa_binary_value *y, const qa_binary_value *x, qa_fp_rounding mode);
/* FYL2XP1: ST(1) * log2(ST(0) + 1). */
void qa_x87_log2_plus_one(qa_binary_result *out, const qa_binary_value *y, const qa_binary_value *x,
                          qa_fp_rounding mode);

typedef struct qa_x87_extract_result {
  qa_binary_value exponent;
  qa_binary_value significand;
  int flags;
} qa_x87_extract_result;
/* FXTRACT: unbiased exponent and significand in [1, 2). */
void qa_x87_extract(qa_x87_extract_result *out, const qa_binary_value *value);

/* FSINCOS and FPTAN share one reduction; false means the operand is beyond 2^63. */
bool qa_x87_sine_cosine(qa_binary_result *sine, qa_binary_result *cosine, const qa_binary_value *value,
                        qa_fp_rounding mode);
bool qa_x87_tangent(qa_binary_result *out, const qa_binary_value *value, qa_fp_rounding mode);

#endif
