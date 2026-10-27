/*
 * Port of src/persistence/source-json.ts: lossless jsoncpp numeric
 * representation (SaveNumber keeps its source text), parseSourceJson,
 * writeSourceJson, sourceObject, sourceNumber. C accommodations:
 *  - SaveJson becomes an owned qa_save_json tree; SaveObject is the
 *    object kind (ordered entries, mutated by the q2 field writers).
 *  - SaveNumber.bigint() (arbitrary precision) becomes validated
 *    decimal text plus uint64/int64 accessors.
 *  - writeSourceJson orders object keys as JS Object.keys does
 *    (canonical array indices ascending first, then insertion order).
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_SOURCE_JSON_H
#define QA_PERSISTENCE_SOURCE_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum qa_save_json_kind {
  QA_SAVE_JSON_UNDEFINED,
  QA_SAVE_JSON_NULL,
  QA_SAVE_JSON_BOOLEAN,
  QA_SAVE_JSON_STRING,
  QA_SAVE_JSON_NUMBER,
  QA_SAVE_JSON_ARRAY,
  QA_SAVE_JSON_OBJECT
} qa_save_json_kind;

typedef struct qa_save_json qa_save_json;
typedef struct qa_save_json_entry {
  char *key; /* owned */
  qa_save_json *value; /* owned */
} qa_save_json_entry;

struct qa_save_json {
  qa_save_json_kind kind;
  bool boolean;
  char *text; /* owned: string value, or SaveNumber source text */
  qa_save_json **items; /* owned */
  size_t item_count;
  qa_save_json_entry *entries; /* owned */
  size_t entry_count;
};

/* Constructors; NULL only on OOM. The SaveNumber constructor aborts
 * with SaveFormatError("json", "invalid source number") on bad text. */
qa_save_json *qa_save_json_null(void);
qa_save_json *qa_save_json_boolean(bool value);
qa_save_json *qa_save_json_string(const char *value);
qa_save_json *qa_save_number_create(const char *text);
/* TS SaveNumber.from(number|bigint), split by argument type. */
qa_save_json *qa_save_number_from_double(double value);
qa_save_json *qa_save_number_from_bigint_text(const char *decimal);
qa_save_json *qa_save_json_array(void);
qa_save_json *qa_save_json_object(void);
bool qa_save_json_push(qa_save_json *array, qa_save_json *child);
/* Sets (replacing an existing key, as JS assignment) or appends. */
bool qa_save_json_set(qa_save_json *object, const char *key, qa_save_json *child);
qa_save_json *qa_save_json_get(qa_save_json *object, const char *key);
const qa_save_json *qa_save_json_get_const(const qa_save_json *object, const char *key);
void qa_save_json_free(qa_save_json *value);

/* TS SaveNumber.number() / .bigint(). */
double qa_save_number_number(const qa_save_json *number);
const char *qa_save_number_bigint_text(const qa_save_json *number);
uint64_t qa_save_number_bigint_u64(const qa_save_json *number);
int64_t qa_save_number_bigint_i64(const qa_save_json *number);

/* Owned tree; NULL only on OOM. Malformed input aborts. */
qa_save_json *qa_parse_source_json(const char *text);
/* Malloc'd text; NULL only on OOM. */
char *qa_write_source_json(const qa_save_json *value);

/* TS sourceObject(value, path): NULL value is TS undefined. The
 * returned pointer is the same node. */
qa_save_json *qa_source_object(qa_save_json *value, const char *path);
const qa_save_json *qa_source_object_const(const qa_save_json *value, const char *path);
/* TS sourceNumber(value, path, fallback = 0). */
double qa_source_number(const qa_save_json *value, const char *path);
double qa_source_number_fallback(const qa_save_json *value, const char *path, double fallback);

#endif
