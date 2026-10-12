#ifndef QA_TEXT_H
#define QA_TEXT_H

#include "qa/common.h"
#include "qa/arena.h"

/* Locale-independent Unicode lowercase for source-defined name matching.
 * Output is owned UTF-8 with a trailing NUL outside the counted size. */
bool qa_utf8_lower(qa_bytes input, qa_buffer *out, qa_error *error);
/* Same conversion; output lives until the caller's arena is reset. */
bool qa_utf8_lower_arena(qa_bytes input, qa_arena *, qa_buffer *out, qa_error *error);
/* Text resources replace malformed UTF-8 subsequences with U+FFFD. The owned
 * output is NUL terminated; counted embedded NULs remain in the byte span. */
bool qa_utf8_repair(qa_bytes input, qa_buffer *out, qa_error *error);
/* Allocation-free scalar iteration, replacing malformed subsequences. */
bool qa_utf8_next(qa_bytes, size_t *cursor, uint32_t *scalar);
/* Strict scalar iteration. End or malformed UTF-8 leaves outputs unchanged. */
bool qa_utf8_next_strict(qa_bytes, size_t *cursor, uint32_t *scalar);
bool qa_utf8_valid(qa_bytes);
size_t qa_utf8_encode(uint32_t scalar, char out[4]);
/* Source strings use one byte per Latin-1 scalar. Capacity includes the
 * trailing NUL; size excludes it. Neither conversion allocates. */
bool qa_text_to_source_bytes(qa_bytes, uint8_t *out, size_t capacity, size_t *size, qa_error *);
bool qa_text_from_source_bytes(qa_bytes, char *out, size_t capacity, size_t *size, qa_error *);
bool qa_unicode_whitespace(uint32_t scalar);
bool qa_text_join_words(const char *const *,size_t,char *,size_t,qa_error *);
/* Counted bytes, including embedded NULs; folding changes ASCII A-Z only. */
static inline uint8_t qa_text_ascii_lower(uint8_t input_byte)
{
    return input_byte >= 'A' && input_byte <= 'Z' ? input_byte + ('a' - 'A') : input_byte;
}
int qa_text_bytes_compare(qa_bytes, qa_bytes, bool ascii_fold);
bool qa_text_bytes_equal(qa_bytes, qa_bytes, bool ascii_fold);
bool qa_text_bytes_prefix(qa_bytes text, qa_bytes prefix, bool ascii_fold);
int qa_text_ascii_compare(const char *, const char *);
bool qa_text_ascii_equal(const char *, const char *);
/* Q3 Q_IsColorString/ColorIndex after source-byte or UTF-8 scalar decoding.
 * A literal caret or end of text leaves the palette index unchanged. */
bool qa_text_q3_color_escape(uint32_t current, uint32_t next, uint8_t *index);
/* Quake backslash info pairs, borrowed from the NUL-terminated input. Cursor
 * starts at the string and ends at the next delimiter or terminator. */
bool qa_info_next(const char **cursor,qa_bytes *key,qa_bytes *value);
enum { QA_INFO_TRUNCATE = 4 };
/* Append raw counted pairs in order. Capacity includes the trailing NUL;
 * without TRUNCATE, insufficient space leaves output and length unchanged. */
bool qa_info_append(char *, size_t capacity, size_t *length,
    qa_bytes key, qa_bytes value, unsigned policy, qa_error *);
enum { QA_INFO_LAST=1, QA_INFO_CASE_FOLD=2 };
/* Exact byte keys by default; CASE_FOLD compares ASCII letters only. */
bool qa_info_key_equal(qa_bytes,qa_bytes,unsigned policy);
/* First match by default; missing keys return false and an empty span. */
bool qa_info_find(const char *,qa_bytes key,unsigned policy,qa_bytes *value);
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
 * The first three policies accumulate/result in binary64; callers retain
 * their float conversion and admission bounds. The Q3 policies select bg_lib
 * grammar and precision. Flags preserve reached arithmetic/quoted bytes. */
typedef enum qa_quake_number_policy {
    QA_QUAKE_NUMBER_ASCII_UNSIGNED = 0, /* value * 10 + ascii - '0' */
    QA_QUAKE_NUMBER_DIGIT_FIRST = 1,    /* value * 10 + (ascii - '0') */
    QA_QUAKE_NUMBER_SIGNED_QUOTE = 2,   /* Interpret quoted bytes as int8. */
    /* Q3 bg_lib atof: signed whitespace/plus/minus, decimal prefix, float
     * accumulator with double fractional terms; no hex, quotes or exponent. */
    QA_QUAKE_NUMBER_Q3_VM = 4,
    /* Original Q3 lcc bytecode uses four-byte float and double operations.
     * The same bg_lib grammar rounds each product, sum and fraction to float. */
    QA_QUAKE_NUMBER_Q3_BYTECODE = 8
} qa_quake_number_policy;
double qa_parse_quake_number(const char *, qa_quake_number_policy);
typedef struct qa_number_cursor {
    qa_bytes text;
    size_t offset;
    bool overrun;
} qa_number_cursor;
double qa_parse_quake_number_cursor(qa_number_cursor *, qa_quake_number_policy, bool scan);
/* Signed decimal prefix with the caller's original byte/overflow rules.
 * Missing prefixes produce zero; wrap is modulo 2^32 for every digit. */
typedef enum qa_decimal_rule {
    QA_DECIMAL_ASCII_CLAMP,
    QA_DECIMAL_ASCII_WRAP,
    QA_DECIMAL_CONTROL_WRAP,
    QA_DECIMAL_SIGNED_WRAP,
    QA_DECIMAL_LOCALE_CLAMP
} qa_decimal_rule;
int32_t qa_parse_decimal_i32(const char *, qa_decimal_rule);
int32_t qa_parse_decimal_i32_cursor(qa_number_cursor *, qa_decimal_rule, bool consume_delimiter);
/* Original x86 truncation, with INT32_MIN for nonfinite or out-of-range
 * operands. Float source arithmetic rounds before this boundary. */
int32_t qa_source_to_i32(double value);
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
