/* Port of src/app/bootstrap/network/unified-event-codec.ts.
 *
 * Events are qa_save_value trees in their TS typed-object shape (camelCase keys,
 * ActorId/ClientId as {slot, generation}, SeatId as {index}, the POI fields Map as
 * an ordered object). Each TS reader/writer below is the C function of the same
 * name and does the same checks in the same order, so wire key order and the first
 * reported error match. Readers build a new tree; writers build a new tree and only
 * copy listed fields, as the TS object literals do.
 *
 * Identity: the decoder's returned handle contributes slot/generation/index to the
 * tree and rides on that node as its attachment, so the session token TS keeps on
 * the object is still there for owns() and equals(); see qa_app_unified_event_actor_id.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/unified_event_codec.h"
#include "qa/app/bootstrap/network/unified_finite.h"
#include "qa/app/bootstrap/network/unified_frame_values.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/qvm/memory_writes.h"
#include "qa/compat/qvm/player_record.h"
#include "qa/contracts/content.h"
#include "qa/foundation/support.h"
#include "qa/persistence/execution.h"

#define MAX_EVENT_BYTES ((size_t)16 * 1024 * 1024)
#define MAX_SAFE_INTEGER 9007199254740991.0

/* Decode ownership. Readers build trees bottom-up, so a raise midway would strand every node
 * not yet attached. Inside a read scope each new node is tracked until it is attached or freed;
 * a raise caught above frees the nodes still tracked (disjoint subtree roots, newest first). */
