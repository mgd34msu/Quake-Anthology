/* Port of src/core/common-error.ts. GPL-2.0-or-later. */
#include "qa/core/common_error.h"
#include "qa/foundation/support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const code_names[] = {
    "server-disconnect", "drop", "disconnect", "need-cd", "fatal",
};

const char *qa_common_error_code_name(qa_common_error_code code) {
  return code_names[code];
}

bool qa_common_error_code_parse(const char *text, qa_common_error_code *out) {
  for (size_t i = 0; i < sizeof code_names / sizeof code_names[0]; i++) {
    if (strcmp(text, code_names[i]) == 0) {
      *out = (qa_common_error_code)i;
      return true;
    }
  }
  return false;
}

noreturn void qa_common_error(qa_common_error_code code, const char *message) {
  qa_raise("CommonError", (int)code, 0, "%s", message);
}
