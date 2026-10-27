/* Port of src/network/q2/constants.ts. GPL-2.0-or-later. */
#include "qa/network/q2/constants.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

noreturn void qa_q2_com_error_throw(int code, const char *format, ...) {
  (void)code; /* The code travels on the TS error object; abort keeps none. */
  va_list args;
  va_start(args, format);
  fputs("ComError: ", stderr);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
}

noreturn void qa_q2_throw_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fputs("Error: ", stderr);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
}
