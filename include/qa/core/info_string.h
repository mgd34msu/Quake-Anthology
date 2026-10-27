/*
 * Port of src/core/info-string.ts: Info_Print from id Software's
 * qcommon/common.c and Info_ValueForKey from code/game/q_shared.c.
 * C accommodations: strings are NUL-terminated bytes, so the TS
 * truncation at an embedded "\0" and its charCode > 255 checks are
 * inherent; lengths are byte counts. Lookup results are malloc'd
 * copies (caller frees), NULL only on allocation failure; the TS
 * returns string slices.
 * GPL-2.0-or-later.
 */
#ifndef QA_CORE_INFO_STRING_H
#define QA_CORE_INFO_STRING_H

#include <stdint.h>

typedef void (*qa_print_info_fn)(const char *text, void *ctx);

/* Emits padded keys and values through `print`, piece by piece. */
void qa_print_info(const char *text, qa_print_info_fn print, void *ctx);

/* First ASCII-case-insensitive key match, or a malloc'd "" when absent.
 * `maximum_length` mirrors the TS bound (1..8192, default 8192 in the
 * unbounded-name wrapper below); an input at or beyond it raises the
 * TS CommonError("drop", ...) via qa_common_error. */
char *qa_info_value_for_key_bounded(const char *input, const char *wanted,
                                    int64_t maximum_length);
char *qa_info_value_for_key(const char *input, const char *wanted);

#endif
