/*
 * Port of src/core/common-error.ts (CommonError): id Software's common.c
 * Com_Error control flow as a thrown Error carrying a code. C
 * accommodation: the thrown error has no stack to unwind, so raising one
 * prints "CommonError: <message>" (the TypeScript Error string form) to
 * stderr and aborts, like qa_range_error. The code string-literal union
 * becomes an enum with name/parse helpers returning the exact TS strings.
 * GPL-2.0-or-later.
 */
#ifndef QA_CORE_COMMON_ERROR_H
#define QA_CORE_COMMON_ERROR_H

#include <stdbool.h>
#include <stdnoreturn.h>

typedef enum qa_common_error_code {
  QA_COMMON_ERROR_SERVER_DISCONNECT,
  QA_COMMON_ERROR_DROP,
  QA_COMMON_ERROR_DISCONNECT,
  QA_COMMON_ERROR_NEED_CD,
  QA_COMMON_ERROR_FATAL,
} qa_common_error_code;

/* Returns the exact TypeScript CommonErrorCode string for a code. */
const char *qa_common_error_code_name(qa_common_error_code code);
/* Parses a TypeScript CommonErrorCode string; returns false if unknown. */
bool qa_common_error_code_parse(const char *text, qa_common_error_code *out);

/* Raises the port's CommonError: prints and aborts (see header note). */
noreturn void qa_common_error(qa_common_error_code code, const char *message);

#endif