static _Thread_local qa_save_value **ev_pending;
static _Thread_local size_t ev_pending_count, ev_pending_capacity;
static _Thread_local int ev_scopes;
static void ev_track(qa_save_value *value) {
  if (ev_scopes == 0 || !value) return;
  if (ev_pending_count > 0 && ev_pending[ev_pending_count - 1] == value) return;
  if (ev_pending_count == ev_pending_capacity) {
    size_t capacity = ev_pending_capacity ? ev_pending_capacity * 2 : 64;
    qa_save_value **grown = realloc(ev_pending, capacity * sizeof *grown);
    if (!grown) { (qa_save_value_free)(value); qa_range_error("out of memory"); }
    ev_pending = grown;
    ev_pending_capacity = capacity;
  }
  ev_pending[ev_pending_count++] = value;
}
static void ev_untrack(const qa_save_value *value) {
  for (size_t i = ev_pending_count; i-- > 0;)
    if (ev_pending[i] == value) {
      memmove(ev_pending + i, ev_pending + i + 1, (ev_pending_count - i - 1) * sizeof *ev_pending);
      ev_pending_count--;
      return;
    }
}
static void ev_free_value(qa_save_value *value) { ev_untrack(value); (qa_save_value_free)(value); }
static bool ev_raw_set(qa_save_value *object, const char *key, qa_save_value *child) {
  bool ok = (qa_save_value_set)(object, key, child);
  if (ok) ev_untrack(child);
  return ok;
}
static bool ev_raw_push(qa_save_value *array, qa_save_value *child) {
  bool ok = (qa_save_value_push)(array, child);
  if (ok) ev_untrack(child);
  return ok;
}
#define qa_save_value_free ev_free_value
#define qa_save_value_set ev_raw_set
#define qa_save_value_push ev_raw_push
typedef struct ev_scope { size_t mark; qa_cleanup guard; } ev_scope;
static void ev_scope_unwind(void *arg) {
  ev_scope *scope = arg;
  while (ev_pending_count > scope->mark) (qa_save_value_free)(ev_pending[--ev_pending_count]);
  ev_scopes--;
}
static void ev_scope_open(ev_scope *scope) {
  scope->mark = ev_pending_count;
  ev_scopes++;
  qa_cleanup_push(&scope->guard, ev_scope_unwind, scope);
}
/* Success: the result and anything it owns leave the scope's care. */
static qa_save_value *ev_scope_close(ev_scope *scope, qa_save_value *result) {
  qa_cleanup_pop(&scope->guard);
  ev_pending_count = scope->mark;
  ev_scopes--;
  return result;
}
static qa_save_value *ev_oom(qa_save_value *value) {
  if (!value) qa_range_error("out of memory");
  ev_track(value);
  return value;
}
static qa_save_value *ev_object(void) { return ev_oom(qa_save_value_object()); }
static qa_save_value *ev_array(void) { return ev_oom(qa_save_value_array()); }
static qa_save_value *ev_null(void) { return ev_oom(qa_save_value_null()); }
static qa_save_value *ev_undefined(void) { return ev_oom(qa_save_value_undefined()); }
static qa_save_value *ev_number(double value) { return ev_oom(qa_save_value_number(value)); }
static qa_save_value *ev_string(const char *value) { return ev_oom(qa_save_value_string(value)); }
static qa_save_value *ev_bool(bool value) { return ev_oom(qa_save_value_boolean(value)); }
static void ev_push(qa_save_value *array, qa_save_value *child) {
  if (!qa_save_value_push(array, child)) qa_range_error("out of memory");
}
static qa_save_value *ev_clone(const qa_save_value *value) {
  if (!value) return ev_undefined();
  qa_save_value *out = NULL;
  switch (value->kind) {
    case QA_SAVE_UNDEFINED: out = qa_save_value_undefined(); break;
    case QA_SAVE_NULL: out = qa_save_value_null(); break;
    case QA_SAVE_BOOLEAN: out = qa_save_value_boolean(value->boolean); break;
    case QA_SAVE_NUMBER: out = qa_save_value_number(value->number); break;
    case QA_SAVE_BIGINT: out = qa_save_value_bigint_text(value->text); break;
    case QA_SAVE_STRING: out = qa_save_value_string(value->text); break;
    case QA_SAVE_BYTES: out = qa_save_value_bytes(value->bytes, value->byte_length); break;
    case QA_SAVE_ARRAY:
      out = ev_array();
      for (size_t i = 0; i < value->item_count; i++) ev_push(out, ev_clone(value->items[i]));
      break;
    case QA_SAVE_OBJECT:
      out = ev_object();
      for (size_t i = 0; i < value->entry_count; i++)
        if (!qa_save_value_set(out, value->entries[i].key, ev_clone(value->entries[i].value)))
          qa_range_error("out of memory");
      break;
  }
  ev_oom(out);
  if (value->attachment && !qa_save_value_attach(out, value->attachment, value->attachment_length))
    qa_range_error("out of memory");
  return out;
}
/* JS property write: an existing key keeps its position and takes the new value. */
static void ev_set(qa_save_value *object, const char *key, qa_save_value *child) {
  for (size_t i = 0; i < object->entry_count; i++) {
    if (strcmp(object->entries[i].key, key) == 0) {
      qa_save_value_free(object->entries[i].value);
      object->entries[i].value = child;
      return;
    }
  }
  if (!qa_save_value_set(object, key, child)) qa_range_error("out of memory");
}
/* JS object spread; spreading a non-object contributes nothing. */
static void ev_spread(qa_save_value *object, const qa_save_value *source) {
  if (!source || source->kind != QA_SAVE_OBJECT) return;
  for (size_t i = 0; i < source->entry_count; i++)
    ev_set(object, source->entries[i].key, ev_clone(source->entries[i].value));
}
static const qa_save_value ev_undefined_value = {.kind = QA_SAVE_UNDEFINED};
static bool ev_undefined_is(const qa_save_value *value) { return !value || value->kind == QA_SAVE_UNDEFINED; }
/* JS member access: throws TypeError on undefined/null; an absent key is undefined. */
static const qa_save_value *ev_get(const qa_save_value *value, const char *key) {
  if (!value || value->kind == QA_SAVE_UNDEFINED || value->kind == QA_SAVE_NULL)
    qa_range_error("TypeError: Cannot read properties of undefined");
  const qa_save_value *found = value->kind == QA_SAVE_OBJECT ? qa_save_value_get(value, key) : NULL;
  return found ? found : &ev_undefined_value;
}
static const qa_save_value *ev_at(const qa_save_value *value, size_t index) {
  if (!value || value->kind == QA_SAVE_UNDEFINED || value->kind == QA_SAVE_NULL)
    qa_range_error("TypeError: Cannot read properties of undefined");
  return value->kind == QA_SAVE_ARRAY && index < value->item_count ? value->items[index] : &ev_undefined_value;
}
static bool ev_null_is(const qa_save_value *value) { return value && value->kind == QA_SAVE_NULL; }
static bool ev_eq(const qa_save_value *value, const char *expected) {
  return value && value->kind == QA_SAVE_STRING && strcmp(value->text, expected) == 0;
}
static bool ev_take_eq(qa_save_value *value, const char *expected) {
  bool same = ev_eq(value, expected);
  qa_save_value_free(value);
  return same;
}
static bool ev_safe_integer(const qa_save_value *value) {
  return value && value->kind == QA_SAVE_NUMBER && isfinite(value->number)
      && value->number == trunc(value->number) && fabs(value->number) <= MAX_SAFE_INTEGER;
}
static const char *ev_text(const qa_save_value *value) {
  return value && value->kind == QA_SAVE_STRING ? value->text : "";
}
static bool ev_is(const char *kind, const char *label) { return strcmp(kind, label) == 0; }
static qa_save_value *ev_map_identity(const qa_save_value *value) { return ev_clone(value); }
static qa_save_value *ev_map(const qa_save_value *value, qa_save_value *(*write)(const qa_save_value *)) {
  if (!value || value->kind != QA_SAVE_ARRAY) qa_range_error("TypeError: value.map is not a function");
  qa_save_value *out = ev_array();
  for (size_t i = 0; i < value->item_count; i++) ev_push(out, write(value->items[i]));
  return out;
}

