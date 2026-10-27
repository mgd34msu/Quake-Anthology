/*
 * Port of src/guest/floating-point/sse.ts: SSE/SSE2 numeric execution.
 * Also hosts qa_execute_numeric_instruction, mirroring the dispatcher in
 * src/guest/floating-point/index.ts (barrels are not ported this wave).
 * TS UnsupportedSse/NumericFault throws ride the context throw channel.
 * GPL-2.0-or-later.
 */
#ifndef QA_GUEST_FLOATING_POINT_SSE_H
#define QA_GUEST_FLOATING_POINT_SSE_H

#include "qa/guest/floating_point/binary.h"

void qa_execute_sse(qa_numeric_result *out, qa_numeric_context *ctx);
/* floating-point/index.ts executeNumericInstruction. */
void qa_execute_numeric_instruction(qa_numeric_result *out, qa_numeric_context *ctx);

#endif
