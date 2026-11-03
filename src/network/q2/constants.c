/* Port of src/network/q2/constants.ts. GPL-2.0-or-later. */
#include "qa/network/q2/constants.h"

#include <stdarg.h>
#include "qa/foundation/support.h"
#include <stdio.h>
#include <stdlib.h>

noreturn void qa_q2_com_error_throw(int code, const char *format, ...) {
  char text[1536];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  qa_raise("ComError", code, 0, "%s", text);
}

noreturn void qa_q2_throw_error(const char *format, ...) {
  char text[1536];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  qa_raise("Error", 0, 0, "%s", text);
}
