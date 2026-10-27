/*
 * Port of src/core/renderer-math.ts: the renderer sine table and
 * Q_rsqrt. The sine table is built on first use with the same staged
 * binary32 computation. The index takes double so the TS safe-integer
 * and int32 checks keep their exact RangeError.
 * GPL-2.0-or-later.
 */
#ifndef QA_CORE_RENDERER_MATH_H
#define QA_CORE_RENDERER_MATH_H

#include "qa/contracts/math.h"

/* tr.sinTable[index & 1023]; index must be a signed 32-bit integer. */
float qa_renderer_sine(double index);
/* q_math.c Q_rsqrt with one Newton refinement. */
float qa_inverse_sqrt32(double square);
/* q_math.c VectorNormalizeFast. */
qa_vec3 qa_normalize_fast3(qa_vec3 value);

#endif
