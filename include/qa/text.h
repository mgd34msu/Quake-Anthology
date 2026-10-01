#ifndef QA_TEXT_H
#define QA_TEXT_H

#include "qa/common.h"

/* Locale-independent Unicode lowercase for source-defined name matching.
 * Output is owned UTF-8 with a trailing NUL outside the counted size. */
bool qa_utf8_lower(qa_bytes input, qa_buffer *out, qa_error *error);
/* Text resources replace malformed UTF-8 subsequences with U+FFFD. The owned
 * output is NUL terminated; counted embedded NULs remain in the byte span. */
bool qa_utf8_repair(qa_bytes input, qa_buffer *out, qa_error *error);
/* Allocation-free scalar iteration, replacing malformed subsequences. */
bool qa_utf8_next(qa_bytes, size_t *cursor, uint32_t *scalar);
bool qa_utf8_valid(qa_bytes);
size_t qa_utf8_encode(uint32_t scalar, char out[4]);
bool qa_unicode_whitespace(uint32_t scalar);
/* Full-token conversion in the C numeric locale, independent of UI language.
 * Leading/trailing ASCII whitespace is accepted. The caller applies finite,
 * integral or domain bounds as needed. Output is unchanged on failure. */
bool qa_parse_number(qa_bytes input, double *out, qa_error *error);
/* C-locale atof prefix conversion of a NUL-terminated byte string. No numeric
 * prefix produces zero; overflow/underflow and nonfinite values are preserved.
 * False reports an argument/resource failure, never a rejected numeric token. */
bool qa_parse_atof(const char *text, double *out, qa_error *error);
/* The same prefix conversion rounded directly to binary32. */
bool qa_parse_atof_float(const char *text, float *out, qa_error *error);
/* Finite double serialization in the same C locale, with round-trip precision. */
bool qa_format_number(double value, char out[32], qa_error *error);
/* ECMAScript Number/String spelling: shortest round-tripping binary64,
 * closest decimal with even ties, fixed/scientific thresholds, and -0 as 0. */
bool qa_format_ecmascript_number(double value, char out[32], qa_error *error);
/* ECMAScript StringNumericLiteral conversion, including Unicode trim,
 * unsigned binary/octal/hex integers and signed decimal/Infinity. Empty
 * trimmed text produces zero. Invalid grammar fails without changing output;
 * callers separately qualify nonempty, finite and domain requirements. */
bool qa_parse_ecmascript_number(qa_bytes, double *, qa_error *);
/* Fixed decimal C-locale formatting, rounding ties to even. Nonfinite values
 * use nan/inf/-inf. Capacity includes NUL; failure empties a valid output. */
bool qa_format_fixed(double value, unsigned digits, char *out, size_t capacity, qa_error *error);

#endif
