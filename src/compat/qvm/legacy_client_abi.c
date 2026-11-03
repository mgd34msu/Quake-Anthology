/* See include/qa/compat/qvm/legacy_client_abi.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/legacy_client_abi.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/foundation/js.h"

static char *copy_text(const char *text) {
  size_t length = strlen(text);
  char *result = malloc(length + 1);
  if (result == NULL) qa_range_error("QVM memory allocation failed");
  memcpy(result, text, length + 1);
  return result;
}

char **qa_qvm_legacy_client_command(const char *const *argv, size_t count, qa_qvm_abi_profile profile, size_t *out_count) {
  *out_count = count;
  char **result = malloc((count == 0 ? 1 : count) * sizeof *result);
  if (result == NULL) qa_range_error("QVM memory allocation failed");
  for (size_t i = 0; i < count; i++) result[i] = copy_text(argv[i]);
  if ((profile != NULL && strcmp(profile, "q3-modern") == 0) || count == 0 || strcmp(argv[0], "cs") != 0)
    return result;
  /* TS Number(argv[1]): missing element is Number(undefined) == NaN.
   * Number() trims string whitespace, maps an empty/blank string to 0,
   * accepts 0x/0b/0o integer literals, and requires the whole string. */
  /* Number(argv[1]); strtod also took "inf", "nan" and "0x1p3". */
  double index_value = count > 1 ? qa_js_string_to_number(argv[1]) : NAN;
  /* TS !Number.isInteger(index): null. */
  if (index_value != trunc(index_value) || !isfinite(index_value)) {
    qa_qvm_legacy_client_command_free(result, count);
    *out_count = 0;
    return NULL;
  }
  /* Compare in double: huge integers (1e30) are valid and unmapped in
   * TS; casting them to long long first would be undefined. */
  if (index_value < 12 || index_value > 26) return result;
  long long index = (long long)index_value;
  if (index >= 20 && index <= 23) {
    char buffer[32];
    snprintf(buffer, sizeof buffer, "%lld", index - 8);
    free(result[1]);
    result[1] = copy_text(buffer);
    return result;
  }
  /* TS: an unmapped legacy configstring index is null. */
  if (index >= 12 && index <= 26) {
    qa_qvm_legacy_client_command_free(result, count);
    *out_count = 0;
    return NULL;
  }
  return result;
}

void qa_qvm_legacy_client_command_free(char **argv, size_t count) {
  if (argv == NULL) return;
  for (size_t i = 0; i < count; i++) free(argv[i]);
  free(argv);
}
