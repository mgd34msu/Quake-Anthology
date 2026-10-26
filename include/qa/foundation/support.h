/*
 * C-only support for the port: TypeScript `throw new RangeError(msg)` on
 * invalid arguments becomes qa_range_error(), which prints the same
 * message to stderr and aborts. There is no corresponding .ts file.
 * GPL-2.0-or-later.
 */
#ifndef QA_FOUNDATION_SUPPORT_H
#define QA_FOUNDATION_SUPPORT_H

#include <stdnoreturn.h>

/** Largest integer a TypeScript `number` holds exactly (2^53 - 1). */
#define QA_MAX_SAFE_INTEGER 9007199254740991LL

noreturn void qa_range_error(const char *format, ...);

#endif