/* SaveReader accessors: ev_r_x(reader) is reader.x(); ev_f_x(reader, k) is reader.field(k).x(). */
typedef struct ev_lit { const char *text; double number; } ev_lit;
typedef qa_save_value *(*ev_read_fn)(const qa_save_reader *, const qa_app_unified_identity_decoder *);
static void ev_field(const qa_save_reader *reader, const char *name, qa_save_reader *out) {
  if (!qa_save_reader_field(reader, name, out)) qa_range_error("out of memory");
}
static void ev_item(const qa_save_reader *reader, size_t index, qa_save_reader *out) {
  if (!qa_save_reader_item(reader, index, out)) qa_range_error("out of memory");
}
static qa_save_value *ev_r_finite(const qa_save_reader *r) { return ev_number(qa_save_reader_finite(r)); }
static qa_save_value *ev_r_string(const qa_save_reader *r) { return ev_string(qa_save_reader_string(r)); }
static qa_save_value *ev_r_boolean(const qa_save_reader *r) { return ev_bool(qa_save_reader_boolean(r)); }
static qa_save_value *ev_r_integer(const qa_save_reader *r, double minimum) {
  return ev_number(qa_save_reader_integer_min(r, minimum));
}
static bool ev_lit_matches(const qa_save_reader *r, const ev_lit *lit) {
  const qa_save_value *v = r->value;
  if (!v) return false;
  if (lit->text) return v->kind == QA_SAVE_STRING && strcmp(v->text, lit->text) == 0;
  return v->kind == QA_SAVE_NUMBER && v->number == lit->number;
}
static qa_save_value *ev_lit_value(const ev_lit *lit) {
  return lit->text ? ev_string(lit->text) : ev_number(lit->number);
}
static qa_save_value *ev_r_literal(const qa_save_reader *r, const ev_lit *lits, size_t count) {
  (void)count;
  if (ev_lit_matches(r, lits)) return ev_lit_value(lits);
  if (lits->text) qa_save_reader_fail(r, "expected %s", lits->text);
  qa_save_reader_fail(r, "expected %g", lits->number);
}
static qa_save_value *ev_r_choice(const qa_save_reader *r, const ev_lit *lits, size_t count) {
  for (size_t i = 0; i < count; i++)
    if (ev_lit_matches(r, &lits[i])) return ev_lit_value(&lits[i]);
  char message[1024];
  size_t used = 0;
  message[0] = '\0';
  for (size_t i = 0; i < count && used < sizeof message; i++) {
    int n = lits[i].text
        ? snprintf(message + used, sizeof message - used, "%s%s", i ? " or " : "", lits[i].text)
        : snprintf(message + used, sizeof message - used, "%s%g", i ? " or " : "", lits[i].number);
    if (n < 0) break;
    used += (size_t)n;
  }
  qa_save_reader_fail(r, "expected %s", message);
}
#define EV_FIELD_ACCESSOR(NAME) \
  static qa_save_value *ev_f_##NAME(const qa_save_reader *r, const char *key) { \
    qa_save_reader child; \
    ev_field(r, key, &child); \
    qa_save_value *out = ev_r_##NAME(&child); \
    qa_save_reader_destroy(&child); \
    return out; \
  }
