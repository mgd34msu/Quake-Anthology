/* Port of src/formats/q3-map/entities.ts. GPL-2.0-or-later. */
#include "qa/formats/q3_map/entities.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}

static void *xrealloc(void *p, size_t size) {
  void *next = realloc(p, size == 0 ? 1 : size);
  if (next == NULL) qa_range_error("Out of memory");
  return next;
}

noreturn void qa_q3_text_parse_error(const char *source_name, double line, double column,
                                     const char *message) {
  fprintf(stderr, "TextParseError: %s:%g:%g: %s\n", source_name, line, column, message);
  abort();
}

static bool is_whitespace(char character) { return (unsigned char)character <= 32; }

static bool starts_with(const qa_q3_tokenizer *tokenizer, const char *prefix) {
  size_t length = strlen(prefix);
  return tokenizer->offset + length <= tokenizer->source_length &&
         memcmp(tokenizer->source + tokenizer->offset, prefix, length) == 0;
}

static void advance(qa_q3_tokenizer *tokenizer) {
  if (tokenizer->offset >= tokenizer->source_length) return;
  char character = tokenizer->source[tokenizer->offset];
  if (character == '\r') {
    tokenizer->offset++;
    if (tokenizer->offset < tokenizer->source_length && tokenizer->source[tokenizer->offset] == '\n')
      tokenizer->offset++;
    tokenizer->line++;
    tokenizer->column = 1;
    return;
  }
  tokenizer->offset++;
  if (character == '\n') {
    tokenizer->line++;
    tokenizer->column = 1;
  } else {
    tokenizer->column++;
  }
}

void qa_q3_tokenizer_init(qa_q3_tokenizer *tokenizer, const char *source, const char *name) {
  tokenizer->source = source;
  tokenizer->source_length = strlen(source);
  tokenizer->name = name != NULL ? name : "<text>";
  tokenizer->offset = 0;
  tokenizer->line = 1;
  tokenizer->column = 1;
}

bool qa_q3_tokenizer_next(qa_q3_tokenizer *tokenizer, bool allow_line_breaks, qa_q3_token *out) {
  bool crossed_line = false;
  for (;;) {
    while (tokenizer->offset < tokenizer->source_length) {
      char character = tokenizer->source[tokenizer->offset];
      if (!is_whitespace(character)) break;
      if (character == '\n' || character == '\r') crossed_line = true;
      advance(tokenizer);
    }
    if (starts_with(tokenizer, "//")) {
      advance(tokenizer);
      advance(tokenizer);
      while (tokenizer->offset < tokenizer->source_length) {
        char character = tokenizer->source[tokenizer->offset];
        if (character == '\n' || character == '\r') break;
        advance(tokenizer);
      }
      continue;
    }
    if (starts_with(tokenizer, "/*")) {
      advance(tokenizer);
      advance(tokenizer);
      while (tokenizer->offset < tokenizer->source_length && !starts_with(tokenizer, "*/")) {
        char character = tokenizer->source[tokenizer->offset];
        if (character == '\n' || character == '\r') crossed_line = true;
        advance(tokenizer);
      }
      if (starts_with(tokenizer, "*/")) {
        advance(tokenizer);
        advance(tokenizer);
      }
      continue;
    }
    break;
  }
  if (!allow_line_breaks && crossed_line) return false;
  if (tokenizer->offset >= tokenizer->source_length) return false;
  double line = tokenizer->line;
  double column = tokenizer->column;
  bool quoted = tokenizer->source[tokenizer->offset] == '"';
  char *value = xmalloc(1);
  size_t length = 0, capacity = 1;
  value[0] = '\0';
#define APPEND(ch)                                        \
  do {                                                    \
    if (length + 1 >= capacity) {                         \
      capacity *= 2;                                      \
      value = xrealloc(value, capacity);                  \
    }                                                     \
    value[length++] = (ch);                               \
    value[length] = '\0';                                 \
  } while (0)
  if (quoted) {
    advance(tokenizer);
    while (tokenizer->offset < tokenizer->source_length) {
      char character = tokenizer->source[tokenizer->offset];
      if (character == '"') break;
      APPEND(character);
      advance(tokenizer);
    }
    if (tokenizer->offset < tokenizer->source_length && tokenizer->source[tokenizer->offset] == '"')
      advance(tokenizer);
  } else {
    while (tokenizer->offset < tokenizer->source_length) {
      char character = tokenizer->source[tokenizer->offset];
      if (is_whitespace(character)) break;
      APPEND(character);
      advance(tokenizer);
    }
  }
#undef APPEND
  if (length >= QA_Q3_TOKEN_MAX) {
    char message[128];
    snprintf(message, sizeof message, "token is limited to %d characters", QA_Q3_TOKEN_MAX - 1);
    qa_q3_text_parse_error(tokenizer->name, line, column, message);
  }
  out->value = value;
  out->line = line;
  out->column = column;
  out->quoted = quoted;
  return true;
}

