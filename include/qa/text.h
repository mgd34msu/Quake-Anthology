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
/* C-locale strtod prefix receipt using the caller's rounding mode. The end
 * offset and range error are returned without changing the host errno. */
bool qa_parse_strtod(const char *, double *, size_t *consumed, bool *range_error, qa_error *);
/* The same prefix conversion rounded directly to binary32. */
bool qa_parse_atof_float(const char *text, float *out, qa_error *error);
/* Quake-style prefix grammar: optional minus, hex or quoted byte, otherwise
 * decimal digits scaled after the last dot. Whitespace, plus and exponents
 * are not consumed. NULL or a missing numeric prefix produces zero.
 * Accumulation/result stay binary64; callers retain their float conversion
 * and admission bounds. Flags preserve reached arithmetic and quoted bytes. */
typedef enum qa_quake_number_policy {
    QA_QUAKE_NUMBER_ASCII_UNSIGNED = 0, /* value * 10 + ascii - '0' */
    QA_QUAKE_NUMBER_DIGIT_FIRST = 1,    /* value * 10 + (ascii - '0') */
    QA_QUAKE_NUMBER_SIGNED_QUOTE = 2    /* Interpret quoted bytes as int8. */
} qa_quake_number_policy;
double qa_parse_quake_number(const char *, qa_quake_number_policy);
/* Original QC/x86 truncation, with INT32_MIN for nonfinite or out-of-range
 * binary32 operands. This is not a wrapping word conversion. */
int32_t qa_source_float_to_i32(float value);
/* Quake PF_ftos: integral values use %d; all others use %5.1f. */
bool qa_format_quake_float(float value, char out[64], qa_error *error);
/* Q3 bg_lib AddInt spelling, including its wrapped INT32_MIN digit bytes.
 * Output includes a trailing NUL; the returned length excludes it. */
size_t qa_format_q3_integer(int32_t value, char out[12]);
/* Finite double serialization in the same C locale, with round-trip precision. */
bool qa_format_number(double value, char out[32], qa_error *error);
/* Fixed decimal C-locale formatting, rounding ties to even. Nonfinite values
 * use nan/inf/-inf. Capacity includes NUL; failure empties a valid output. */
bool qa_format_fixed(double value, unsigned digits, char *out, size_t capacity, qa_error *error);

#endif
