/* ECMAScript Number::toString for text and JSON that travels on the wire.
 * GPL-2.0-or-later. */
#ifndef QA_NETWORK_COMMON_JS_NUMBER_H
#define QA_NETWORK_COMMON_JS_NUMBER_H
#include <stddef.h>
/* Writes `value` as String(value) does, NUL-terminated; 40 bytes always suffices. */
void qa_net_js_number(double value, char *buffer, size_t capacity);
/* As JSON.stringify: -0 is "0", NaN and infinities are "null". */
void qa_net_json_number(double value, char *buffer, size_t capacity);
/* Writes String(value) into `buffer` (40 bytes) and returns it. */
const char *qa_net_js_number_text(double value, char *buffer);
/* String(value) for one printf "%s" argument, in place of "%g": the text lives
 * in a compound literal for the rest of the enclosing block. */
#define QA_JS_NUMBER(value) qa_net_js_number_text((value), (char[40]){0})
#endif
