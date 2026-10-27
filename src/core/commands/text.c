/* Port of src/core/commands/text.ts. GPL-2.0-or-later. */
#include "qa/core/commands/text.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static char *copy_range(const char *text, size_t start, size_t end) {
  char *copy = (char *)malloc(end - start + 1);
  if (copy == NULL) return NULL;
  memcpy(copy, text + start, end - start);
  copy[end - start] = '\0';
  return copy;
}

char *qa_source_command_text(const char *input) {
  return copy_range(input, 0, strlen(input));
}

char *qa_ascii_fold(const char *text) {
  size_t length = strlen(text);
  char *copy = copy_range(text, 0, length);
  if (copy == NULL) return NULL;
  for (size_t i = 0; i < length; i++) {
    uint8_t byte = (uint8_t)copy[i];
    if (byte >= 65 && byte <= 90) copy[i] = (char)(byte + 32);
  }
  return copy;
}

bool qa_command_dialect_is_q1(qa_command_dialect dialect) {
  return dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE || dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD;
}
bool qa_command_dialect_is_q2(qa_command_dialect dialect) {
  return dialect == QA_COMMAND_DIALECT_Q2_CLASSIC || dialect == QA_COMMAND_DIALECT_Q2_RERELEASE;
}

size_t qa_command_separator_offset(const char *text, qa_command_dialect dialect) {
  int quoted = 0;
  size_t offset = 0;
  while (text[offset] != '\0') {
    char character = text[offset];
    if (character == '"') quoted = !quoted;
    if ((!quoted && character == ';') || character == '\n' ||
        (dialect == QA_COMMAND_DIALECT_Q3 && character == '\r')) break;
    offset++;
  }
  return offset;
}

/* Original signed-char lexers skip high bytes in source mode. */
static int is_whitespace(const char *text, size_t index, qa_command_text_mode mode) {
  uint8_t byte = (uint8_t)text[index];
  return byte <= 32 || (mode == QA_COMMAND_TEXT_SOURCE && byte >= 128);
}

static int starts_with_at(const char *text, size_t offset, const char *prefix) {
  return strncmp(text + offset, prefix, strlen(prefix)) == 0;
}

typedef struct parsed_token {
  char *value; /* malloc'd; NULL marks "no token" (or OOM under has_token) */
  size_t end;
  int has_token;
} parsed_token;

static parsed_token parse_token(const char *text, size_t length, size_t start,
                                qa_command_dialect dialect, qa_command_text_mode mode) {
  parsed_token result = {NULL, start, 0};
  size_t offset = start;
  for (;;) {
    while (offset < length && is_whitespace(text, offset, mode)) offset++;
    if (offset < length && starts_with_at(text, offset, "//")) {
      if (dialect == QA_COMMAND_DIALECT_Q3) return result;
      while (offset < length && text[offset] != '\n') offset++;
      continue;
    }
    if (dialect == QA_COMMAND_DIALECT_Q3 && offset < length && starts_with_at(text, offset, "/*")) {
      const char *end = offset + 2 <= length ? strstr(text + offset + 2, "*/") : NULL;
      offset = end == NULL ? length : (size_t)(end - text) + 2;
      continue;
    }
    break;
  }
  if (offset >= length) return result;
  int quoted = text[offset] == '"';
  if (quoted) offset++;
  size_t token_start = offset;
  if (quoted) {
    while (offset < length && text[offset] != '"') offset++;
  } else if (dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE &&
             strchr("{}()':", text[offset]) != NULL) {
    offset++;
  } else {
    while (offset < length && !is_whitespace(text, offset, mode)) {
      if (dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE &&
          strchr("{}()':", text[offset]) != NULL) break;
      if (dialect == QA_COMMAND_DIALECT_Q3 &&
          (text[offset] == '"' || starts_with_at(text, offset, "//") ||
           starts_with_at(text, offset, "/*"))) break;
      offset++;
    }
  }
  size_t value_length = offset - token_start;
  int empty_value = 0;
  if (qa_command_dialect_is_q2(dialect) && value_length >= 128) {
    if (quoted) qa_range_error("Quoted command token overflows source MAX_TOKEN_CHARS");
    empty_value = 1;
  } else if (qa_command_dialect_is_q1(dialect) && value_length >= 1024) {
    qa_range_error("Command token overflows source com_token");
  }
  if (quoted && offset < length && text[offset] == '"') offset++;
  result.value = empty_value ? copy_range("", 0, 0) : copy_range(text, token_start, token_start + value_length);
  result.end = offset;
  result.has_token = 1;
  return result;
}