static qa_save_value *ev_r_non_negative(const qa_save_reader *r) { return ev_number(qa_app_unified_non_negative(r)); }
static qa_save_value *ev_r_positive(const qa_save_reader *r) { return ev_number(qa_app_unified_positive(r)); }
EV_FIELD_ACCESSOR(finite)
EV_FIELD_ACCESSOR(non_negative)
EV_FIELD_ACCESSOR(positive)
EV_FIELD_ACCESSOR(string)
EV_FIELD_ACCESSOR(boolean)
static qa_save_value *ev_f_integer(const qa_save_reader *r, const char *key, double minimum) {
  qa_save_reader child;
  ev_field(r, key, &child);
  qa_save_value *out = ev_r_integer(&child, minimum);
  qa_save_reader_destroy(&child);
  return out;
}
static qa_save_value *ev_f_literal(const qa_save_reader *r, const char *key, const ev_lit *lits, size_t count) {
  qa_save_reader child;
  ev_field(r, key, &child);
  qa_save_value *out = ev_r_literal(&child, lits, count);
  qa_save_reader_destroy(&child);
  return out;
}
static qa_save_value *ev_f_choice(const qa_save_reader *r, const char *key, const ev_lit *lits, size_t count) {
  qa_save_reader child;
  ev_field(r, key, &child);
  qa_save_value *out = ev_r_choice(&child, lits, count);
  qa_save_reader_destroy(&child);
  return out;
}
/* The field's string stays owned by the event tree being read. */
static const char *ev_f_text(const qa_save_reader *r, const char *key) {
  qa_save_reader child;
  ev_field(r, key, &child);
  const char *text = qa_save_reader_string(&child);
  qa_save_reader_destroy(&child);
  return text;
}
static bool ev_r_undefined(const qa_save_reader *r) { return qa_save_reader_is_undefined(r); }
static bool ev_r_null(const qa_save_reader *r) { return qa_save_reader_is_null(r); }
static bool ev_r_is_string(const qa_save_reader *r) { return r->value && r->value->kind == QA_SAVE_STRING; }
static bool ev_f_undefined(const qa_save_reader *r, const char *key) {
  qa_save_reader child;
  ev_field(r, key, &child);
  bool result = ev_r_undefined(&child);
  qa_save_reader_destroy(&child);
  return result;
}
static qa_save_value *ev_read(const qa_save_reader *r, const char *key, ev_read_fn read,
    const qa_app_unified_identity_decoder *identity) {
  qa_save_reader child;
  ev_field(r, key, &child);
  qa_save_value *out = read(&child, identity);
  qa_save_reader_destroy(&child);
  return out;
}
static qa_save_value *ev_nullable(const qa_save_reader *r, ev_read_fn read,
    const qa_app_unified_identity_decoder *identity) {
  return ev_r_null(r) ? ev_null() : read(r, identity);
}
static qa_save_value *ev_namespaced(const qa_save_reader *r) { return ev_string(qa_namespaced(r)); }
/* boundedList: reader.list validates the array first, then the 65536 bound applies. */
static size_t ev_bounded_count(const qa_save_reader *r) {
  size_t count = qa_save_reader_list_count(r);
  if (count > 65536) qa_save_reader_fail(r, "invalid event list length");
  return count;
}
static qa_save_value *ev_list(const qa_save_reader *r, ev_read_fn read,
    const qa_app_unified_identity_decoder *identity) {
  size_t count = ev_bounded_count(r);
  qa_save_value *out = ev_array();
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item;
    ev_item(r, i, &item);
    ev_push(out, read(&item, identity));
    qa_save_reader_destroy(&item);
  }
  return out;
}

