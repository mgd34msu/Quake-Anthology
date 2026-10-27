/* Port of src/content/q2/rerelease/debug-shapes.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_RERELEASE_DEBUG_SHAPES_H
#define QA_CONTENT_Q2_RERELEASE_DEBUG_SHAPES_H
#include <stdint.h>
#include "qa/content/q2/rerelease/types.h"
#include "qa/debug/shapes.h"
/* TS rereleaseDebugLifetime: >>>0 wrap; RangeError on non-finite. */
double qa_q2_rerelease_debug_lifetime(double seconds);
/* Fills event lines into caller buffer; returns total line count. */
size_t qa_q2_debug_shape(qa_debug_shape shape, qa_vec4 color, double lifetime_seconds,
                         bool depth_test, qa_q2_rerelease_event *out,
                         qa_debug_line *lines, size_t capacity);
#endif
