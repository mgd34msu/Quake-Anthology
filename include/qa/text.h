#ifndef QA_TEXT_H
#define QA_TEXT_H

#include "qa/common.h"

/* Locale-independent Unicode lowercase for source-defined name matching.
 * Output is owned UTF-8 with a trailing NUL outside the counted size. */
bool qa_utf8_lower(qa_bytes input, qa_buffer *out, qa_error *error);
/* Text resources replace malformed UTF-8 subsequences with U+FFFD. The owned
 * output is NUL terminated; counted embedded NULs remain in the byte span. */
bool qa_utf8_repair(qa_bytes input, qa_buffer *out, qa_error *error);
/* Full-token conversion in the C numeric locale, independent of UI language.
 * Leading/trailing ASCII whitespace is accepted. The caller applies finite,
 * integral or domain bounds as needed. Output is unchanged on failure. */
bool qa_parse_number(qa_bytes input, double *out, qa_error *error);

#endif
