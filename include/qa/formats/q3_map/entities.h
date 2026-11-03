/*
 * Port of src/formats/q3-map/entities.ts. TextParseError is qa_raise
 * name "TextParseError", message "<source>:<line>:<column>: <message>"
 * (catchable with qa_try, as the TS throw is).
 * Entity maps become ordered key/value lists with JS Map set
 * semantics (duplicate keys overwrite in place). GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_Q3_MAP_ENTITIES_H
#define QA_FORMATS_Q3_MAP_ENTITIES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdnoreturn.h>

#define QA_Q3_TOKEN_MAX 1024

typedef struct qa_q3_token {
  char *value; /* owned */
  double line;
  double column;
  bool quoted;
} qa_q3_token;

typedef struct qa_q3_tokenizer {
  const char *source;     /* borrowed */
  size_t source_length;
  const char *name;       /* borrowed */
  size_t offset;
  double line;
  double column;
} qa_q3_tokenizer;

noreturn void qa_q3_text_parse_error(const char *source_name, double line, double column,
                                     const char *message);

/* `name` NULL means "<text>". */
void qa_q3_tokenizer_init(qa_q3_tokenizer *tokenizer, const char *source, const char *name);
/* False is TS undefined; on true, out->value is owned. */
bool qa_q3_tokenizer_next(qa_q3_tokenizer *tokenizer, bool allow_line_breaks, qa_q3_token *out);

typedef struct qa_q3_entity_property {
  char *key;   /* owned */
  char *value; /* owned */
} qa_q3_entity_property;

typedef struct qa_q3_entity {
  qa_q3_entity_property *properties; /* owned */
  size_t property_count;
} qa_q3_entity;

/* `name` NULL means "<entities>". Owned result. */
qa_q3_entity *qa_q3_parse_entities(const char *text, const char *name, size_t *out_count);
void qa_q3_entities_destroy(qa_q3_entity *entities, size_t count);
const char *qa_q3_entity_get(const qa_q3_entity *entity, const char *key);

#endif
