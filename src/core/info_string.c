/* Port of src/core/info-string.ts. GPL-2.0-or-later. */
#include "qa/core/info_string.h"

#include <stdlib.h>
#include <string.h>

#include "qa/core/common_error.h"
#include "qa/foundation/support.h"

static char *copy_range(const char *text, size_t first, size_t last) {
  size_t length = last - first;
  char *copy = (char *)malloc(length + 1);
  if (copy == NULL) return NULL;
  memcpy(copy, text + first, length);
  copy[length] = '\0';
  return copy;
}

void qa_print_info(const char *text, qa_print_info_fn print, void *ctx) {
  size_t length = strlen(text);
  size_t cursor = length > 0 && text[0] == '\\' ? 1 : 0;
  while (cursor < length) {
    const char *sep = strchr(text + cursor, '\\');
    size_t separator = sep != NULL ? (size_t)(sep - text) : length;
    size_t key_length = separator - cursor;
    if (key_length >= 512) {
      qa_range_error("Info_Print would overflow its source key buffer");
    }
    /* key.padEnd(20, " "): keys at or over 20 bytes print as-is. */
    char padded[512 + 20];
    memcpy(padded, text + cursor, key_length);
    size_t padded_length = key_length < 20 ? 20 : key_length;
    for (size_t i = key_length; i < padded_length; i++) padded[i] = ' ';
    padded[padded_length] = '\0';
    print(padded, ctx);
    if (sep == NULL) {
      print("MISSING VALUE\n", ctx);
      return;
    }
    const char *next_sep = strchr(text + separator + 1, '\\');
    size_t next = next_sep != NULL ? (size_t)(next_sep - text) : length;
    size_t value_length = next - (separator + 1);
    if (value_length >= 512) {
      qa_range_error("Info_Print would overflow its source value buffer");
    }
    char value[512 + 2];
    memcpy(value, text + separator + 1, value_length);
    value[value_length] = '\n';
    value[value_length + 1] = '\0';
    print(value, ctx);
    cursor = next_sep == NULL ? length : next + 1;
  }
}

static int fold_byte(int byte) {
  return byte >= 'A' && byte <= 'Z' ? byte + 32 : byte;
}

static int fold_equal(const char *a, size_t a_count, const char *b,
                      size_t b_count) {
  if (a_count != b_count) return 0;
  for (size_t i = 0; i < a_count; i++) {
    if (fold_byte((unsigned char)a[i]) != fold_byte((unsigned char)b[i])) {
      return 0;
    }
  }
  return 1;
}

char *qa_info_value_for_key_bounded(const char *input, const char *wanted,
                                    int64_t maximum_length) {
  if (maximum_length < 1 || maximum_length > 8192) {
    qa_range_error("Invalid source info-string bound");
  }
  size_t length = strlen(input);
  size_t key_length = strlen(wanted);
  if (length >= (size_t)maximum_length) {
    qa_common_error(QA_COMMON_ERROR_DROP,
                    "Info_ValueForKey: oversize infostring");
  }
  size_t cursor = length > 0 && input[0] == '\\' ? 1 : 0;
  while (cursor < length) {
    const char *sep = strchr(input + cursor, '\\');
    if (sep == NULL) return copy_range(input, 0, 0);
    size_t separator = (size_t)(sep - input);
    const char *next_sep = strchr(input + separator + 1, '\\');
    size_t value_end = next_sep != NULL ? (size_t)(next_sep - input) : length;
    if (fold_equal(input + cursor, separator - cursor, wanted, key_length)) {
      return copy_range(input, separator + 1, value_end);
    }
    cursor = value_end + 1;
  }
  return copy_range(input, 0, 0);
}

char *qa_info_value_for_key(const char *input, const char *wanted) {
  return qa_info_value_for_key_bounded(input, wanted, 8192);
}
