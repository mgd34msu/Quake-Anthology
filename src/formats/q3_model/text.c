/* Port of src/formats/q3-model/text.ts. GPL-2.0-or-later. */
#include "qa/formats/q3_model/text.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}

noreturn void qa_model_text_error(const char *source, size_t offset, const char *message) {
  fprintf(stderr, "ModelTextError: %s:%zu: %s\n", source, offset, message);
  abort();
}

void qa_model_tokens_init(qa_model_tokens *tokens, const char *text, const char *source) {
  tokens->text = text;
  tokens->length = strlen(text);
  tokens->source = source;
  tokens->offset = 0;
}

noreturn void qa_model_tokens_fail(const qa_model_tokens *tokens, const char *message) {
  qa_model_text_error(tokens->source, tokens->offset, message);
}

static bool starts_with(const qa_model_tokens *tokens, const char *prefix) {
  size_t length = strlen(prefix);
  return tokens->offset + length <= tokens->length &&
         memcmp(tokens->text + tokens->offset, prefix, length) == 0;
}

char *qa_model_tokens_next(qa_model_tokens *tokens) {
  while (tokens->offset < tokens->length) {
    char character = tokens->text[tokens->offset];
    if (isspace((unsigned char)character)) { tokens->offset++; continue; }
    if (starts_with(tokens, "//")) {
      size_t end = tokens->offset + 2;
      while (end < tokens->length && tokens->text[end] != '\n') end++;
      tokens->offset = end >= tokens->length ? tokens->length : end + 1;
      continue;
    }
    if (starts_with(tokens, "/*")) {
      size_t end = tokens->offset + 2;
      bool found = false;
      while (end + 1 < tokens->length) {
        if (tokens->text[end] == '*' && tokens->text[end + 1] == '/') { found = true; break; }
        end++;
      }
      if (!found) qa_model_tokens_fail(tokens, "unterminated comment");
      tokens->offset = end + 2;
      continue;
    }
    break;
  }
  if (tokens->offset == tokens->length) return NULL;
  size_t start = tokens->offset++;
  char first = tokens->text[start];
  if (first == '"') {
    size_t end = tokens->offset;
    while (end < tokens->length && tokens->text[end] != '"') end++;
    if (end >= tokens->length) qa_model_tokens_fail(tokens, "unterminated quoted token");
    tokens->offset = end + 1;
    size_t length = end - (start + 1);
    char *value = xmalloc(length + 1);
    memcpy(value, tokens->text + start + 1, length);
    value[length] = '\0';
    return value;
  }
  if (first == '{' || first == '}' || first == '(' || first == ')') {
    char *value = xmalloc(2);
    value[0] = first;
    value[1] = '\0';
    return value;
  }
  while (tokens->offset < tokens->length) {
    char character = tokens->text[tokens->offset];
    if (isspace((unsigned char)character) || character == '{' || character == '}' || character == '(' ||
        character == ')')
      break;
    tokens->offset++;
  }
  size_t length = tokens->offset - start;
  char *value = xmalloc(length + 1);
  memcpy(value, tokens->text + start, length);
  value[length] = '\0';
  return value;
}

char *qa_model_tokens_token(qa_model_tokens *tokens) {
  char *value = qa_model_tokens_next(tokens);
  if (value == NULL) qa_model_tokens_fail(tokens, "unexpected end of model text");
  return value;
}

void qa_model_tokens_expect(qa_model_tokens *tokens, const char *expected) {
  char *actual = qa_model_tokens_token(tokens);
  if (strcmp(actual, expected) != 0) {
    char message[1024];
    snprintf(message, sizeof message, "expected \"%s\", got \"%s\"", expected, actual);
    free(actual);
    qa_model_tokens_fail(tokens, message);
  }
  free(actual);
}

double qa_model_tokens_number(qa_model_tokens *tokens) {
  char *token = qa_model_tokens_token(tokens);
  char *end = NULL;
  double value = strtod(token, &end);
  bool valid = token[0] != '\0' && end != token && *end == '\0' && isfinite(value);
  if (!valid) {
    char message[1024];
    snprintf(message, sizeof message, "invalid number \"%s\"", token);
    free(token);
    qa_model_tokens_fail(tokens, message);
  }
  free(token);
  return value;
}

double qa_model_tokens_float(qa_model_tokens *tokens) {
  double value = (double)(float)qa_model_tokens_number(tokens);
  if (!isfinite(value)) qa_model_tokens_fail(tokens, "number exceeds binary32");
  return value;
}

double qa_model_tokens_integer(qa_model_tokens *tokens, double minimum, double maximum) {
  double value = qa_model_tokens_number(tokens);
  if (value != trunc(value) || value < minimum || value > maximum) {
    char message[256];
    snprintf(message, sizeof message, "integer %g outside %g..%g", value, minimum, maximum);
    qa_model_tokens_fail(tokens, message);
  }
  return value;
}

qa_vec3 qa_model_tokens_vector(qa_model_tokens *tokens) {
  qa_model_tokens_expect(tokens, "(");
  qa_vec3 result = {qa_model_tokens_float(tokens), qa_model_tokens_float(tokens),
                    qa_model_tokens_float(tokens)};
  qa_model_tokens_expect(tokens, ")");
  return result;
}

void qa_model_tokens_end(qa_model_tokens *tokens) {
  char *value = qa_model_tokens_next(tokens);
  if (value != NULL) {
    free(value);
    qa_model_tokens_fail(tokens, "unexpected trailing token");
  }
}

size_t qa_q3_model_at(double index, size_t count, const char *label) {
  if (index != trunc(index) || index < 0 || index >= (double)count) {
    qa_range_error("Missing %s %g", label, index);
  }
  return (size_t)index;
}

void *qa_q3_indexed_records(qa_model_tokens *tokens, size_t count, const char *kind, size_t elem_size,
                            qa_q3_record_read_fn read_record, void *ctx) {
  uint8_t *records = xmalloc(count * elem_size);
  uint8_t *seen = xmalloc(count);
  memset(seen, 0, count);
  memset(records, 0, count * elem_size);
  for (size_t i = 0; i < count; i++) {
    qa_model_tokens_expect(tokens, kind);
    double index = qa_model_tokens_integer(tokens, 0, count > 0 ? (double)count - 1 : -1);
    if (seen[(size_t)index]) {
      char message[256];
      snprintf(message, sizeof message, "duplicate %s %g", kind, index);
      qa_model_tokens_fail(tokens, message);
    }
    seen[(size_t)index] = 1;
    read_record(ctx, records + (size_t)index * elem_size);
  }
  for (size_t i = 0; i < count; i++) {
    if (!seen[i]) {
      char message[256];
      snprintf(message, sizeof message, "missing %s %zu", kind, i);
      qa_model_tokens_fail(tokens, message);
    }
  }
  free(seen);
  return records;
}
