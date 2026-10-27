/*
 * Port of src/guest/floating-point/trigonometric.ts: x87 FSIN/FCOS/FPATAN
 * evaluated with integer guard bits over Intel's 68-bit pi approximation.
 * GPL-2.0-or-later.
 */
#ifndef QA_GUEST_FLOATING_POINT_TRIGONOMETRIC_H
#define QA_GUEST_FLOATING_POINT_TRIGONOMETRIC_H

#include "qa/guest/floating_point/binary.h"

typedef enum qa_x87_trig_kind { QA_X87_TRIG_OUT_OF_RANGE = 0, QA_X87_TRIG_RESULT } qa_x87_trig_kind;
typedef struct qa_x87_trig_result {
  qa_x87_trig_kind kind;
  qa_binary_result result; /* valid when kind == RESULT; free with qa_binary_result_free */
} qa_x87_trig_result;

void qa_x87_trigonometric(qa_x87_trig_result *out, const qa_binary_value *value, bool cosine, qa_fp_rounding mode);
void qa_x87_arctangent(qa_binary_result *out, const qa_binary_value *y, const qa_binary_value *x, qa_fp_rounding mode);

#endif