static noreturn void parse_error_at(const qa_q3_tokenizer *tokenizer, const char *message,
                                    const qa_q3_token *token) {
  if (token == NULL) qa_q3_text_parse_error(tokenizer->name, tokenizer->line, tokenizer->column, message);
  qa_q3_text_parse_error(tokenizer->name, token->line, token->column, message);
}

static void entity_set(qa_q3_entity *entity, char *key, char *value) {
  for (size_t i = 0; i < entity->property_count; i++) {
    if (strcmp(entity->properties[i].key, key) == 0) {
      free(entity->properties[i].value);
      entity->properties[i].value = value;
      free(key);
      return;
    }
  }
  entity->properties = xrealloc(entity->properties,
                                (entity->property_count + 1) * sizeof *entity->properties);
  entity->properties[entity->property_count].key = key;
  entity->properties[entity->property_count].value = value;
  entity->property_count++;
}

qa_q3_entity *qa_q3_parse_entities(const char *text, const char *name, size_t *out_count) {
  qa_q3_tokenizer tokenizer;
  qa_q3_tokenizer_init(&tokenizer, text, name != NULL ? name : "<entities>");
  qa_q3_entity *entities = NULL;
  size_t count = 0;
  for (;;) {
    qa_q3_token opening;
    if (!qa_q3_tokenizer_next(&tokenizer, true, &opening)) break;
    if (strcmp(opening.value, "{") != 0) parse_error_at(&tokenizer, "expected \"{\"", &opening);
    free(opening.value);
    qa_q3_entity entity = {NULL, 0};
    for (;;) {
      qa_q3_token key;
      if (!qa_q3_tokenizer_next(&tokenizer, true, &key))
        parse_error_at(&tokenizer, "expected key or \"}\"", NULL);
      if (strcmp(key.value, "}") == 0) {
        free(key.value);
        entities = xrealloc(entities, (count + 1) * sizeof *entities);
        entities[count++] = entity;
        break;
      }
      qa_q3_token value;
      char message[1200];
      if (!qa_q3_tokenizer_next(&tokenizer, false, &value)) {
        snprintf(message, sizeof message, "missing value for entity key \"%s\"", key.value);
        parse_error_at(&tokenizer, message, NULL);
      }
      if (strcmp(value.value, "}") == 0) {
        snprintf(message, sizeof message, "missing value for entity key \"%s\"", key.value);
        parse_error_at(&tokenizer, message, &value);
      }
      entity_set(&entity, key.value, value.value);
    }
  }
  *out_count = count;
  return entities;
}

void qa_q3_entities_destroy(qa_q3_entity *entities, size_t count) {
  if (entities == NULL) return;
  for (size_t i = 0; i < count; i++) {
    for (size_t j = 0; j < entities[i].property_count; j++) {
      free(entities[i].properties[j].key);
      free(entities[i].properties[j].value);
    }
    free(entities[i].properties);
  }
  free(entities);
}

const char *qa_q3_entity_get(const qa_q3_entity *entity, const char *key) {
  for (size_t i = 0; i < entity->property_count; i++) {
    if (strcmp(entity->properties[i].key, key) == 0) return entity->properties[i].value;
  }
  return NULL;
}
