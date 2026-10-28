/* See include/qa/compat/qvm/legacy_client_abi.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/legacy_client_abi.h"

#include <math.h>
#include <stdbool.h>
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
  /* TS Number(argv[1]): missing element is Number(undefined) == NaN.
   * Number() trims string whitespace, maps an empty/blank string to 0,
   * accepts 0x/0b/0o integer literals, and requires the whole string. */
  double index_value = NAN;
  if (count > 1) {
    const char *text = argv[1];
    while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\v' || *text == '\f' || *text == '\r') text++;
    size_t trimmed = strlen(text);
    while (trimmed > 0 && (text[trimmed - 1] == ' ' || text[trimmed - 1] == '\t' || text[trimmed - 1] == '\n' ||
                           text[trimmed - 1] == '\v' || text[trimmed - 1] == '\f' || text[trimmed - 1] == '\r'))
      trimmed--;
    if (trimmed == 0) {
      index_value = 0.0; /* Number("") === 0 */
    } else if (trimmed > 2 && text[0] == '0' &&
               (text[1] == 'x' || text[1] == 'X' || text[1] == 'b' || text[1] == 'B' || text[1] == 'o' || text[1] == 'O')) {
      int base = text[1] == 'x' || text[1] == 'X' ? 16 : text[1] == 'b' || text[1] == 'B' ? 2 : 8;
      double value = 0;
      bool valid = true;
      for (size_t i = 2; i < trimmed; i++) {
        char c = text[i];
        int digit = c >= '0' && c <= '9' ? c - '0'
                    : c >= 'a' && c <= 'f' ? c - 'a' + 10
                    : c >= 'A' && c <= 'F' ? c - 'A' + 10 : base;
        if (digit >= base) { valid = false; break; }
        value = value * base + digit;
      }
      index_value = valid ? value : NAN;
    } else if (trimmed > 3 && (text[0] == '+' || text[0] == '-') && text[1] == '0' &&
               (text[2] == 'x' || text[2] == 'X' || text[2] == 'b' || text[2] == 'B' || text[2] == 'o' || text[2] == 'O')) {
      index_value = NAN; /* JS rejects a sign before a radix literal. */
    } else {
      char *end = NULL;
      index_value = strtod(text, &end);
      if (end == text || (size_t)(end - text) != trimmed) index_value = NAN;
    }
  }
  if (index_value != trunc(index_value) || !isfinite(index_value)) {
    qa_qvm_legacy_client_command_free(result, count);
    qa_range_error("Invalid configstring command index");
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