bool qa_expand_command_macros(const char *input, qa_command_variable_fn variable,
                              void *variable_ctx, qa_command_print_fn print,
                              void *print_ctx, qa_command_text_mode mode, char **out) {
  *out = NULL;
  char *text = qa_source_command_text(input);
  if (text == NULL) return true; /* OOM only */
  size_t budget_length = strlen(text);
  if (budget_length >= 1024) {
    print(print_ctx, "Line exceeded 1024 chars, discarded.\n");
    free(text);
    return false;
  }
  int quoted = 0, count = 0;
  for (size_t offset = 0; text[offset] != '\0'; offset++) {
    if (text[offset] == '"') quoted = !quoted;
    if (quoted || text[offset] != '$') continue;
    parsed_token token = parse_token(text, strlen(text), offset + 1,
                                     QA_COMMAND_DIALECT_Q2_CLASSIC, mode);
    if (!token.has_token) continue;
    if (token.value == NULL) { free(text); return true; } /* OOM only */
    const char *value = variable(variable_ctx, token.value);
    size_t value_length = strlen(value);
    budget_length += value_length;
    if (budget_length >= 1024) {
      print(print_ctx, "Expanded line exceeded 1024 chars, discarded.\n");
      free(token.value);
      free(text);
      return false;
    }
    size_t tail_start = token.end;
    size_t new_length = offset + value_length + strlen(text + tail_start);
    char *replaced = (char *)malloc(new_length + 1);
    if (replaced == NULL) { free(token.value); free(text); return true; }
    memcpy(replaced, text, offset);
    memcpy(replaced + offset, value, value_length);
    strcpy(replaced + offset + value_length, text + tail_start);
    free(token.value);
    free(text);
    text = replaced;
    offset--; /* size_t underflow at 0 wraps, then offset++ restores 0 */
    if (++count == 100) {
      print(print_ctx, "Macro expansion loop, discarded.\n");
      free(text);
      return false;
    }
  }
  if (quoted) {
    print(print_ctx, "Line has unmatched quote, discarded.\n");
    free(text);
    return false;
  }
  *out = text;
  return true;
}

char *qa_command_text_tail(const char *input, qa_command_dialect dialect, int64_t count) {
  if (count < 0) qa_range_error("Command prefix length must be a nonnegative integer");
  size_t length = strlen(input);
  size_t offset = 0;
  for (int64_t index = 0; index < count; index++) {
    while (offset < length && is_whitespace(input, offset, QA_COMMAND_TEXT_SOURCE)) offset++;
    parsed_token token = parse_token(input, length, offset, dialect, QA_COMMAND_TEXT_SOURCE);
    if (!token.has_token) return copy_range("", 0, 0);
    free(token.value);
    offset = token.end;
  }
  while (offset < length && is_whitespace(input, offset, QA_COMMAND_TEXT_SOURCE)) offset++;
  return copy_range(input, offset, length);
}

static char *join_tokens(char **argv, size_t argc) {
  size_t total = 1;
  for (size_t i = 1; i < argc; i++) total += strlen(argv[i]) + 1;
  char *joined = (char *)malloc(total);
  if (joined == NULL) return NULL;
  joined[0] = '\0';
  for (size_t i = 1; i < argc; i++) {
    if (i > 1) strcat(joined, " ");
    strcat(joined, argv[i]);
  }
  return joined;
}

void qa_command_tokens_destroy(qa_command_tokens *tokens) {
  for (size_t i = 0; i < tokens->argc; i++) free(tokens->argv[i]);
  free(tokens->argv);
  free(tokens->args_text);
  tokens->argv = NULL;
  tokens->argc = 0;
  tokens->args_text = NULL;
}

bool qa_tokenize_command(const char *input, qa_command_dialect dialect,
                         qa_command_text_mode mode, qa_command_tokens *out) {
  out->argv = NULL;
  out->argc = 0;
  out->args_text = NULL;
  size_t length = strlen(input);
  size_t maximum_tokens = dialect == QA_COMMAND_DIALECT_Q3 ? 1024 : 80;
  size_t capacity = 0, offset = 0, stored_bytes = 0;
  char *args_text = copy_range("", 0, 0);
  if (args_text == NULL) return false;
  while (offset < length) {
    while (offset < length && is_whitespace(input, offset, mode) &&
           (dialect == QA_COMMAND_DIALECT_Q3 || input[offset] != '\n')) offset++;
    if (dialect != QA_COMMAND_DIALECT_Q3 && offset < length && input[offset] == '\n') break;
    if (out->argc == 1) {
      free(args_text);
      if (qa_command_dialect_is_q2(dialect)) {
        size_t end = length;
        while (end > offset && (uint8_t)input[end - 1] <= 0x20) end--;
        args_text = copy_range(input, offset, end);
      } else {
        args_text = copy_range(input, offset, length);
      }
      if (args_text == NULL) { qa_command_tokens_destroy(out); return false; }
    }
    parsed_token token = parse_token(input, length, offset, dialect, mode);
    if (!token.has_token) break;
    if (token.value == NULL) { free(args_text); qa_command_tokens_destroy(out); return false; }
    offset = token.end;
    if (out->argc < maximum_tokens) {
      stored_bytes += strlen(token.value) + 1;
      if (dialect == QA_COMMAND_DIALECT_Q3 && stored_bytes > 9216) {
        free(token.value);
        free(args_text);
        qa_command_tokens_destroy(out);
        qa_range_error("Command tokens overflow source cmd_tokenized");
      }
      if (out->argc == capacity) {
        size_t grown_capacity = capacity == 0 ? 16 : capacity * 2;
        char **grown = (char **)realloc(out->argv, grown_capacity * sizeof *grown);
        if (grown == NULL) {
          free(token.value);
          free(args_text);
          qa_command_tokens_destroy(out);
          return false;
        }
        out->argv = grown;
        capacity = grown_capacity;
      }
      out->argv[out->argc++] = token.value;
    } else {
      free(token.value);
    }
    if (dialect == QA_COMMAND_DIALECT_Q3 && out->argc == maximum_tokens) break;
  }
  if (dialect == QA_COMMAND_DIALECT_Q3) {
    free(args_text);
    args_text = join_tokens(out->argv, out->argc);
    if (args_text == NULL) { qa_command_tokens_destroy(out); return false; }
  }
  out->args_text = args_text;
  return true;
}
