/* Port of src/core/cvars/info.ts. GPL-2.0-or-later. */
#include "qa/core/cvars/info.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static char *join3(const char *a, size_t a_length, const char *b, size_t b_length) {
  char *joined = (char *)malloc(a_length + b_length + 1);
  if (joined == NULL) return NULL;
  memcpy(joined, a, a_length);
  memcpy(joined + a_length, b, b_length);
  joined[a_length + b_length] = '\0';
  return joined;
}

static char *copy_text(const char *text, size_t length) { return join3(text, length, "", 0); }

char *qa_set_info_value(const char *input, const char *key_input,
                        const char *value_input, const qa_info_options *options) {
  /* sourceCommandText on C byte strings is the identity. */
  const char *info = input, *key = key_input, *value = value_input;
  size_t info_length = strlen(info), key_length = strlen(key), value_length = strlen(value);
  int64_t maximum = options->maximum_length;
  int q3 = options->dialect == QA_COMMAND_DIALECT_Q3;
  int qw = options->dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD;
  qa_command_print_fn print = options->print;
  void *print_ctx = options->print_ctx;
  if ((int64_t)info_length >= maximum) {
    qa_range_error("Info_SetValueForKey: oversize infostring");
  }
  if (strchr(key, '\\') != NULL || strchr(value, '\\') != NULL) {
    print(print_ctx, "Can't use keys or values with a \\\n");
    return copy_text(info, info_length);
  }
  if ((!qw && strchr(key, ';') != NULL) || (q3 && strchr(value, ';') != NULL)) {
    print(print_ctx, "Can't use keys or values with a semicolon\n");
    return copy_text(info, info_length);
  }
  if (strchr(key, '"') != NULL || strchr(value, '"') != NULL) {
    print(print_ctx, "Can't use keys or values with a \"\n");
    return copy_text(info, info_length);
  }
  if (qw && key[0] == '*') {
    print(print_ctx, "Can't set * keys\n");
    return copy_text(info, info_length);
  }
  if (!q3 && (key_length > 63 || value_length > 63)) {
    print(print_ctx, "Keys and values must be < 64 characters.\n");
    return copy_text(info, info_length);
  }
  /* Find and cut an existing pair with this key. */
  char *result = NULL;
  size_t result_length = info_length, cursor = 0;
  int replaced = 0;
  while (cursor < info_length) {
    size_t start = cursor;
    if (info[cursor] == '\\') cursor++;
    const char *separator = strchr(info + cursor, '\\');
    if (separator == NULL) break;
    size_t separator_index = (size_t)(separator - info);
    const char *next = strchr(info + separator_index + 1, '\\');
    size_t end = next == NULL ? info_length : (size_t)(next - info);
    if (separator_index - cursor == key_length &&
        memcmp(info + cursor, key, key_length) == 0) {
      if (qw && end > separator_index + 1 &&
          (int64_t)value_length - (int64_t)(end - separator_index - 1) +
                  (int64_t)info_length > maximum) {
        print(print_ctx, "Info string length exceeded\n");
        return copy_text(info, info_length);
      }
      result = join3(info, start, info + end, info_length - end);
      if (result == NULL) return NULL;
      result_length = strlen(result);
      replaced = 1;
      break;
    }
    cursor = end;
  }
  if (!replaced) {
    result = copy_text(info, info_length);
    if (result == NULL) return NULL;
  }
  if (value_length == 0) return result;
  size_t pair_length = 1 + key_length + 1 + value_length;
  char *pair = (char *)malloc(pair_length + 1);
  if (pair == NULL) { free(result); return NULL; }
  pair[0] = '\\';
  memcpy(pair + 1, key, key_length);
  pair[1 + key_length] = '\\';
  memcpy(pair + key_length + 2, value, value_length);
  pair[pair_length] = '\0';
  if (q3 && (int64_t)pair_length >= maximum) {
    /* JS slice(0, maximum - 1): a negative end counts from the end. */
    int64_t end = maximum - 1;
    pair_length = end < 0 ? ((int64_t)pair_length + end > 0 ? (size_t)((int64_t)pair_length + end) : 0)
                          : (size_t)end;
    pair[pair_length] = '\0';
  }
  if ((int64_t)(pair_length + result_length) > maximum) {
    print(print_ctx, "Info string length exceeded\n");
    free(pair);
    return result;
  }
  if (!q3) {
    char *folded_key = qa_ascii_fold(key);
    if (folded_key == NULL) { free(pair); free(result); return NULL; }
    int key_is_name = strcmp(folded_key, "name") == 0;
    int key_is_team = strcmp(folded_key, "team") == 0;
    size_t filtered_length = 0;
    for (size_t index = 0; index < pair_length; index++) {
      int byte = (uint8_t)pair[index];
      if (qw) {
        int strip = options->target == QA_CVAR_INFO_SERVER_INFO
                        ? !options->server_high_characters
                        : !key_is_name;
        if (strip) {
          byte &= 127;
          if (byte < 32 || byte > 127) continue;
          if (options->target == QA_CVAR_INFO_CLIENT_USERINFO && key_is_team &&
              byte >= 65 && byte <= 90) byte += 32;
        }
        if (byte > 13) pair[filtered_length++] = (char)byte;
      } else {
        byte &= 127;
        if (byte >= 32 && byte < 127) pair[filtered_length++] = (char)byte;
      }
    }
    free(folded_key);
    pair_length = filtered_length;
    pair[pair_length] = '\0';
  }
  if ((int64_t)(pair_length + result_length) == maximum) {
    free(pair);
    free(result);
    qa_range_error("Info string overflows source terminator");
  }
  char *joined = q3 && maximum != 8192 ? join3(pair, pair_length, result, result_length)
                                       : join3(result, result_length, pair, pair_length);
  free(pair);
  free(result);
  return joined;
}
