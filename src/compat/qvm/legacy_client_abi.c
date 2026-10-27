/* See include/qa/compat/qvm/legacy_client_abi.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/legacy_client_abi.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

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
  /* TS Number(argv[1]): missing element is Number(undefined) == NaN. */
  double index_value = count > 1 ? strtod(argv[1], NULL) : NAN;
  if (count > 1) {
    /* strtod accepts prefixes; Number() requires the whole string. */
    char *end = NULL;
    index_value = strtod(argv[1], &end);
    if (end == argv[1] || *end != '\0') index_value = NAN;
    if (strcmp(argv[1], "") == 0) index_value = 0.0; /* Number("") === 0 */
  }
  if (index_value != trunc(index_value) || !isfinite(index_value)) {
    qa_qvm_legacy_client_command_free(result, count);
    qa_range_error("Invalid configstring command index");
  }
  long long index = (long long)index_value;
  if (index >= 20 && index <= 23) {
    char buffer[32];
    snprintf(buffer, sizeof buffer, "%lld", index - 8);
    free(result[1]);
    result[1] = copy_text(buffer);
    return result;
  }
  if (index >= 12 && index <= 26) {
    qa_qvm_legacy_client_command_free(result, count);
    qa_range_error("Configstring %lld has no legacy client mapping", index);
  }
  return result;
}

void qa_qvm_legacy_client_command_free(char **argv, size_t count) {
  if (argv == NULL) return;
  for (size_t i = 0; i < count; i++) free(argv[i]);
  free(argv);
}
