/*
 * Number.prototype.toExponential and toPrecision, beside qa_js_to_fixed in
 * include/qa/foundation/js.h. printf("%.*e") rounds exact decimal ties to even and
 * pads the exponent to two digits; JS picks the larger of two equally near results
 * and prints the exponent unpadded ("1.5e+5"). Expected values in
 * tests/foundation/test_js_parse_extra.c come from bun. GPL-2.0-or-later.
 */
#ifndef QA_FOUNDATION_JS_PARSE_EXTRA_H
#define QA_FOUNDATION_JS_PARSE_EXTRA_H

#include <stddef.h>

/* Longest result: sign, 101 digits, point, "e-324", NUL. */
#define QA_JS_TO_PRECISION_CAPACITY 128

/* value.toExponential(digits), digits in 0..100 (RangeError otherwise); returns the length. */
size_t qa_js_to_exponential(double value, int digits, char *buffer, size_t capacity);
/* value.toPrecision(precision), precision in 1..100 (RangeError otherwise); returns the length. */
size_t qa_js_to_precision(double value, int precision, char *buffer, size_t capacity);

#endif
