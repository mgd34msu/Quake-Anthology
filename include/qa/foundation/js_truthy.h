/*
 * JavaScript truthiness of a number: `if (x)`, `!x`, `x ? a : b`, `x && y`. A C
 * floating condition is `x != 0`, which is true for NaN; JS NaN is falsy. There is
 * no corresponding .ts file. GPL-2.0-or-later.
 */
#ifndef QA_FOUNDATION_JS_TRUTHY_H
#define QA_FOUNDATION_JS_TRUTHY_H

#include <stdbool.h>

/** Boolean(value) for a number: false for 0, -0 and NaN. */
static inline bool qa_js_truthy(double value) { return value < 0 || value > 0; }

#endif
