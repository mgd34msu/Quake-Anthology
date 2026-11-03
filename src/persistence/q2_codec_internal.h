/* Shared reader/builder helpers for the Q2 checkpoint codecs (q2_*.c).
 * Readers fail through qa_save_reader_fail with the TS SaveFormatError
 * messages; builders accumulate an object and report OOM once.
 * GPL-2.0-or-later. */
#ifndef QA_PERSISTENCE_Q2_CODEC_INTERNAL_H
#define QA_PERSISTENCE_Q2_CODEC_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/math.h"
#include "qa/contracts/session.h"
#include "qa/persistence/decoded.h"
#include "qa/persistence/shared.h"
#include "qa/persistence/value.h"

static inline qa_save_reader q2c_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_save_reader_fail(reader, "out of memory");
  return out;
}
static inline qa_save_reader q2c_item(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out)) qa_save_reader_fail(reader, "out of memory");
  return out;
}
static inline double q2c_number(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = q2c_field(reader, name);
  double value = qa_save_reader_number(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static inline double q2c_integer(const qa_save_reader *reader, const char *name, double minimum) {
  qa_save_reader f = q2c_field(reader, name);
  double value = qa_save_reader_integer_min(&f, minimum);
  qa_save_reader_destroy(&f);
  return value;
}
static inline bool q2c_boolean(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = q2c_field(reader, name);
  bool value = qa_save_reader_boolean(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static inline const char *q2c_string(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = q2c_field(reader, name);
  const char *value = qa_save_reader_string(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static inline const char *q2c_choice(const qa_save_reader *reader, const char *name, const char *const *choices,
                                     size_t count) {
  qa_save_reader f = q2c_field(reader, name);
  const char *value = qa_save_reader_choice_string(&f, choices, count);
  qa_save_reader_destroy(&f);
  return value;
}
static inline bool q2c_present(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = q2c_field(reader, name);
  bool present = !qa_save_reader_is_undefined(&f);
  qa_save_reader_destroy(&f);
  return present;
}
/* TS `reader.field(name).value === true`: any other value (or none) is false. */
static inline bool q2c_true(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = q2c_field(reader, name);
  bool value = f.value != NULL && f.value->kind == QA_SAVE_BOOLEAN && f.value->boolean;
  qa_save_reader_destroy(&f);
  return value;
}
static inline bool q2c_is_null(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = q2c_field(reader, name);
  bool null = qa_save_reader_is_null(&f);
  qa_save_reader_destroy(&f);
  return null;
}
static inline qa_vec3 q2c_vector(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = q2c_field(reader, name);
  qa_vec3 value = qa_read_vector(&f);
  qa_save_reader_destroy(&f);
  return value;
}
/* TS readSavedActor: {slot: integer(0), generation: integer(0)}. */
static inline qa_saved_actor_id q2c_read_actor(const qa_save_reader *reader) {
  qa_saved_actor_id actor;
  actor.slot = q2c_integer(reader, "slot", 0);
  actor.generation = q2c_integer(reader, "generation", 0);
  return actor;
}
static inline qa_saved_actor_id q2c_actor(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = q2c_field(reader, name);
  qa_saved_actor_id value = q2c_read_actor(&f);
  qa_save_reader_destroy(&f);
  return value;
}
/* field(name).nullable(readSavedActor): false = null. */
static inline bool q2c_nullable_actor(const qa_save_reader *reader, const char *name, qa_saved_actor_id *out) {
  qa_save_reader f = q2c_field(reader, name);
  bool has = !qa_save_reader_is_null(&f);
  if (has) *out = q2c_read_actor(&f);
  qa_save_reader_destroy(&f);
  return has;
}

/* ---- builders ---- */
typedef struct q2c_object { qa_save_value *value; bool ok; } q2c_object;
static inline q2c_object q2c_object_new(void) {
  q2c_object object;
  object.value = qa_save_value_object();
  object.ok = object.value != NULL;
  return object;
}
/* Takes child (NULL = OOM). */
static inline void q2c_set(q2c_object *object, const char *key, qa_save_value *child) {
  if (!object->ok || child == NULL) { qa_save_value_free(child); object->ok = false; return; }
  if (!qa_save_value_set(object->value, key, child)) { qa_save_value_free(child); object->ok = false; }
}
static inline qa_save_value *q2c_done(q2c_object *object) {
  if (object->ok) return object->value;
  qa_save_value_free(object->value);
  return NULL;
}
static inline bool q2c_push(qa_save_value *array, qa_save_value *child) {
  if (array == NULL || child == NULL) { qa_save_value_free(child); return false; }
  if (!qa_save_value_push(array, child)) { qa_save_value_free(child); return false; }
  return true;
}
static inline qa_save_value *q2c_string_value(const char *text) { return qa_save_value_string(text != NULL ? text : ""); }
static inline qa_save_value *q2c_vector_value(qa_vec3 vector) {
  q2c_object object = q2c_object_new();
  q2c_set(&object, "x", qa_save_value_number(vector.x));
  q2c_set(&object, "y", qa_save_value_number(vector.y));
  q2c_set(&object, "z", qa_save_value_number(vector.z));
  return q2c_done(&object);
}
static inline qa_save_value *q2c_actor_value(qa_saved_actor_id actor) {
  q2c_object object = q2c_object_new();
  q2c_set(&object, "slot", qa_save_value_number(actor.slot));
  q2c_set(&object, "generation", qa_save_value_number(actor.generation));
  return q2c_done(&object);
}
static inline qa_save_value *q2c_nullable_actor_value(bool has, qa_saved_actor_id actor) {
  return has ? q2c_actor_value(actor) : qa_save_value_null();
}
static inline qa_save_value *q2c_time_value(qa_source_time time) {
  q2c_object object = q2c_object_new();
  q2c_set(&object, "kind", qa_save_value_string(time.kind == QA_SOURCE_TIME_SECONDS ? "seconds" : "milliseconds"));
  q2c_set(&object, "value", qa_save_value_number(time.value));
  return q2c_done(&object);
}
/* Encodes a built tree (NULL = OOM) to the TS checkpoint envelope. */
static inline uint8_t *q2c_encode(qa_save_value *value, size_t *out_length) {
  if (value == NULL) return NULL;
  uint8_t *bytes = qa_encode_checkpoint_value(value, out_length);
  qa_save_value_free(value);
  return bytes;
}
/* Parses bytes into owner->root and opens a reader on it. False when the
 * bytes do not parse (owner untouched). */
static inline bool q2c_open(const uint8_t *bytes, size_t length, const char *path, qa_persist_decoded *owner,
                            qa_save_reader *reader) {
  qa_save_value *root = qa_decode_checkpoint_value(bytes, length);
  if (root == NULL) return false;
  if (!qa_save_reader_init(reader, root, path)) { qa_save_value_free(root); return false; }
  owner->root = root;
  return true;
}

#endif