bool qa_app_unified_event_actor_id(const qa_save_value *node, qa_actor_id *out) {
  const void *handle = qa_save_value_attachment(node, sizeof *out);
  if (!handle) return false;
  memcpy(out, handle, sizeof *out);
  return true;
}
bool qa_app_unified_event_client_id(const qa_save_value *node, qa_client_id *out) {
  const void *handle = qa_save_value_attachment(node, sizeof *out);
  if (!handle) return false;
  memcpy(out, handle, sizeof *out);
  return true;
}
bool qa_app_unified_event_seat_id(const qa_save_value *node, qa_seat_id *out) {
  const void *handle = qa_save_value_attachment(node, sizeof *out);
  if (!handle) return false;
  memcpy(out, handle, sizeof *out);
  return true;
}
static qa_save_value *read_actor_id(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  qa_save_value *slot = ev_f_integer(reader, "slot", 0), *generation = ev_f_integer(reader, "generation", 0);
  qa_actor_id actor = identity->actor(identity->ctx, slot->number, generation->number);
  qa_save_value *out = ev_object();
  ev_set(out, "slot", ev_number((double)actor.slot));
  ev_set(out, "generation", ev_number((double)actor.generation));
  if (!qa_save_value_attach(out, &actor, sizeof actor)) qa_range_error("out of memory");
  qa_save_value_free(slot);
  qa_save_value_free(generation);
  return out;
}
static qa_save_value *write_actor_id(const qa_save_value *value) {
  qa_save_value *out = ev_object();
  ev_set(out, "slot", ev_clone(ev_get(value, "slot")));
  ev_set(out, "generation", ev_clone(ev_get(value, "generation")));
  return out;
}
static qa_save_value *read_client_id(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  qa_save_value *slot = ev_f_integer(reader, "slot", 0), *generation = ev_f_integer(reader, "generation", 0);
  qa_client_id client = identity->client(identity->ctx, slot->number, generation->number);
  qa_save_value *out = ev_object();
  ev_set(out, "slot", ev_number((double)client.slot));
  ev_set(out, "generation", ev_number((double)client.generation));
  if (!qa_save_value_attach(out, &client, sizeof client)) qa_range_error("out of memory");
  qa_save_value_free(slot);
  qa_save_value_free(generation);
  return out;
}
static qa_save_value *write_client_id(const qa_save_value *value) {
  qa_save_value *out = ev_object();
  ev_set(out, "slot", ev_clone(ev_get(value, "slot")));
  ev_set(out, "generation", ev_clone(ev_get(value, "generation")));
  return out;
}
static qa_save_value *read_seat_id(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  qa_save_value *index = ev_f_integer(reader, "index", 0);
  qa_seat_id seat = identity->seat(identity->ctx, index->number);
  qa_save_value *out = ev_object();
  ev_set(out, "index", ev_number((double)seat.index));
  if (!qa_save_value_attach(out, &seat, sizeof seat)) qa_range_error("out of memory");
  qa_save_value_free(index);
  return out;
}
static qa_save_value *write_seat_id(const qa_save_value *value) {
  qa_save_value *out = ev_object();
  ev_set(out, "index", ev_clone(ev_get(value, "index")));
  return out;
}
/* /^resource:unified:[0-9a-f]{64}$/ */
static bool unified_resource_ok(const char *value) {
  static const char prefix[] = "resource:unified:";
  size_t prefix_length = sizeof prefix - 1;
  if (strncmp(value, prefix, prefix_length) != 0) return false;
  const char *hex = value + prefix_length;
  if (strlen(hex) != 64) return false;
  for (size_t i = 0; i < 64; i++) {
    char c = hex[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}
static qa_save_value *read_resource_id(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  const char *value = qa_save_reader_string(reader);
  if (!unified_resource_ok(value)) qa_save_reader_fail(reader, "expected negotiated unified resource identity");
  return ev_string(identity->resource_id(identity->ctx, value));
}
static qa_save_value *write_resource_id(const qa_save_value *value) {
  if (!value || value->kind != QA_SAVE_STRING || !unified_resource_ok(value->text))
    qa_range_error("Unified events require negotiated resource identities");
  return ev_clone(value);
}
/* parts = value.split(':'); [family, edition, package, revision]; createContentId
 * validates the three identity parts and aborts with its own RangeError. */
static qa_save_value *read_content_id(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *value = qa_save_reader_string(reader);
  const char *colon1 = strchr(value, ':');
  const char *colon2 = colon1 ? strchr(colon1 + 1, ':') : NULL;
  const char *colon3 = colon2 ? strchr(colon2 + 1, ':') : NULL;
  size_t family_length = colon1 ? (size_t)(colon1 - value) : strlen(value);
  qa_game_family family;
  if (family_length == 2 && strncmp(value, "q1", 2) == 0) family = QA_GAME_FAMILY_Q1;
  else if (family_length == 2 && strncmp(value, "q2", 2) == 0) family = QA_GAME_FAMILY_Q2;
  else if (family_length == 2 && strncmp(value, "q3", 2) == 0) family = QA_GAME_FAMILY_Q3;
  else qa_save_reader_fail(reader, "invalid content identity");
  if (!colon3 || strchr(colon3 + 1, ':')) qa_save_reader_fail(reader, "invalid content identity");
  char *copy = malloc(strlen(value) + 1);
  if (!copy) qa_range_error("out of memory");
  strcpy(copy, value);
  copy[colon1 - value] = copy[colon2 - value] = copy[colon3 - value] = '\0';
  char *id = qa_create_content_id((qa_content_identity){family, copy + (colon1 - value) + 1,
      copy + (colon2 - value) + 1, copy + (colon3 - value) + 1});
  if (!id) qa_range_error("out of memory");
  qa_save_value *out = ev_string(id);
  free(id);
  free(copy);
  return out;
}
static qa_save_value *source_client_integer(const qa_save_reader *reader, double minimum, double maximum) {
  double value = qa_save_reader_integer_min(reader, minimum);
  if (value > maximum) qa_save_reader_fail(reader, "source client value out of range");
  return ev_number(value);
}
static qa_save_value *read_module(const qa_save_reader *reader) {
  qa_module_identity module = qa_read_module(reader);
  qa_save_value *out = ev_object();
  ev_set(out, "id", ev_string(module.id));
  ev_set(out, "artifactPath", ev_string(module.artifact_path));
  ev_set(out, "digest", ev_string(module.digest));
  ev_set(out, "revision", ev_string(module.revision));
  return out;
}

/* The fields Map is an ordered object; the list form rejects duplicates, as TS does. */
static qa_save_value *read_event_q2_presentation_event_poi_fields(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  size_t count = ev_bounded_count(reader);
  qa_save_value *result = ev_object();
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item;
    ev_item(reader, i, &item);
    const char *key = ev_f_text(&item, "key");
    if (qa_save_value_get(result, key)) qa_save_reader_fail(&item, "duplicate map key");
    ev_set(result, key, ev_f_string(&item, "value"));
    qa_save_reader_destroy(&item);
  }
  return result;
}
static qa_save_value *write_event_q2_presentation_event_poi_fields(const qa_save_value *value) {
  if (!value || value->kind != QA_SAVE_OBJECT) qa_range_error("TypeError: value is not iterable");
  qa_save_value *out = ev_array();
  for (size_t i = 0; i < value->entry_count; i++) {
    qa_save_value *row = ev_object();
    ev_set(row, "key", ev_string(value->entries[i].key));
    ev_set(row, "value", ev_clone(value->entries[i].value));
    ev_push(out, row);
  }
  return out;
}

/* Q3PlayerState in tree form. */
static qa_save_value *vec3_tree(qa_vec3 v) {
  qa_save_value *out = ev_object();
  ev_set(out, "x", ev_number(v.x));
  ev_set(out, "y", ev_number(v.y));
  ev_set(out, "z", ev_number(v.z));
  return out;
}
static qa_save_value *numbers_tree(const double *values, size_t count) {
  qa_save_value *out = ev_array();
  for (size_t i = 0; i < count; i++) ev_push(out, ev_number(values[i]));
  return out;
}
static qa_save_value *player_state_tree(const qa_q3_player_state *s) {
  qa_save_value *o = ev_object();
#define PS_NUM(key, member) ev_set(o, key, ev_number(s->member))
#define PS_VEC(key, member) ev_set(o, key, vec3_tree(s->member))
  PS_NUM("commandTimeMilliseconds", command_time_milliseconds);
  PS_NUM("movementType", movement_type);
  PS_NUM("bobCycle", bob_cycle);
  PS_NUM("movementFlags", movement_flags);
  PS_NUM("movementTimeMilliseconds", movement_time_milliseconds);
  PS_VEC("origin", origin);
  PS_VEC("velocity", velocity);
  PS_NUM("weaponTimeMilliseconds", weapon_time_milliseconds);
  PS_NUM("gravity", gravity);
  PS_NUM("speed", speed);
  ev_set(o, "deltaAngleWords", numbers_tree(s->delta_angle_words.values, 3));
  PS_NUM("groundEntityNumber", ground_entity_number);
  PS_NUM("legsTimerMilliseconds", legs_timer_milliseconds);
  PS_NUM("legsAnimation", legs_animation);
  PS_NUM("torsoTimerMilliseconds", torso_timer_milliseconds);
  PS_NUM("torsoAnimation", torso_animation);
  PS_NUM("movementDirection", movement_direction);
  PS_VEC("grapplePoint", grapple_point);
  PS_NUM("flags", flags);
  PS_NUM("eventSequence", event_sequence);
  ev_set(o, "events", numbers_tree(s->events, 2));
  ev_set(o, "eventParameters", numbers_tree(s->event_parameters, 2));
  PS_NUM("externalEvent", external_event);
  PS_NUM("externalEventParameter", external_event_parameter);
  PS_NUM("externalEventTimeMilliseconds", external_event_time_milliseconds);
  PS_NUM("clientNumber", client_number);
  PS_NUM("weapon", weapon);
  PS_NUM("weaponState", weapon_state);
  PS_VEC("viewAngles", view_angles);
  PS_NUM("viewHeight", view_height);
  PS_NUM("damageEvent", damage_event);
  PS_NUM("damageYaw", damage_yaw);
  PS_NUM("damagePitch", damage_pitch);
  PS_NUM("damageCount", damage_count);
  ev_set(o, "stats", numbers_tree(s->stats.values, s->stats.count));
  ev_set(o, "persistent", numbers_tree(s->persistent.values, s->persistent.count));
  ev_set(o, "powerups", numbers_tree(s->powerups.values, s->powerups.count));
  ev_set(o, "ammo", numbers_tree(s->ammo.values, s->ammo.count));
  PS_NUM("generic1", generic1);
  PS_NUM("loopSound", loop_sound);
  PS_NUM("jumpPadEntity", jump_pad_entity);
  PS_NUM("pingMilliseconds", ping_milliseconds);
  PS_NUM("movementFrameCount", movement_frame_count);
  PS_NUM("jumpPadFrame", jump_pad_frame);
  PS_NUM("entityEventSequence", entity_event_sequence);
#undef PS_NUM
#undef PS_VEC
  return o;
}
static double tree_number(const qa_save_value *value) {
  /* DataView stores coerce with ToNumber; absent or non-number becomes NaN, then 0 as int32. */
  return value && value->kind == QA_SAVE_NUMBER ? value->number : NAN;
}
static qa_vec3 tree_vec3(const qa_save_value *value) {
  return (qa_vec3){tree_number(ev_get(value, "x")), tree_number(ev_get(value, "y")), tree_number(ev_get(value, "z"))};
}
/* Copies a number array into `storage` and returns a view of its real length. */
static qa_double_view tree_slots(const qa_save_value *value, double *storage) {
  if (!value || value->kind != QA_SAVE_ARRAY) qa_range_error("TypeError: state slots are not iterable");
  for (size_t i = 0; i < value->item_count && i < 16; i++) storage[i] = tree_number(value->items[i]);
  return (qa_double_view){storage, value->item_count};
}
typedef struct player_slots { double stats[16], persistent[16], powerups[16], ammo[16]; } player_slots;
static void tree_player_state(const qa_save_value *v, qa_q3_player_state *s, player_slots *slots) {
#define PS_NUM(key, member) s->member = tree_number(ev_get(v, key))
#define PS_VEC(key, member) s->member = tree_vec3(ev_get(v, key))
  PS_NUM("commandTimeMilliseconds", command_time_milliseconds);
  PS_NUM("movementType", movement_type);
  PS_NUM("bobCycle", bob_cycle);
  PS_NUM("movementFlags", movement_flags);
  PS_NUM("movementTimeMilliseconds", movement_time_milliseconds);
  PS_VEC("origin", origin);
  PS_VEC("velocity", velocity);
  PS_NUM("weaponTimeMilliseconds", weapon_time_milliseconds);
  PS_NUM("gravity", gravity);
  PS_NUM("speed", speed);
  for (size_t i = 0; i < 3; i++) s->delta_angle_words.values[i] = tree_number(ev_at(ev_get(v, "deltaAngleWords"), i));
  PS_NUM("groundEntityNumber", ground_entity_number);
  PS_NUM("legsTimerMilliseconds", legs_timer_milliseconds);
  PS_NUM("legsAnimation", legs_animation);
  PS_NUM("torsoTimerMilliseconds", torso_timer_milliseconds);
  PS_NUM("torsoAnimation", torso_animation);
  PS_NUM("movementDirection", movement_direction);
  PS_VEC("grapplePoint", grapple_point);
  PS_NUM("flags", flags);
  PS_NUM("eventSequence", event_sequence);
  for (size_t i = 0; i < 2; i++) {
    s->events[i] = tree_number(ev_at(ev_get(v, "events"), i));
    s->event_parameters[i] = tree_number(ev_at(ev_get(v, "eventParameters"), i));
  }
  PS_NUM("externalEvent", external_event);
  PS_NUM("externalEventParameter", external_event_parameter);
  PS_NUM("externalEventTimeMilliseconds", external_event_time_milliseconds);
  PS_NUM("clientNumber", client_number);
  PS_NUM("weapon", weapon);
  PS_NUM("weaponState", weapon_state);
  PS_VEC("viewAngles", view_angles);
  PS_NUM("viewHeight", view_height);
  PS_NUM("damageEvent", damage_event);
  PS_NUM("damageYaw", damage_yaw);
  PS_NUM("damagePitch", damage_pitch);
  PS_NUM("damageCount", damage_count);
  s->stats = tree_slots(ev_get(v, "stats"), slots->stats);
  s->persistent = tree_slots(ev_get(v, "persistent"), slots->persistent);
  s->powerups = tree_slots(ev_get(v, "powerups"), slots->powerups);
  s->ammo = tree_slots(ev_get(v, "ammo"), slots->ammo);
  PS_NUM("generic1", generic1);
  PS_NUM("loopSound", loop_sound);
  PS_NUM("jumpPadEntity", jump_pad_entity);
  PS_NUM("pingMilliseconds", ping_milliseconds);
  PS_NUM("movementFrameCount", movement_frame_count);
  PS_NUM("jumpPadFrame", jump_pad_frame);
  PS_NUM("entityEventSequence", entity_event_sequence);
#undef PS_NUM
#undef PS_VEC
}
static qa_save_value *read_source_player_state(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  size_t count = qa_save_reader_list_count(reader);
  uint8_t *bytes = malloc(count ? count : 1);
  if (!bytes) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item;
    ev_item(reader, i, &item);
    double value = qa_save_reader_integer_min(&item, 0);
    if (value > 255) qa_save_reader_fail(&item, "source client value out of range");
    bytes[i] = (uint8_t)value;
    qa_save_reader_destroy(&item);
  }
  if (count != (size_t)qa_qvm_player_state_bytes("q3-modern"))
    qa_save_reader_fail(reader, "invalid source player state record length");
  qa_qvm_memory_writes writes;
  qa_qvm_memory_writes_init(&writes, bytes, count, NULL, NULL);
  qa_qvm_writable_view view;
  qa_qvm_writable_view_init(&view, bytes, count, 0, (int64_t)count, &writes);
  double stats[16], persistent[16], powerups[16], ammo[16];
  qa_q3_player_state state = {0};
  state.stats = (qa_double_view){stats, 16};
  state.persistent = (qa_double_view){persistent, 16};
  state.powerups = (qa_double_view){powerups, 16};
  state.ammo = (qa_double_view){ammo, 16};
  qa_qvm_read_source_player_state(&view, "q3-modern", &state);
  const qa_vec3 vectors[4] = {state.origin, state.velocity, state.grapple_point, state.view_angles};
  for (size_t i = 0; i < 4; i++)
    if (!isfinite(vectors[i].x) || !isfinite(vectors[i].y) || !isfinite(vectors[i].z))
      qa_save_reader_fail(reader, "nonfinite source player vector");
  qa_qvm_memory_writes_destroy(&writes);
  free(bytes);
  return player_state_tree(&state);
}
/* The transport uses the complete modern record; source enums are not translated. */
static qa_save_value *write_source_player_state(const qa_save_value *value) {
  size_t size = (size_t)qa_qvm_player_state_bytes("q3-modern");
  uint8_t *bytes = calloc(size, 1);
  if (!bytes) qa_range_error("out of memory");
  qa_qvm_memory_writes writes; /* A plain DataView: no watches, no effects. */
  qa_qvm_memory_writes_init(&writes, bytes, size, NULL, NULL);
  qa_qvm_writable_view view;
  qa_qvm_writable_view_init(&view, bytes, size, 0, (int64_t)size, &writes);
  qa_q3_player_state state = {0};
  player_slots slots = {{0}, {0}, {0}, {0}};
  tree_player_state(value, &state, &slots);
  qa_qvm_write_player_state(&view, &state, "q3-modern", NULL);
  qa_qvm_memory_writes_destroy(&writes);
  qa_save_value *out = ev_array();
  for (size_t i = 0; i < size; i++) ev_push(out, ev_number(bytes[i]));
  free(bytes);
  return out;
}
