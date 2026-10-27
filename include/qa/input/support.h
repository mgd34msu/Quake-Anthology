/*
 * C-only support for the input port (no corresponding .ts file, in
 * the tradition of include/qa/foundation/support.h): the plain
 * TypeScript `throw new Error(msg)` abort, JavaScript number
 * formatting/parsing at the observable string boundaries, JS trim,
 * shared key/mouse queue callback shapes, and deep copies of the
 * contracts binding shapes (TypeScript retains the objects; C
 * stores own copies). Strings are NUL-terminated bytes; the JS
 * helpers treat each byte as one UTF-16 code unit, so non-ASCII
 * text can differ from the TypeScript (see docs/PORTING.md).
 * GPL-2.0-or-later.
 */
#ifndef QA_INPUT_SUPPORT_H
#define QA_INPUT_SUPPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>

#include "qa/contracts/ui.h"

/** Plain TS Error: prints "Error: <message>" and aborts. */
noreturn void qa_input_error(const char *format, ...);

/* Malloc'd copy; NULL only on allocation failure. */
char *qa_input_dup(const char *text);
/* Malloc'd copy of the first length bytes, NUL-terminated. */
char *qa_input_dup_range(const char *text, size_t length);

/** TypeScript ToInt32 (the `| 0` and bitwise-operand conversion). */
int32_t qa_input_to_int32(double value);

/* Malloc'd String(number): shortest round-trip decimal, "NaN",
 * "Infinity"/"-Infinity", both zeroes as "0". NULL only on OOM. */
char *qa_input_js_number(double value);
/** TypeScript Number(text) over byte text. */
double qa_input_js_parse_number(const char *text);
/** TypeScript Number.parseInt(text, 10). */
double qa_input_js_parse_int10(const char *text);

/** JS `\s` / String.prototype.trim whitespace, byte values only. */
bool qa_input_js_space(unsigned char byte);
/* Malloc'd trimmed copy (String.prototype.trim). NULL only on OOM. */
char *qa_input_js_trim(const char *text);

/** TS (key, down, time) => undefined queue callbacks. */
typedef void (*qa_queue_key_fn)(void *ctx, double key, bool down, double time);
typedef void (*qa_queue_mouse_fn)(void *ctx, double dx, double dy, double time);

/* Deep copy with owned strings. False only on allocation failure
 * (out is left zeroed). */
bool qa_input_binding_copy(qa_input_binding *out, const qa_input_binding *in);
void qa_input_binding_dispose(qa_input_binding *binding);

#endif
