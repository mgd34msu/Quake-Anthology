/* See include/qa/foundation/support.h. GPL-2.0-or-later. */
#include "qa/foundation/support.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

noreturn void qa_range_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fputs("RangeError: ", stderr);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
}
