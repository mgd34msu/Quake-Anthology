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
#include "qa/foundation/js.h"

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
    bytes[i] = qa_js_to_uint8(value);
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

/* One C function per TS reader/writer, same order. */
static qa_save_value *read_provider_id(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_provider_id(const qa_save_value *value);
static qa_save_value *read_item_id(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_item_id(const qa_save_value *value);
static qa_save_value *read_objective_id(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_objective_id(const qa_save_value *value);
static qa_save_value *write_content_id(const qa_save_value *value);
static qa_save_value *read_vector(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_vector(const qa_save_value *value);
static qa_save_value *read_trajectory(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_trajectory(const qa_save_value *value);
static qa_save_value *read_entity_state(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_entity_state(const qa_save_value *value);
static qa_save_value *read_music_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_music_event(const qa_save_value *event);
static qa_save_value *read_event_vec3(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_vec3(const qa_save_value *value);
static qa_save_value *read_event_q1_event_message_args(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q1_event_message_args(const qa_save_value *value);
static qa_save_value *read_event_q1_message_part(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q1_message_part(const qa_save_value *value);
static qa_save_value *read_event_q1_event_message_parts(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q1_event_message_parts(const qa_save_value *value);
static qa_save_value *read_event_q1_event_effect_actor(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q1_event_effect_actor(const qa_save_value *value);
static qa_save_value *read_event_q1_event_colored_explosion(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q1_event_colored_explosion(const qa_save_value *value);
static qa_save_value *read_event_q1_event_lightstyle(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q1_event_lightstyle(const qa_save_value *value);
static qa_save_value *read_event_q1_weapon(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q1_weapon(const qa_save_value *value);
static qa_save_value *read_event_q1_character_attack(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q1_character_attack(const qa_save_value *value);
static qa_save_value *read_event_q1_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q1_event(const qa_save_value *value);
static qa_save_value *read_event_simulation_presentation_event_q1_source_entity(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_simulation_presentation_event_q1_source_entity(const qa_save_value *value);
static qa_save_value *read_event_q1_fog(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q1_fog(const qa_save_value *value);
static qa_save_value *read_event_q1_fog_transition(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q1_fog_transition(const qa_save_value *value);
static qa_save_value *read_event_q1_addon_event_sell_screen(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q1_addon_event_sell_screen(const qa_save_value *value);
static qa_save_value *read_event_q1_addon_event_alpha(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q1_addon_event_alpha(const qa_save_value *value);
static qa_save_value *read_event_q1_addon_event_developer_message(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q1_addon_event_developer_message(const qa_save_value *value);
static qa_save_value *read_event_q1_addon_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q1_addon_event(const qa_save_value *value);
static qa_save_value *read_event_q1_client_snapshot_userinfo_item(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q1_client_snapshot_userinfo_item(const qa_save_value *value);
static qa_save_value *read_event_q1_client_snapshot_userinfo(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q1_client_snapshot_userinfo(const qa_save_value *value);
static qa_save_value *read_event_q1_client_snapshot(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q1_client_snapshot(const qa_save_value *value);
static qa_save_value *read_event_ctf_status(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_ctf_status(const qa_save_value *value);
static qa_save_value *read_event_q1_composition_event_prompt_choices_item(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q1_composition_event_prompt_choices_item(const qa_save_value *value);
static qa_save_value *read_event_q1_composition_event_prompt_choices(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q1_composition_event_prompt_choices(const qa_save_value *value);
static qa_save_value *read_event_q1_source_finale_finale(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q1_source_finale_finale(const qa_save_value *value);
static qa_save_value *read_event_q1_source_finale(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q1_source_finale(const qa_save_value *value);
static qa_save_value *read_event_q1_composition_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q1_composition_event(const qa_save_value *value);
static qa_save_value *read_event_q1_intermission_result(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q1_intermission_result(const qa_save_value *value);
static qa_save_value *read_event_q2_presentation_event_model_attached_models(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_presentation_event_model_attached_models(const qa_save_value *value);
static qa_save_value *read_event_q2_presentation_event_dynamic_light_cone_value(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_presentation_event_dynamic_light_cone_value(const qa_save_value *value);
static qa_save_value *read_event_q2_presentation_event_dynamic_light_cone(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_presentation_event_dynamic_light_cone(const qa_save_value *value);
static qa_save_value *read_event_q2_presentation_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_presentation_event(const qa_save_value *value);
static qa_save_value *read_event_q2_weapon_event_view_weapon_weapon(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_weapon_event_view_weapon_weapon(const qa_save_value *value);
static qa_save_value *read_event_q2_weapon_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_weapon_event(const qa_save_value *value);
static qa_save_value *read_event_q2_ctf_score_row_carried_flag(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_ctf_score_row_carried_flag(const qa_save_value *value);
static qa_save_value *read_event_q2_ctf_score_row(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_ctf_score_row(const qa_save_value *value);
static qa_save_value *read_event_q2_ctf_event_scoreboard_red(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_ctf_event_scoreboard_red(const qa_save_value *value);
static qa_save_value *read_event_q2_ctf_event_scoreboard_captures(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_ctf_event_scoreboard_captures(const qa_save_value *value);
static qa_save_value *read_event_q2_ctf_event_hud_flag_states(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_ctf_event_hud_flag_states(const qa_save_value *value);
static qa_save_value *read_event_q2_ctf_event_hud_tech(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_ctf_event_hud_tech(const qa_save_value *value);
static qa_save_value *read_event_q2_ctf_event_menu_entries_item_action(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_ctf_event_menu_entries_item_action(const qa_save_value *value);
static qa_save_value *read_event_q2_ctf_event_menu_entries_item(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_ctf_event_menu_entries_item(const qa_save_value *value);
static qa_save_value *read_event_q2_ctf_event_menu_entries(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_ctf_event_menu_entries(const qa_save_value *value);
static qa_save_value *read_event_q2_ctf_admin_settings(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_ctf_admin_settings(const qa_save_value *value);
static qa_save_value *read_event_q2_ctf_event_grapple_cable(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_ctf_event_grapple_cable(const qa_save_value *value);
static qa_save_value *read_event_q2_ctf_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_ctf_event(const qa_save_value *value);
static qa_save_value *read_event_lmctf_event_menu_entries_item(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_lmctf_event_menu_entries_item(const qa_save_value *value);
static qa_save_value *read_event_lmctf_event_menu_entries(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_lmctf_event_menu_entries(const qa_save_value *value);
static qa_save_value *read_event_lmctf_score_row(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_lmctf_score_row(const qa_save_value *value);
static qa_save_value *read_event_lmctf_event_scoreboard_rows(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_lmctf_event_scoreboard_rows(const qa_save_value *value);
static qa_save_value *read_event_lmctf_event_hud_rune(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_lmctf_event_hud_rune(const qa_save_value *value);
static qa_save_value *read_event_lmctf_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_lmctf_event(const qa_save_value *value);
static qa_save_value *read_event_q2_mission_pack_player_effect(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_mission_pack_player_effect(const qa_save_value *value);
static qa_save_value *read_event_q2_mission_pack_entity_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_mission_pack_entity_event(const qa_save_value *value);
static qa_save_value *read_event_q2_composition_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_composition_event(const qa_save_value *value);
static qa_save_value *read_event_vec4(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_vec4(const qa_save_value *value);
static qa_save_value *read_event_debug_line(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_debug_line(const qa_save_value *value);
static qa_save_value *read_event_q2_rerelease_event_debug_shapes_lines(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_rerelease_event_debug_shapes_lines(const qa_save_value *value);
static qa_save_value *read_event_world_text_input_orientation(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_world_text_input_orientation(const qa_save_value *value);
static qa_save_value *read_event_world_text_input(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_world_text_input(const qa_save_value *value);
static qa_save_value *read_event_q2_fog(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_fog(const qa_save_value *value);
static qa_save_value *read_event_q2_height_fog(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_height_fog(const qa_save_value *value);
static qa_save_value *read_event_q2_fog_state(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_fog_state(const qa_save_value *value);
static qa_save_value *read_event_q2_rerelease_level_entry(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_rerelease_level_entry(const qa_save_value *value);
static qa_save_value *read_event_q2_rerelease_event_end_of_unit_levels(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_rerelease_event_end_of_unit_levels(const qa_save_value *value);
static qa_save_value *read_event_q2_rerelease_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_rerelease_event(const qa_save_value *value);
static qa_save_value *read_event_q2_player_view_selected_item(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_player_view_selected_item(const qa_save_value *value);
static qa_save_value *read_event_q2_player_view_timer_value(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_player_view_timer_value(const qa_save_value *value);
static qa_save_value *read_event_q2_player_view_timer(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_player_view_timer(const qa_save_value *value);
static qa_save_value *read_event_q2_player_view(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_player_view(const qa_save_value *value);
static qa_save_value *read_event_q2_score_row(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_score_row(const qa_save_value *value);
static qa_save_value *read_event_q2_player_event_scoreboard_rows(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_player_event_scoreboard_rows(const qa_save_value *value);
static qa_save_value *read_event_inventory_count_policy(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_inventory_count_policy(const qa_save_value *value);
static qa_save_value *read_event_inventory_entry(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_inventory_entry(const qa_save_value *value);
static qa_save_value *read_event_q2_player_event_inventory_entries(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_player_event_inventory_entries(const qa_save_value *value);
static qa_save_value *read_event_q2_player_event_inventory_labels_item(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_player_event_inventory_labels_item(const qa_save_value *value);
static qa_save_value *read_event_q2_player_event_inventory_labels(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_player_event_inventory_labels(const qa_save_value *value);
static qa_save_value *read_event_q2_player_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_player_event(const qa_save_value *value);
static qa_save_value *read_event_q3_character_presentation_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q3_character_presentation_event(const qa_save_value *value);
static qa_save_value *read_event_q3_source_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q3_source_event(const qa_save_value *value);
static qa_save_value *read_q1_client_metadata(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *read_presentation_owner(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *read_event_simulation_presentation_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *read_source_presentation_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_simulation_presentation_event(const qa_save_value *value);
static qa_save_value *write_source_presentation_event(const qa_save_value *value);
static qa_save_value *read_event_source_time(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_source_time(const qa_save_value *value);
static qa_save_value *read_event_event_audience(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_event_audience(const qa_save_value *value);
static qa_save_value *read_event_q2_native_cause_classic(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_native_cause_classic(const qa_save_value *value);
static qa_save_value *read_event_q2_native_cause_rerelease(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_native_cause_rerelease(const qa_save_value *value);
static qa_save_value *read_event_q2_native_cause(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_q2_native_cause(const qa_save_value *value);
static qa_save_value *read_event_attack_provenance_cause(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_attack_provenance_cause(const qa_save_value *value);
static qa_save_value *read_event_attack_provenance(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_attack_provenance(const qa_save_value *value);
static qa_save_value *read_event_damage_request(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_damage_request(const qa_save_value *value);
static qa_save_value *read_event_kind_none(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_kind_none(const qa_save_value *value);
static qa_save_value *read_event_powered_protection_state(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_powered_protection_state(const qa_save_value *value);
static qa_save_value *read_event_regular_armor_state(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_regular_armor_state(const qa_save_value *value);
static qa_save_value *read_event_armor_state(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_armor_state(const qa_save_value *value);
static qa_save_value *read_event_damage_mutation(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_damage_mutation(const qa_save_value *value);
static qa_save_value *read_event_damage_decision_mutations(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_damage_decision_mutations(const qa_save_value *value);
static qa_save_value *read_event_damage_decision_feedback(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_damage_decision_feedback(const qa_save_value *value);
static qa_save_value *read_event_damage_decision(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_damage_decision(const qa_save_value *value);
static qa_save_value *read_event_damage_outcome(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_damage_outcome(const qa_save_value *value);
static qa_save_value *read_event_transition_decision_stay_blocked(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_transition_decision_stay_blocked(const qa_save_value *value);
static qa_save_value *read_event_transition_decision(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_transition_decision(const qa_save_value *value);
static qa_save_value *read_event_network_event_sound_origin(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_network_event_sound_origin(const qa_save_value *value);
static qa_save_value *read_event_network_event_q2_inventory_counts(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_network_event_q2_inventory_counts(const qa_save_value *value);
static qa_save_value *read_event_network_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_network_event(const qa_save_value *value);
static qa_save_value *read_event_simulation_event_payload(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_simulation_event_payload(const qa_save_value *value);
static qa_save_value *read_event_simulation_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity);
static qa_save_value *write_event_simulation_event(const qa_save_value *value);

static qa_save_value *read_event_q1_event_message_args_item_1(const qa_save_reader *item, const qa_app_unified_identity_decoder *identity);
static qa_save_value *read_event_q2_presentation_event_model_attached_models_item_2(const qa_save_reader *item, const qa_app_unified_identity_decoder *identity);
static qa_save_value *read_event_network_event_q2_inventory_counts_item_3(const qa_save_reader *item, const qa_app_unified_identity_decoder *identity);

static qa_save_value *read_provider_id(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *result = ev_namespaced(reader);
  return result;
}

static qa_save_value *write_provider_id(const qa_save_value *value) {
  qa_save_value *result = ev_clone(value);
  return result;
}

static qa_save_value *read_item_id(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *result = ev_namespaced(reader);
  return result;
}

static qa_save_value *write_item_id(const qa_save_value *value) {
  qa_save_value *result = ev_clone(value);
  return result;
}

static qa_save_value *read_objective_id(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *result = ev_namespaced(reader);
  return result;
}

static qa_save_value *write_objective_id(const qa_save_value *value) {
  qa_save_value *result = ev_clone(value);
  return result;
}

static qa_save_value *write_content_id(const qa_save_value *value) {
  qa_save_value *result = ev_clone(value);
  return result;
}

static qa_save_value *read_vector(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "x", ev_f_finite(reader, "x"));
  ev_set(o1, "y", ev_f_finite(reader, "y"));
  ev_set(o1, "z", ev_f_finite(reader, "z"));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_vector(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "x", ev_clone(ev_get(value, "x")));
  ev_set(o1, "y", ev_clone(ev_get(value, "y")));
  ev_set(o1, "z", ev_clone(ev_get(value, "z")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_trajectory(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "type", ev_f_finite(reader, "type"));
  ev_set(o1, "time", ev_f_finite(reader, "time"));
  ev_set(o1, "duration", ev_f_finite(reader, "duration"));
  ev_set(o1, "base", ev_read(reader, "base", read_vector, identity));
  ev_set(o1, "delta", ev_read(reader, "delta", read_vector, identity));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_trajectory(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "type", ev_clone(ev_get(value, "type")));
  ev_set(o1, "time", ev_clone(ev_get(value, "time")));
  ev_set(o1, "duration", ev_clone(ev_get(value, "duration")));
  ev_set(o1, "base", write_vector(ev_get(value, "base")));
  ev_set(o1, "delta", write_vector(ev_get(value, "delta")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_entity_state(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *state = ev_object();
  ev_set(state, "pos", ev_read(reader, "pos", read_trajectory, identity));
  ev_set(state, "apos", ev_read(reader, "apos", read_trajectory, identity));
  ev_set(state, "origin", ev_read(reader, "origin", read_vector, identity));
  ev_set(state, "origin2", ev_read(reader, "origin2", read_vector, identity));
  ev_set(state, "angles", ev_read(reader, "angles", read_vector, identity));
  ev_set(state, "angles2", ev_read(reader, "angles2", read_vector, identity));
  ev_set(state, "number", ev_f_finite(reader, "number"));
  ev_set(state, "eType", ev_f_finite(reader, "eType"));
  ev_set(state, "eFlags", ev_f_finite(reader, "eFlags"));
  ev_set(state, "time", ev_f_finite(reader, "time"));
  ev_set(state, "time2", ev_f_finite(reader, "time2"));
  ev_set(state, "otherEntityNum", ev_f_finite(reader, "otherEntityNum"));
  ev_set(state, "otherEntityNum2", ev_f_finite(reader, "otherEntityNum2"));
  ev_set(state, "groundEntityNum", ev_f_finite(reader, "groundEntityNum"));
  ev_set(state, "constantLight", ev_f_finite(reader, "constantLight"));
  ev_set(state, "loopSound", ev_f_finite(reader, "loopSound"));
  ev_set(state, "modelindex", ev_f_finite(reader, "modelindex"));
  ev_set(state, "modelindex2", ev_f_finite(reader, "modelindex2"));
  ev_set(state, "clientNum", ev_f_finite(reader, "clientNum"));
  ev_set(state, "frame", ev_f_finite(reader, "frame"));
  ev_set(state, "solid", ev_f_finite(reader, "solid"));
  ev_set(state, "event", ev_f_finite(reader, "event"));
  ev_set(state, "eventParm", ev_f_finite(reader, "eventParm"));
  ev_set(state, "powerups", ev_f_finite(reader, "powerups"));
  ev_set(state, "weapon", ev_f_finite(reader, "weapon"));
  ev_set(state, "legsAnim", ev_f_finite(reader, "legsAnim"));
  ev_set(state, "torsoAnim", ev_f_finite(reader, "torsoAnim"));
  ev_set(state, "generic1", ev_f_finite(reader, "generic1"));
  qa_save_value *result = ev_clone(state);
  qa_save_value_free(state);
  return result;
}

static qa_save_value *write_entity_state(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "number", ev_clone(ev_get(value, "number")));
  ev_set(o1, "eType", ev_clone(ev_get(value, "eType")));
  ev_set(o1, "eFlags", ev_clone(ev_get(value, "eFlags")));
  ev_set(o1, "pos", write_trajectory(ev_get(value, "pos")));
  ev_set(o1, "apos", write_trajectory(ev_get(value, "apos")));
  ev_set(o1, "time", ev_clone(ev_get(value, "time")));
  ev_set(o1, "time2", ev_clone(ev_get(value, "time2")));
  ev_set(o1, "origin", write_vector(ev_get(value, "origin")));
  ev_set(o1, "origin2", write_vector(ev_get(value, "origin2")));
  ev_set(o1, "angles", write_vector(ev_get(value, "angles")));
  ev_set(o1, "angles2", write_vector(ev_get(value, "angles2")));
  ev_set(o1, "otherEntityNum", ev_clone(ev_get(value, "otherEntityNum")));
  ev_set(o1, "otherEntityNum2", ev_clone(ev_get(value, "otherEntityNum2")));
  ev_set(o1, "groundEntityNum", ev_clone(ev_get(value, "groundEntityNum")));
  ev_set(o1, "constantLight", ev_clone(ev_get(value, "constantLight")));
  ev_set(o1, "loopSound", ev_clone(ev_get(value, "loopSound")));
  ev_set(o1, "modelindex", ev_clone(ev_get(value, "modelindex")));
  ev_set(o1, "modelindex2", ev_clone(ev_get(value, "modelindex2")));
  ev_set(o1, "clientNum", ev_clone(ev_get(value, "clientNum")));
  ev_set(o1, "frame", ev_clone(ev_get(value, "frame")));
  ev_set(o1, "solid", ev_clone(ev_get(value, "solid")));
  ev_set(o1, "event", ev_clone(ev_get(value, "event")));
  ev_set(o1, "eventParm", ev_clone(ev_get(value, "eventParm")));
  ev_set(o1, "powerups", ev_clone(ev_get(value, "powerups")));
  ev_set(o1, "weapon", ev_clone(ev_get(value, "weapon")));
  ev_set(o1, "legsAnim", ev_clone(ev_get(value, "legsAnim")));
  ev_set(o1, "torsoAnim", ev_clone(ev_get(value, "torsoAnim")));
  ev_set(o1, "generic1", ev_clone(ev_get(value, "generic1")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_music_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *kind = ev_f_choice(reader, "kind", (const ev_lit[]){{"cd-track", 0}, {"pause", 0}}, 2);
  qa_save_value *c1;
  if (ev_eq(kind, "cd-track")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(kind));
    ev_set(o2, "track", ev_f_finite(reader, "track"));
    c1 = o2;
  } else {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(kind));
    ev_set(o3, "paused", ev_f_boolean(reader, "paused"));
    c1 = o3;
  }
  qa_save_value *result = c1;
  qa_save_value_free(kind);
  return result;
}

static qa_save_value *write_music_event(const qa_save_value *event) {
  qa_save_value *c1;
  if (ev_eq(ev_get(event, "kind"), "cd-track")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(event, "kind")));
    ev_set(o2, "track", ev_clone(ev_get(event, "track")));
    c1 = o2;
  } else {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(ev_get(event, "kind")));
    ev_set(o3, "paused", ev_clone(ev_get(event, "paused")));
    c1 = o3;
  }
  qa_save_value *result = c1;
  return result;
}

static qa_save_value *read_event_vec3(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "x", ev_f_finite(reader, "x"));
  ev_set(o1, "y", ev_f_finite(reader, "y"));
  ev_set(o1, "z", ev_f_finite(reader, "z"));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_vec3(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "x", ev_clone(ev_get(value, "x")));
  ev_set(o1, "y", ev_clone(ev_get(value, "y")));
  ev_set(o1, "z", ev_clone(ev_get(value, "z")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q1_event_message_args(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *result = ev_list(reader, read_event_q1_event_message_args_item_1, identity);
  return result;
}

static qa_save_value *write_event_q1_event_message_args(const qa_save_value *value) {
  qa_save_value *result = ev_map(value, ev_map_identity);
  return result;
}

static qa_save_value *read_event_q1_message_part(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "text", ev_f_string(reader, "text"));
  if (!ev_f_undefined(reader, "args")) {
    ev_set(o1, "args", ev_read(reader, "args", read_event_q1_event_message_args, identity));
  }
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_q1_message_part(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "text", ev_clone(ev_get(value, "text")));
  if (!ev_undefined_is(ev_get(value, "args"))) {
    ev_set(o1, "args", write_event_q1_event_message_args(ev_get(value, "args")));
  }
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q1_event_message_parts(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *result = ev_list(reader, read_event_q1_message_part, identity);
  return result;
}

static qa_save_value *write_event_q1_event_message_parts(const qa_save_value *value) {
  qa_save_value *result = ev_map(value, write_event_q1_message_part);
  return result;
}

static qa_save_value *read_event_q1_event_effect_actor(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *c1;
  if (ev_r_null(reader)) {
    c1 = ev_null();
  } else {
    c1 = read_actor_id(reader, identity);
  }
  qa_save_value *result = c1;
  return result;
}

static qa_save_value *write_event_q1_event_effect_actor(const qa_save_value *value) {
  qa_save_value *c1;
  if (ev_null_is(value)) {
    c1 = ev_null();
  } else {
    c1 = write_actor_id(value);
  }
  qa_save_value *result = c1;
  return result;
}

static qa_save_value *read_event_q1_event_colored_explosion(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"colored-explosion", 0}}, 1));
  ev_set(o1, "origin", ev_read(reader, "origin", read_event_vec3, identity));
  ev_set(o1, "colorStart", ev_f_finite(reader, "colorStart"));
  ev_set(o1, "colorLength", ev_f_finite(reader, "colorLength"));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_q1_event_colored_explosion(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "kind", ev_clone(ev_get(value, "kind")));
  ev_set(o1, "origin", write_event_vec3(ev_get(value, "origin")));
  ev_set(o1, "colorStart", ev_clone(ev_get(value, "colorStart")));
  ev_set(o1, "colorLength", ev_clone(ev_get(value, "colorLength")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q1_event_lightstyle(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"lightstyle", 0}}, 1));
  ev_set(o1, "style", ev_f_finite(reader, "style"));
  ev_set(o1, "pattern", ev_f_string(reader, "pattern"));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_q1_event_lightstyle(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "kind", ev_clone(ev_get(value, "kind")));
  ev_set(o1, "style", ev_clone(ev_get(value, "style")));
  ev_set(o1, "pattern", ev_clone(ev_get(value, "pattern")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q1_weapon(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *result = ev_r_choice(reader, (const ev_lit[]){{"axe", 0}, {"shotgun", 0}, {"supershotgun", 0}, {"nailgun", 0}, {"supernailgun", 0}, {"grenadelauncher", 0}, {"rocketlauncher", 0}, {"lightning", 0}, {"hipnotic:laser", 0}, {"hipnotic:mjolnir", 0}, {"hipnotic:proximity", 0}, {"rogue:lava-nailgun", 0}, {"rogue:lava-supernailgun", 0}, {"rogue:multi-grenade", 0}, {"rogue:multi-rocket", 0}, {"rogue:plasma", 0}, {"rogue:grapple", 0}, {"mg3:laser", 0}, {"mg3:mjolnir", 0}, {"ctf:grapple", 0}}, 20);
  return result;
}

static qa_save_value *write_event_q1_weapon(const qa_save_value *value) {
  qa_save_value *result = ev_clone(value);
  return result;
}

static qa_save_value *read_event_q1_character_attack(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "axe")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"axe", 0}}, 1));
    ev_set(o2, "variant", ev_f_choice(reader, "variant", (const ev_lit[]){{NULL, 0.0}, {NULL, 1.0}, {NULL, 2.0}, {NULL, 3.0}}, 4));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "shotgun") || ev_is(kind1, "rocket") || ev_is(kind1, "nail") || ev_is(kind1, "lightning")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_f_choice(reader, "kind", (const ev_lit[]){{"shotgun", 0}, {"rocket", 0}, {"nail", 0}, {"lightning", 0}}, 4));
    qa_save_value *result = o3;
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_q1_character_attack(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "axe")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o2, "variant", ev_clone(ev_get(value, "variant")));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "shotgun") || ev_is(kind1, "rocket") || ev_is(kind1, "nail") || ev_is(kind1, "lightning")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(ev_get(value, "kind")));
    qa_save_value *result = o3;
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_q1_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "stop-sound")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"stop-sound", 0}}, 1));
    ev_set(o2, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o2, "channel", ev_f_finite(reader, "channel"));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "sound")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"sound", 0}}, 1));
    if (!ev_f_undefined(reader, "origin")) {
      ev_set(o3, "origin", ev_read(reader, "origin", read_event_vec3, identity));
    }
    ev_set(o3, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o3, "path", ev_f_string(reader, "path"));
    ev_set(o3, "channel", ev_f_choice(reader, "channel", (const ev_lit[]){{"auto", 0}, {"weapon", 0}, {"voice", 0}, {"item", 0}, {"body", 0}, {NULL, -1.0}, {NULL, 5.0}, {NULL, 6.0}, {NULL, 7.0}}, 9));
    ev_set(o3, "attenuation", ev_f_finite(reader, "attenuation"));
    ev_set(o3, "volume", ev_f_finite(reader, "volume"));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "ambient")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"ambient", 0}}, 1));
    ev_set(o4, "origin", ev_read(reader, "origin", read_event_vec3, identity));
    ev_set(o4, "path", ev_f_string(reader, "path"));
    ev_set(o4, "volume", ev_f_finite(reader, "volume"));
    ev_set(o4, "attenuation", ev_f_finite(reader, "attenuation"));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "message")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"message", 0}}, 1));
    ev_set(o5, "player", ev_read(reader, "player", read_actor_id, identity));
    ev_set(o5, "text", ev_f_string(reader, "text"));
    ev_set(o5, "center", ev_f_boolean(reader, "center"));
    if (!ev_f_undefined(reader, "args")) {
      ev_set(o5, "args", ev_read(reader, "args", read_event_q1_event_message_args, identity));
    }
    if (!ev_f_undefined(reader, "parts")) {
      ev_set(o5, "parts", ev_read(reader, "parts", read_event_q1_event_message_parts, identity));
    }
    qa_save_value *result = o5;
    return result;
  }
  if (ev_is(kind1, "effect")) {
    qa_save_value *o6 = ev_object();
    ev_set(o6, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"effect", 0}}, 1));
    ev_set(o6, "effect", ev_f_choice(reader, "effect", (const ev_lit[]){{"blood", 0}, {"gunshot", 0}, {"spike", 0}, {"superspike", 0}, {"explosion", 0}, {"teleport", 0}, {"muzzleflash", 0}, {"pickup", 0}, {"lava-splash", 0}, {"tar-explosion", 0}, {"meat-spray", 0}, {"wizard-spike", 0}, {"knight-spike", 0}}, 13));
    ev_set(o6, "actor", ev_read(reader, "actor", read_event_q1_event_effect_actor, identity));
    ev_set(o6, "origin", ev_read(reader, "origin", read_event_vec3, identity));
    ev_set(o6, "amount", ev_f_finite(reader, "amount"));
    if (!ev_f_undefined(reader, "muzzle")) {
      qa_save_value *o7 = ev_object();
      qa_save_reader f8;
      ev_field(reader, "muzzle", &f8);
      ev_set(o7, "origin", ev_read(&f8, "origin", read_event_vec3, identity));
      qa_save_reader f9;
      ev_field(reader, "muzzle", &f9);
      ev_set(o7, "angles", ev_read(&f9, "angles", read_event_vec3, identity));
      ev_set(o6, "muzzle", o7);
      qa_save_reader_destroy(&f9);
      qa_save_reader_destroy(&f8);
    }
    qa_save_value *result = o6;
    return result;
  }
  if (ev_is(kind1, "colored-explosion")) {
    qa_save_value *result = read_event_q1_event_colored_explosion(reader, identity);
    return result;
  }
  if (ev_is(kind1, "static-model")) {
    qa_save_value *o10 = ev_object();
    ev_set(o10, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"static-model", 0}}, 1));
    ev_set(o10, "path", ev_f_string(reader, "path"));
    ev_set(o10, "frame", ev_f_finite(reader, "frame"));
    ev_set(o10, "colorMap", ev_f_finite(reader, "colorMap"));
    ev_set(o10, "skin", ev_f_finite(reader, "skin"));
    ev_set(o10, "origin", ev_read(reader, "origin", read_event_vec3, identity));
    ev_set(o10, "angles", ev_read(reader, "angles", read_event_vec3, identity));
    qa_save_value *result = o10;
    return result;
  }
  if (ev_is(kind1, "particles")) {
    qa_save_value *o11 = ev_object();
    ev_set(o11, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"particles", 0}}, 1));
    ev_set(o11, "origin", ev_read(reader, "origin", read_event_vec3, identity));
    ev_set(o11, "direction", ev_read(reader, "direction", read_event_vec3, identity));
    ev_set(o11, "color", ev_f_finite(reader, "color"));
    ev_set(o11, "count", ev_f_finite(reader, "count"));
    qa_save_value *result = o11;
    return result;
  }
  if (ev_is(kind1, "server-command")) {
    qa_save_value *o12 = ev_object();
    ev_set(o12, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"server-command", 0}}, 1));
    ev_set(o12, "text", ev_f_string(reader, "text"));
    qa_save_value *result = o12;
    return result;
  }
  if (ev_is(kind1, "camera")) {
    qa_save_value *o13 = ev_object();
    ev_set(o13, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"camera", 0}}, 1));
    ev_set(o13, "player", ev_read(reader, "player", read_actor_id, identity));
    ev_set(o13, "origin", ev_read(reader, "origin", read_event_vec3, identity));
    ev_set(o13, "angles", ev_read(reader, "angles", read_event_vec3, identity));
    if (!ev_f_undefined(reader, "viewOffset")) {
      ev_set(o13, "viewOffset", ev_read(reader, "viewOffset", read_event_vec3, identity));
    }
    qa_save_value *result = o13;
    return result;
  }
  if (ev_is(kind1, "beam")) {
    qa_save_value *o14 = ev_object();
    ev_set(o14, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"beam", 0}}, 1));
    ev_set(o14, "style", ev_f_choice(reader, "style", (const ev_lit[]){{"lightning1", 0}, {"lightning2", 0}, {"lightning3", 0}, {"grapple", 0}}, 4));
    ev_set(o14, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o14, "start", ev_read(reader, "start", read_event_vec3, identity));
    ev_set(o14, "end", ev_read(reader, "end", read_event_vec3, identity));
    qa_save_value *result = o14;
    return result;
  }
  if (ev_is(kind1, "lightstyle")) {
    qa_save_value *result = read_event_q1_event_lightstyle(reader, identity);
    return result;
  }
  if (ev_is(kind1, "monster-total")) {
    qa_save_value *o15 = ev_object();
    ev_set(o15, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"monster-total", 0}}, 1));
    ev_set(o15, "total", ev_f_finite(reader, "total"));
    qa_save_value *result = o15;
    return result;
  }
  if (ev_is(kind1, "secret") || ev_is(kind1, "monster-killed")) {
    qa_save_value *o16 = ev_object();
    ev_set(o16, "kind", ev_f_choice(reader, "kind", (const ev_lit[]){{"secret", 0}, {"monster-killed", 0}}, 2));
    ev_set(o16, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o16, "total", ev_f_finite(reader, "total"));
    ev_set(o16, "found", ev_f_finite(reader, "found"));
    qa_save_value *result = o16;
    return result;
  }
  if (ev_is(kind1, "weapon")) {
    qa_save_value *o17 = ev_object();
    ev_set(o17, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"weapon", 0}}, 1));
    ev_set(o17, "player", ev_read(reader, "player", read_actor_id, identity));
    ev_set(o17, "weapon", ev_read(reader, "weapon", read_event_q1_weapon, identity));
    ev_set(o17, "viewModel", ev_f_string(reader, "viewModel"));
    ev_set(o17, "frame", ev_f_finite(reader, "frame"));
    ev_set(o17, "punch", ev_f_finite(reader, "punch"));
    if (!ev_f_undefined(reader, "attack")) {
      ev_set(o17, "attack", ev_read(reader, "attack", read_event_q1_character_attack, identity));
    }
    qa_save_value *result = o17;
    return result;
  }
  if (ev_is(kind1, "teleport-player")) {
    qa_save_value *o18 = ev_object();
    ev_set(o18, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"teleport-player", 0}}, 1));
    ev_set(o18, "player", ev_read(reader, "player", read_actor_id, identity));
    ev_set(o18, "angles", ev_read(reader, "angles", read_event_vec3, identity));
    ev_set(o18, "lockUntil", ev_f_finite(reader, "lockUntil"));
    qa_save_value *result = o18;
    return result;
  }
  if (ev_is(kind1, "powerup")) {
    qa_save_value *o19 = ev_object();
    ev_set(o19, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"powerup", 0}}, 1));
    ev_set(o19, "player", ev_read(reader, "player", read_actor_id, identity));
    ev_set(o19, "powerup", ev_f_choice(reader, "powerup", (const ev_lit[]){{"quad", 0}, {"invulnerability", 0}, {"invisibility", 0}, {"suit", 0}, {"hipnotic:wetsuit", 0}, {"hipnotic:empathy", 0}, {"rogue:shield", 0}, {"rogue:antigrav", 0}, {"mg3:lavasuit", 0}, {"q3:haste", 0}, {"q3:regen", 0}, {"q3:flight", 0}}, 12));
    ev_set(o19, "expires", ev_f_finite(reader, "expires"));
    qa_save_value *result = o19;
    return result;
  }
  if (ev_is(kind1, "intermission")) {
    qa_save_value *o20 = ev_object();
    ev_set(o20, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"intermission", 0}}, 1));
    ev_set(o20, "origin", ev_read(reader, "origin", read_event_vec3, identity));
    ev_set(o20, "angles", ev_read(reader, "angles", read_event_vec3, identity));
    ev_set(o20, "map", ev_f_string(reader, "map"));
    ev_set(o20, "exitAfter", ev_f_finite(reader, "exitAfter"));
    ev_set(o20, "track", ev_f_finite(reader, "track"));
    qa_save_value *result = o20;
    return result;
  }
  if (ev_is(kind1, "finale")) {
    qa_save_value *o21 = ev_object();
    ev_set(o21, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"finale", 0}}, 1));
    ev_set(o21, "text", ev_f_string(reader, "text"));
    ev_set(o21, "stage", ev_f_choice(reader, "stage", (const ev_lit[]){{NULL, 1.0}, {NULL, 2.0}, {NULL, 3.0}, {NULL, 4.0}, {NULL, 5.0}, {NULL, 6.0}}, 6));
    qa_save_value *result = o21;
    return result;
  }
  if (ev_is(kind1, "achievement")) {
    qa_save_value *o22 = ev_object();
    ev_set(o22, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"achievement", 0}}, 1));
    ev_set(o22, "player", ev_read(reader, "player", read_event_q1_event_effect_actor, identity));
    ev_set(o22, "id", ev_f_string(reader, "id"));
    qa_save_value *result = o22;
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_q1_event(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "stop-sound")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o2, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o2, "channel", ev_clone(ev_get(value, "channel")));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "sound")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(ev_get(value, "kind")));
    if (!ev_undefined_is(ev_get(value, "origin"))) {
      ev_set(o3, "origin", write_event_vec3(ev_get(value, "origin")));
    }
    ev_set(o3, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o3, "path", ev_clone(ev_get(value, "path")));
    ev_set(o3, "channel", ev_clone(ev_get(value, "channel")));
    ev_set(o3, "attenuation", ev_clone(ev_get(value, "attenuation")));
    ev_set(o3, "volume", ev_clone(ev_get(value, "volume")));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "ambient")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o4, "origin", write_event_vec3(ev_get(value, "origin")));
    ev_set(o4, "path", ev_clone(ev_get(value, "path")));
    ev_set(o4, "volume", ev_clone(ev_get(value, "volume")));
    ev_set(o4, "attenuation", ev_clone(ev_get(value, "attenuation")));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "message")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o5, "player", write_actor_id(ev_get(value, "player")));
    ev_set(o5, "text", ev_clone(ev_get(value, "text")));
    ev_set(o5, "center", ev_clone(ev_get(value, "center")));
    if (!ev_undefined_is(ev_get(value, "args"))) {
      ev_set(o5, "args", write_event_q1_event_message_args(ev_get(value, "args")));
    }
    if (!ev_undefined_is(ev_get(value, "parts"))) {
      ev_set(o5, "parts", write_event_q1_event_message_parts(ev_get(value, "parts")));
    }
    qa_save_value *result = o5;
    return result;
  }
  if (ev_is(kind1, "effect")) {
    qa_save_value *o6 = ev_object();
    ev_set(o6, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o6, "effect", ev_clone(ev_get(value, "effect")));
    ev_set(o6, "actor", write_event_q1_event_effect_actor(ev_get(value, "actor")));
    ev_set(o6, "origin", write_event_vec3(ev_get(value, "origin")));
    ev_set(o6, "amount", ev_clone(ev_get(value, "amount")));
    if (!ev_undefined_is(ev_get(value, "muzzle"))) {
      qa_save_value *o7 = ev_object();
      ev_set(o7, "origin", write_event_vec3(ev_get(ev_get(value, "muzzle"), "origin")));
      ev_set(o7, "angles", write_event_vec3(ev_get(ev_get(value, "muzzle"), "angles")));
      ev_set(o6, "muzzle", o7);
    }
    qa_save_value *result = o6;
    return result;
  }
  if (ev_is(kind1, "colored-explosion")) {
    qa_save_value *result = write_event_q1_event_colored_explosion(value);
    return result;
  }
  if (ev_is(kind1, "static-model")) {
    qa_save_value *o8 = ev_object();
    ev_set(o8, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o8, "path", ev_clone(ev_get(value, "path")));
    ev_set(o8, "frame", ev_clone(ev_get(value, "frame")));
    ev_set(o8, "colorMap", ev_clone(ev_get(value, "colorMap")));
    ev_set(o8, "skin", ev_clone(ev_get(value, "skin")));
    ev_set(o8, "origin", write_event_vec3(ev_get(value, "origin")));
    ev_set(o8, "angles", write_event_vec3(ev_get(value, "angles")));
    qa_save_value *result = o8;
    return result;
  }
  if (ev_is(kind1, "particles")) {
    qa_save_value *o9 = ev_object();
    ev_set(o9, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o9, "origin", write_event_vec3(ev_get(value, "origin")));
    ev_set(o9, "direction", write_event_vec3(ev_get(value, "direction")));
    ev_set(o9, "color", ev_clone(ev_get(value, "color")));
    ev_set(o9, "count", ev_clone(ev_get(value, "count")));
    qa_save_value *result = o9;
    return result;
  }
  if (ev_is(kind1, "server-command")) {
    qa_save_value *o10 = ev_object();
    ev_set(o10, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o10, "text", ev_clone(ev_get(value, "text")));
    qa_save_value *result = o10;
    return result;
  }
  if (ev_is(kind1, "camera")) {
    qa_save_value *o11 = ev_object();
    ev_set(o11, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o11, "player", write_actor_id(ev_get(value, "player")));
    ev_set(o11, "origin", write_event_vec3(ev_get(value, "origin")));
    ev_set(o11, "angles", write_event_vec3(ev_get(value, "angles")));
    if (!ev_undefined_is(ev_get(value, "viewOffset"))) {
      ev_set(o11, "viewOffset", write_event_vec3(ev_get(value, "viewOffset")));
    }
    qa_save_value *result = o11;
    return result;
  }
  if (ev_is(kind1, "beam")) {
    qa_save_value *o12 = ev_object();
    ev_set(o12, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o12, "style", ev_clone(ev_get(value, "style")));
    ev_set(o12, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o12, "start", write_event_vec3(ev_get(value, "start")));
    ev_set(o12, "end", write_event_vec3(ev_get(value, "end")));
    qa_save_value *result = o12;
    return result;
  }
  if (ev_is(kind1, "lightstyle")) {
    qa_save_value *result = write_event_q1_event_lightstyle(value);
    return result;
  }
  if (ev_is(kind1, "monster-total")) {
    qa_save_value *o13 = ev_object();
    ev_set(o13, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o13, "total", ev_clone(ev_get(value, "total")));
    qa_save_value *result = o13;
    return result;
  }
  if (ev_is(kind1, "secret") || ev_is(kind1, "monster-killed")) {
    qa_save_value *o14 = ev_object();
    ev_set(o14, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o14, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o14, "total", ev_clone(ev_get(value, "total")));
    ev_set(o14, "found", ev_clone(ev_get(value, "found")));
    qa_save_value *result = o14;
    return result;
  }
  if (ev_is(kind1, "weapon")) {
    qa_save_value *o15 = ev_object();
    ev_set(o15, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o15, "player", write_actor_id(ev_get(value, "player")));
    ev_set(o15, "weapon", write_event_q1_weapon(ev_get(value, "weapon")));
    ev_set(o15, "viewModel", ev_clone(ev_get(value, "viewModel")));
    ev_set(o15, "frame", ev_clone(ev_get(value, "frame")));
    ev_set(o15, "punch", ev_clone(ev_get(value, "punch")));
    if (!ev_undefined_is(ev_get(value, "attack"))) {
      ev_set(o15, "attack", write_event_q1_character_attack(ev_get(value, "attack")));
    }
    qa_save_value *result = o15;
    return result;
  }
  if (ev_is(kind1, "teleport-player")) {
    qa_save_value *o16 = ev_object();
    ev_set(o16, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o16, "player", write_actor_id(ev_get(value, "player")));
    ev_set(o16, "angles", write_event_vec3(ev_get(value, "angles")));
    ev_set(o16, "lockUntil", ev_clone(ev_get(value, "lockUntil")));
    qa_save_value *result = o16;
    return result;
  }
  if (ev_is(kind1, "powerup")) {
    qa_save_value *o17 = ev_object();
    ev_set(o17, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o17, "player", write_actor_id(ev_get(value, "player")));
    ev_set(o17, "powerup", ev_clone(ev_get(value, "powerup")));
    ev_set(o17, "expires", ev_clone(ev_get(value, "expires")));
    qa_save_value *result = o17;
    return result;
  }
  if (ev_is(kind1, "intermission")) {
    qa_save_value *o18 = ev_object();
    ev_set(o18, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o18, "origin", write_event_vec3(ev_get(value, "origin")));
    ev_set(o18, "angles", write_event_vec3(ev_get(value, "angles")));
    ev_set(o18, "map", ev_clone(ev_get(value, "map")));
    ev_set(o18, "exitAfter", ev_clone(ev_get(value, "exitAfter")));
    ev_set(o18, "track", ev_clone(ev_get(value, "track")));
    qa_save_value *result = o18;
    return result;
  }
  if (ev_is(kind1, "finale")) {
    qa_save_value *o19 = ev_object();
    ev_set(o19, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o19, "text", ev_clone(ev_get(value, "text")));
    ev_set(o19, "stage", ev_clone(ev_get(value, "stage")));
    qa_save_value *result = o19;
    return result;
  }
  if (ev_is(kind1, "achievement")) {
    qa_save_value *o20 = ev_object();
    ev_set(o20, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o20, "player", write_event_q1_event_effect_actor(ev_get(value, "player")));
    ev_set(o20, "id", ev_clone(ev_get(value, "id")));
    qa_save_value *result = o20;
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_simulation_presentation_event_q1_source_entity(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *c1;
  if (ev_r_null(reader)) {
    c1 = ev_null();
  } else {
    c1 = ev_r_finite(reader);
  }
  qa_save_value *result = c1;
  return result;
}

static qa_save_value *write_event_simulation_presentation_event_q1_source_entity(const qa_save_value *value) {
  qa_save_value *c1;
  if (ev_null_is(value)) {
    c1 = ev_null();
  } else {
    c1 = ev_clone(value);
  }
  qa_save_value *result = c1;
  return result;
}

static qa_save_value *read_event_q1_fog(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "density", ev_f_finite(reader, "density"));
  ev_set(o1, "color", ev_read(reader, "color", read_event_vec3, identity));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_q1_fog(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "density", ev_clone(ev_get(value, "density")));
  ev_set(o1, "color", write_event_vec3(ev_get(value, "color")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q1_fog_transition(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "previous", ev_read(reader, "previous", read_event_q1_fog, identity));
  ev_set(o1, "target", ev_read(reader, "target", read_event_q1_fog, identity));
  ev_set(o1, "start", ev_f_finite(reader, "start"));
  ev_set(o1, "duration", ev_f_finite(reader, "duration"));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_q1_fog_transition(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "previous", write_event_q1_fog(ev_get(value, "previous")));
  ev_set(o1, "target", write_event_q1_fog(ev_get(value, "target")));
  ev_set(o1, "start", ev_clone(ev_get(value, "start")));
  ev_set(o1, "duration", ev_clone(ev_get(value, "duration")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q1_addon_event_sell_screen(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"sell-screen", 0}}, 1));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_q1_addon_event_sell_screen(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "kind", ev_clone(ev_get(value, "kind")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q1_addon_event_alpha(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"alpha", 0}}, 1));
  ev_set(o1, "actor", ev_read(reader, "actor", read_actor_id, identity));
  ev_set(o1, "alpha", ev_f_finite(reader, "alpha"));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_q1_addon_event_alpha(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "kind", ev_clone(ev_get(value, "kind")));
  ev_set(o1, "actor", write_actor_id(ev_get(value, "actor")));
  ev_set(o1, "alpha", ev_clone(ev_get(value, "alpha")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q1_addon_event_developer_message(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"developer-message", 0}}, 1));
  ev_set(o1, "text", ev_f_string(reader, "text"));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_q1_addon_event_developer_message(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "kind", ev_clone(ev_get(value, "kind")));
  ev_set(o1, "text", ev_clone(ev_get(value, "text")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q1_addon_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "music")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"music", 0}}, 1));
    ev_set(o2, "track", ev_f_finite(reader, "track"));
    ev_set(o2, "loopTrack", ev_f_finite(reader, "loopTrack"));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "sell-screen")) {
    qa_save_value *result = read_event_q1_addon_event_sell_screen(reader, identity);
    return result;
  }
  if (ev_is(kind1, "alpha")) {
    qa_save_value *result = read_event_q1_addon_event_alpha(reader, identity);
    return result;
  }
  if (ev_is(kind1, "rune-collected")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"rune-collected", 0}}, 1));
    ev_set(o3, "player", ev_read(reader, "player", read_actor_id, identity));
    ev_set(o3, "bits", ev_f_finite(reader, "bits"));
    ev_set(o3, "program", ev_f_choice(reader, "program", (const ev_lit[]){{"dopa", 0}, {"mg1", 0}, {"mg3", 0}, {"ctf", 0}}, 4));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "cutscene")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"cutscene", 0}}, 1));
    ev_set(o4, "camera", ev_read(reader, "camera", read_event_vec3, identity));
    ev_set(o4, "angles", ev_read(reader, "angles", read_event_vec3, identity));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "fog")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"fog", 0}}, 1));
    ev_set(o5, "player", ev_read(reader, "player", read_event_q1_event_effect_actor, identity));
    ev_set(o5, "density", ev_f_finite(reader, "density"));
    ev_set(o5, "color", ev_read(reader, "color", read_event_vec3, identity));
    ev_set(o5, "skyFactor", ev_f_finite(reader, "skyFactor"));
    ev_set(o5, "duration", ev_f_finite(reader, "duration"));
    qa_save_value *result = o5;
    return result;
  }
  if (ev_is(kind1, "punch-angle")) {
    qa_save_value *o6 = ev_object();
    ev_set(o6, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"punch-angle", 0}}, 1));
    ev_set(o6, "player", ev_read(reader, "player", read_actor_id, identity));
    ev_set(o6, "angles", ev_read(reader, "angles", read_event_vec3, identity));
    qa_save_value *result = o6;
    return result;
  }
  if (ev_is(kind1, "view-roll")) {
    qa_save_value *o7 = ev_object();
    ev_set(o7, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"view-roll", 0}}, 1));
    ev_set(o7, "player", ev_read(reader, "player", read_actor_id, identity));
    ev_set(o7, "roll", ev_f_finite(reader, "roll"));
    qa_save_value *result = o7;
    return result;
  }
  if (ev_is(kind1, "lightning")) {
    qa_save_value *o8 = ev_object();
    ev_set(o8, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"lightning", 0}}, 1));
    ev_set(o8, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o8, "style", ev_f_choice(reader, "style", (const ev_lit[]){{NULL, 1.0}, {NULL, 2.0}, {NULL, 3.0}}, 3));
    ev_set(o8, "start", ev_read(reader, "start", read_event_vec3, identity));
    ev_set(o8, "end", ev_read(reader, "end", read_event_vec3, identity));
    qa_save_value *result = o8;
    return result;
  }
  if (ev_is(kind1, "colored-explosion")) {
    qa_save_value *result = read_event_q1_event_colored_explosion(reader, identity);
    return result;
  }
  if (ev_is(kind1, "monster-count")) {
    qa_save_value *o9 = ev_object();
    ev_set(o9, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"monster-count", 0}}, 1));
    ev_set(o9, "count", ev_f_finite(reader, "count"));
    qa_save_value *result = o9;
    return result;
  }
  if (ev_is(kind1, "developer-message")) {
    qa_save_value *result = read_event_q1_addon_event_developer_message(reader, identity);
    return result;
  }
  if (ev_is(kind1, "actor-effects")) {
    qa_save_value *o10 = ev_object();
    ev_set(o10, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"actor-effects", 0}}, 1));
    ev_set(o10, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o10, "effects", ev_f_finite(reader, "effects"));
    qa_save_value *result = o10;
    return result;
  }
  if (ev_is(kind1, "debug-bounds")) {
    qa_save_value *o11 = ev_object();
    ev_set(o11, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"debug-bounds", 0}}, 1));
    ev_set(o11, "min", ev_read(reader, "min", read_event_vec3, identity));
    ev_set(o11, "max", ev_read(reader, "max", read_event_vec3, identity));
    ev_set(o11, "color", ev_f_finite(reader, "color"));
    ev_set(o11, "lifetime", ev_f_finite(reader, "lifetime"));
    ev_set(o11, "depthTest", ev_f_boolean(reader, "depthTest"));
    qa_save_value *result = o11;
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_q1_addon_event(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "music")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o2, "track", ev_clone(ev_get(value, "track")));
    ev_set(o2, "loopTrack", ev_clone(ev_get(value, "loopTrack")));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "sell-screen")) {
    qa_save_value *result = write_event_q1_addon_event_sell_screen(value);
    return result;
  }
  if (ev_is(kind1, "alpha")) {
    qa_save_value *result = write_event_q1_addon_event_alpha(value);
    return result;
  }
  if (ev_is(kind1, "rune-collected")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o3, "player", write_actor_id(ev_get(value, "player")));
    ev_set(o3, "bits", ev_clone(ev_get(value, "bits")));
    ev_set(o3, "program", ev_clone(ev_get(value, "program")));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "cutscene")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o4, "camera", write_event_vec3(ev_get(value, "camera")));
    ev_set(o4, "angles", write_event_vec3(ev_get(value, "angles")));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "fog")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o5, "player", write_event_q1_event_effect_actor(ev_get(value, "player")));
    ev_set(o5, "density", ev_clone(ev_get(value, "density")));
    ev_set(o5, "color", write_event_vec3(ev_get(value, "color")));
    ev_set(o5, "skyFactor", ev_clone(ev_get(value, "skyFactor")));
    ev_set(o5, "duration", ev_clone(ev_get(value, "duration")));
    qa_save_value *result = o5;
    return result;
  }
  if (ev_is(kind1, "punch-angle")) {
    qa_save_value *o6 = ev_object();
    ev_set(o6, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o6, "player", write_actor_id(ev_get(value, "player")));
    ev_set(o6, "angles", write_event_vec3(ev_get(value, "angles")));
    qa_save_value *result = o6;
    return result;
  }
  if (ev_is(kind1, "view-roll")) {
    qa_save_value *o7 = ev_object();
    ev_set(o7, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o7, "player", write_actor_id(ev_get(value, "player")));
    ev_set(o7, "roll", ev_clone(ev_get(value, "roll")));
    qa_save_value *result = o7;
    return result;
  }
  if (ev_is(kind1, "lightning")) {
    qa_save_value *o8 = ev_object();
    ev_set(o8, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o8, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o8, "style", ev_clone(ev_get(value, "style")));
    ev_set(o8, "start", write_event_vec3(ev_get(value, "start")));
    ev_set(o8, "end", write_event_vec3(ev_get(value, "end")));
    qa_save_value *result = o8;
    return result;
  }
  if (ev_is(kind1, "colored-explosion")) {
    qa_save_value *result = write_event_q1_event_colored_explosion(value);
    return result;
  }
  if (ev_is(kind1, "monster-count")) {
    qa_save_value *o9 = ev_object();
    ev_set(o9, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o9, "count", ev_clone(ev_get(value, "count")));
    qa_save_value *result = o9;
    return result;
  }
  if (ev_is(kind1, "developer-message")) {
    qa_save_value *result = write_event_q1_addon_event_developer_message(value);
    return result;
  }
  if (ev_is(kind1, "actor-effects")) {
    qa_save_value *o10 = ev_object();
    ev_set(o10, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o10, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o10, "effects", ev_clone(ev_get(value, "effects")));
    qa_save_value *result = o10;
    return result;
  }
  if (ev_is(kind1, "debug-bounds")) {
    qa_save_value *o11 = ev_object();
    ev_set(o11, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o11, "min", write_event_vec3(ev_get(value, "min")));
    ev_set(o11, "max", write_event_vec3(ev_get(value, "max")));
    ev_set(o11, "color", ev_clone(ev_get(value, "color")));
    ev_set(o11, "lifetime", ev_clone(ev_get(value, "lifetime")));
    ev_set(o11, "depthTest", ev_clone(ev_get(value, "depthTest")));
    qa_save_value *result = o11;
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_q1_client_snapshot_userinfo_item(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "key", ev_f_string(reader, "key"));
  ev_set(o1, "value", ev_f_string(reader, "value"));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_q1_client_snapshot_userinfo_item(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "key", ev_clone(ev_get(value, "key")));
  ev_set(o1, "value", ev_clone(ev_get(value, "value")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q1_client_snapshot_userinfo(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *result = ev_list(reader, read_event_q1_client_snapshot_userinfo_item, identity);
  return result;
}

static qa_save_value *write_event_q1_client_snapshot_userinfo(const qa_save_value *value) {
  qa_save_value *result = ev_map(value, write_event_q1_client_snapshot_userinfo_item);
  return result;
}

static qa_save_value *read_event_q1_client_snapshot(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "actor", ev_read(reader, "actor", read_actor_id, identity));
  ev_set(o1, "slot", ev_f_finite(reader, "slot"));
  ev_set(o1, "name", ev_f_string(reader, "name"));
  ev_set(o1, "frags", ev_f_finite(reader, "frags"));
  ev_set(o1, "shirt", ev_f_finite(reader, "shirt"));
  ev_set(o1, "pants", ev_f_finite(reader, "pants"));
  ev_set(o1, "team", ev_f_finite(reader, "team"));
  ev_set(o1, "observer", ev_f_boolean(reader, "observer"));
  ev_set(o1, "noTarget", ev_f_boolean(reader, "noTarget"));
  ev_set(o1, "userinfo", ev_read(reader, "userinfo", read_event_q1_client_snapshot_userinfo, identity));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_q1_client_snapshot(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "actor", write_actor_id(ev_get(value, "actor")));
  ev_set(o1, "slot", ev_clone(ev_get(value, "slot")));
  ev_set(o1, "name", ev_clone(ev_get(value, "name")));
  ev_set(o1, "frags", ev_clone(ev_get(value, "frags")));
  ev_set(o1, "shirt", ev_clone(ev_get(value, "shirt")));
  ev_set(o1, "pants", ev_clone(ev_get(value, "pants")));
  ev_set(o1, "team", ev_clone(ev_get(value, "team")));
  ev_set(o1, "observer", ev_clone(ev_get(value, "observer")));
  ev_set(o1, "noTarget", ev_clone(ev_get(value, "noTarget")));
  ev_set(o1, "userinfo", write_event_q1_client_snapshot_userinfo(ev_get(value, "userinfo")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_ctf_status(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "red", ev_f_finite(reader, "red"));
  ev_set(o1, "blue", ev_f_finite(reader, "blue"));
  ev_set(o1, "flags", ev_f_finite(reader, "flags"));
  ev_set(o1, "runeItems", ev_f_finite(reader, "runeItems"));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_ctf_status(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "red", ev_clone(ev_get(value, "red")));
  ev_set(o1, "blue", ev_clone(ev_get(value, "blue")));
  ev_set(o1, "flags", ev_clone(ev_get(value, "flags")));
  ev_set(o1, "runeItems", ev_clone(ev_get(value, "runeItems")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q1_composition_event_prompt_choices_item(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "label", ev_f_string(reader, "label"));
  ev_set(o1, "impulse", ev_f_finite(reader, "impulse"));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_q1_composition_event_prompt_choices_item(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "label", ev_clone(ev_get(value, "label")));
  ev_set(o1, "impulse", ev_clone(ev_get(value, "impulse")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q1_composition_event_prompt_choices(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *result = ev_list(reader, read_event_q1_composition_event_prompt_choices_item, identity);
  return result;
}

static qa_save_value *write_event_q1_composition_event_prompt_choices(const qa_save_value *value) {
  qa_save_value *result = ev_map(value, write_event_q1_composition_event_prompt_choices_item);
  return result;
}

static qa_save_value *read_event_q1_source_finale_finale(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"finale", 0}}, 1));
  ev_set(o1, "text", ev_f_string(reader, "text"));
  ev_set(o1, "track", ev_f_finite(reader, "track"));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_q1_source_finale_finale(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "kind", ev_clone(ev_get(value, "kind")));
  ev_set(o1, "text", ev_clone(ev_get(value, "text")));
  ev_set(o1, "track", ev_clone(ev_get(value, "track")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q1_source_finale(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "finale")) {
    qa_save_value *result = read_event_q1_source_finale_finale(reader, identity);
    return result;
  }
  if (ev_is(kind1, "sell-screen")) {
    qa_save_value *result = read_event_q1_addon_event_sell_screen(reader, identity);
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_q1_source_finale(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "finale")) {
    qa_save_value *result = write_event_q1_source_finale_finale(value);
    return result;
  }
  if (ev_is(kind1, "sell-screen")) {
    qa_save_value *result = write_event_q1_addon_event_sell_screen(value);
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_q1_composition_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "addon")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"addon", 0}}, 1));
    ev_set(o2, "event", ev_read(reader, "event", read_event_q1_addon_event, identity));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "client")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"client", 0}}, 1));
    ev_set(o3, "client", ev_read(reader, "client", read_event_q1_client_snapshot, identity));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "client-left")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"client-left", 0}}, 1));
    ev_set(o4, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o4, "slot", ev_f_finite(reader, "slot"));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "ctf-status")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"ctf-status", 0}}, 1));
    ev_set(o5, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o5, "status", ev_read(reader, "status", read_event_ctf_status, identity));
    qa_save_value *result = o5;
    return result;
  }
  if (ev_is(kind1, "ctf-capture")) {
    qa_save_value *o6 = ev_object();
    ev_set(o6, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"ctf-capture", 0}}, 1));
    ev_set(o6, "team", ev_f_choice(reader, "team", (const ev_lit[]){{"red", 0}, {"blue", 0}}, 2));
    ev_set(o6, "total", ev_f_finite(reader, "total"));
    qa_save_value *result = o6;
    return result;
  }
  if (ev_is(kind1, "prompt")) {
    qa_save_value *o7 = ev_object();
    ev_set(o7, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"prompt", 0}}, 1));
    ev_set(o7, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o7, "title", ev_f_string(reader, "title"));
    ev_set(o7, "choices", ev_read(reader, "choices", read_event_q1_composition_event_prompt_choices, identity));
    qa_save_value *result = o7;
    return result;
  }
  if (ev_is(kind1, "clear-prompt")) {
    qa_save_value *o8 = ev_object();
    ev_set(o8, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"clear-prompt", 0}}, 1));
    ev_set(o8, "actor", ev_read(reader, "actor", read_actor_id, identity));
    qa_save_value *result = o8;
    return result;
  }
  if (ev_is(kind1, "source-log")) {
    qa_save_value *o9 = ev_object();
    ev_set(o9, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"source-log", 0}}, 1));
    ev_set(o9, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o9, "action", ev_f_string(reader, "action"));
    qa_save_value *result = o9;
    return result;
  }
  if (ev_is(kind1, "developer-message")) {
    qa_save_value *result = read_event_q1_addon_event_developer_message(reader, identity);
    return result;
  }
  if (ev_is(kind1, "level-presentation")) {
    qa_save_value *o10 = ev_object();
    ev_set(o10, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"level-presentation", 0}}, 1));
    ev_set(o10, "event", ev_read(reader, "event", read_event_q1_source_finale, identity));
    qa_save_value *result = o10;
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_q1_composition_event(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "addon")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o2, "event", write_event_q1_addon_event(ev_get(value, "event")));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "client")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o3, "client", write_event_q1_client_snapshot(ev_get(value, "client")));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "client-left")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o4, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o4, "slot", ev_clone(ev_get(value, "slot")));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "ctf-status")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o5, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o5, "status", write_event_ctf_status(ev_get(value, "status")));
    qa_save_value *result = o5;
    return result;
  }
  if (ev_is(kind1, "ctf-capture")) {
    qa_save_value *o6 = ev_object();
    ev_set(o6, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o6, "team", ev_clone(ev_get(value, "team")));
    ev_set(o6, "total", ev_clone(ev_get(value, "total")));
    qa_save_value *result = o6;
    return result;
  }
  if (ev_is(kind1, "prompt")) {
    qa_save_value *o7 = ev_object();
    ev_set(o7, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o7, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o7, "title", ev_clone(ev_get(value, "title")));
    ev_set(o7, "choices", write_event_q1_composition_event_prompt_choices(ev_get(value, "choices")));
    qa_save_value *result = o7;
    return result;
  }
  if (ev_is(kind1, "clear-prompt")) {
    qa_save_value *o8 = ev_object();
    ev_set(o8, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o8, "actor", write_actor_id(ev_get(value, "actor")));
    qa_save_value *result = o8;
    return result;
  }
  if (ev_is(kind1, "source-log")) {
    qa_save_value *o9 = ev_object();
    ev_set(o9, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o9, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o9, "action", ev_clone(ev_get(value, "action")));
    qa_save_value *result = o9;
    return result;
  }
  if (ev_is(kind1, "developer-message")) {
    qa_save_value *result = write_event_q1_addon_event_developer_message(value);
    return result;
  }
  if (ev_is(kind1, "level-presentation")) {
    qa_save_value *o10 = ev_object();
    ev_set(o10, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o10, "event", write_event_q1_source_finale(ev_get(value, "event")));
    qa_save_value *result = o10;
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_q1_intermission_result(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "waiting")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"waiting", 0}}, 1));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "travel")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"travel", 0}}, 1));
    ev_set(o3, "map", ev_f_string(reader, "map"));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "finale")) {
    qa_save_value *result = read_event_q1_source_finale_finale(reader, identity);
    return result;
  }
  if (ev_is(kind1, "sell-screen")) {
    qa_save_value *result = read_event_q1_addon_event_sell_screen(reader, identity);
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_q1_intermission_result(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "waiting")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "travel")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o3, "map", ev_clone(ev_get(value, "map")));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "finale")) {
    qa_save_value *result = write_event_q1_source_finale_finale(value);
    return result;
  }
  if (ev_is(kind1, "sell-screen")) {
    qa_save_value *result = write_event_q1_addon_event_sell_screen(value);
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_q2_presentation_event_model_attached_models(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *result = ev_list(reader, read_event_q2_presentation_event_model_attached_models_item_2, identity);
  return result;
}

static qa_save_value *write_event_q2_presentation_event_model_attached_models(const qa_save_value *value) {
  qa_save_value *result = ev_map(value, ev_map_identity);
  return result;
}

static qa_save_value *read_event_q2_presentation_event_dynamic_light_cone_value(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "direction", ev_read(reader, "direction", read_event_vec3, identity));
  ev_set(o1, "cosHalfAngle", ev_f_finite(reader, "cosHalfAngle"));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_q2_presentation_event_dynamic_light_cone_value(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "direction", write_event_vec3(ev_get(value, "direction")));
  ev_set(o1, "cosHalfAngle", ev_clone(ev_get(value, "cosHalfAngle")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q2_presentation_event_dynamic_light_cone(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *c1;
  if (ev_r_null(reader)) {
    c1 = ev_null();
  } else {
    c1 = read_event_q2_presentation_event_dynamic_light_cone_value(reader, identity);
  }
  qa_save_value *result = c1;
  return result;
}

static qa_save_value *write_event_q2_presentation_event_dynamic_light_cone(const qa_save_value *value) {
  qa_save_value *c1;
  if (ev_null_is(value)) {
    c1 = ev_null();
  } else {
    c1 = write_event_q2_presentation_event_dynamic_light_cone_value(value);
  }
  qa_save_value *result = c1;
  return result;
}

static qa_save_value *read_event_q2_presentation_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "model")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"model", 0}}, 1));
    ev_set(o2, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o2, "path", ev_f_string(reader, "path"));
    ev_set(o2, "attachedModels", ev_read(reader, "attachedModels", read_event_q2_presentation_event_model_attached_models, identity));
    ev_set(o2, "frame", ev_f_finite(reader, "frame"));
    ev_set(o2, "oldFrame", ev_f_finite(reader, "oldFrame"));
    ev_set(o2, "scale", ev_f_finite(reader, "scale"));
    ev_set(o2, "alpha", ev_f_finite(reader, "alpha"));
    ev_set(o2, "skin", ev_f_finite(reader, "skin"));
    ev_set(o2, "effects", ev_f_finite(reader, "effects"));
    ev_set(o2, "renderFlags", ev_f_finite(reader, "renderFlags"));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "visibility")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"visibility", 0}}, 1));
    ev_set(o3, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o3, "visible", ev_f_boolean(reader, "visible"));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "sound")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"sound", 0}}, 1));
    ev_set(o4, "actor", ev_read(reader, "actor", read_event_q1_event_effect_actor, identity));
    ev_set(o4, "origin", ev_read(reader, "origin", read_event_vec3, identity));
    ev_set(o4, "path", ev_f_string(reader, "path"));
    ev_set(o4, "channel", ev_f_finite(reader, "channel"));
    ev_set(o4, "volume", ev_f_finite(reader, "volume"));
    ev_set(o4, "attenuation", ev_f_finite(reader, "attenuation"));
    ev_set(o4, "reliable", ev_f_boolean(reader, "reliable"));
    ev_set(o4, "loop", ev_f_choice(reader, "loop", (const ev_lit[]){{"start", 0}, {"stop", 0}, {"once", 0}}, 3));
    if (!ev_f_undefined(reader, "loopOwner")) {
      ev_set(o4, "loopOwner", ev_read(reader, "loopOwner", read_provider_id, identity));
    }
    if (!ev_f_undefined(reader, "local")) {
      ev_set(o4, "local", ev_f_boolean(reader, "local"));
    }
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "centerprint")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"centerprint", 0}}, 1));
    ev_set(o5, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o5, "text", ev_f_string(reader, "text"));
    if (!ev_f_undefined(reader, "instant")) {
      ev_set(o5, "instant", ev_f_boolean(reader, "instant"));
    }
    if (!ev_f_undefined(reader, "durationSeconds")) {
      ev_set(o5, "durationSeconds", ev_f_finite(reader, "durationSeconds"));
    }
    qa_save_value *result = o5;
    return result;
  }
  if (ev_is(kind1, "print")) {
    qa_save_value *o6 = ev_object();
    ev_set(o6, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"print", 0}}, 1));
    ev_set(o6, "actor", ev_read(reader, "actor", read_event_q1_event_effect_actor, identity));
    ev_set(o6, "level", ev_f_choice(reader, "level", (const ev_lit[]){{"low", 0}, {"medium", 0}, {"high", 0}, {"chat", 0}}, 4));
    ev_set(o6, "text", ev_f_string(reader, "text"));
    qa_save_value *result = o6;
    return result;
  }
  if (ev_is(kind1, "help")) {
    qa_save_value *o7 = ev_object();
    ev_set(o7, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"help", 0}}, 1));
    ev_set(o7, "slot", ev_f_choice(reader, "slot", (const ev_lit[]){{NULL, 1.0}, {NULL, 2.0}}, 2));
    ev_set(o7, "text", ev_f_string(reader, "text"));
    qa_save_value *result = o7;
    return result;
  }
  if (ev_is(kind1, "lightstyle")) {
    qa_save_value *result = read_event_q1_event_lightstyle(reader, identity);
    return result;
  }
  if (ev_is(kind1, "music")) {
    qa_save_value *o8 = ev_object();
    ev_set(o8, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"music", 0}}, 1));
    ev_set(o8, "track", ev_f_string(reader, "track"));
    qa_save_value *result = o8;
    return result;
  }
  if (ev_is(kind1, "effect")) {
    qa_save_value *o9 = ev_object();
    ev_set(o9, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"effect", 0}}, 1));
    ev_set(o9, "effect", ev_f_string(reader, "effect"));
    ev_set(o9, "origin", ev_read(reader, "origin", read_event_vec3, identity));
    ev_set(o9, "direction", ev_read(reader, "direction", read_event_vec3, identity));
    ev_set(o9, "count", ev_f_finite(reader, "count"));
    ev_set(o9, "color", ev_f_finite(reader, "color"));
    qa_save_value *result = o9;
    return result;
  }
  if (ev_is(kind1, "damage-indicator")) {
    qa_save_value *o10 = ev_object();
    ev_set(o10, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"damage-indicator", 0}}, 1));
    ev_set(o10, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o10, "origin", ev_read(reader, "origin", read_event_vec3, identity));
    ev_set(o10, "amount", ev_f_finite(reader, "amount"));
    qa_save_value *result = o10;
    return result;
  }
  if (ev_is(kind1, "pickup")) {
    qa_save_value *o11 = ev_object();
    ev_set(o11, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"pickup", 0}}, 1));
    ev_set(o11, "player", ev_read(reader, "player", read_actor_id, identity));
    ev_set(o11, "item", ev_read(reader, "item", read_item_id, identity));
    ev_set(o11, "icon", ev_f_string(reader, "icon"));
    ev_set(o11, "name", ev_f_string(reader, "name"));
    qa_save_value *result = o11;
    return result;
  }
  if (ev_is(kind1, "poi")) {
    qa_save_value *o12 = ev_object();
    ev_set(o12, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"poi", 0}}, 1));
    ev_set(o12, "origin", ev_read(reader, "origin", read_event_vec3, identity));
    ev_set(o12, "message", ev_f_string(reader, "message"));
    ev_set(o12, "fields", ev_read(reader, "fields", read_event_q2_presentation_event_poi_fields, identity));
    qa_save_value *result = o12;
    return result;
  }
  if (ev_is(kind1, "dynamic-light")) {
    qa_save_value *o13 = ev_object();
    ev_set(o13, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"dynamic-light", 0}}, 1));
    ev_set(o13, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o13, "origin", ev_read(reader, "origin", read_event_vec3, identity));
    ev_set(o13, "color", ev_read(reader, "color", read_event_vec3, identity));
    ev_set(o13, "visible", ev_f_boolean(reader, "visible"));
    ev_set(o13, "radius", ev_f_finite(reader, "radius"));
    ev_set(o13, "intensity", ev_f_finite(reader, "intensity"));
    ev_set(o13, "resolution", ev_f_finite(reader, "resolution"));
    ev_set(o13, "fadeStart", ev_f_finite(reader, "fadeStart"));
    ev_set(o13, "fadeEnd", ev_f_finite(reader, "fadeEnd"));
    ev_set(o13, "lightstyle", ev_f_finite(reader, "lightstyle"));
    ev_set(o13, "cone", ev_read(reader, "cone", read_event_q2_presentation_event_dynamic_light_cone, identity));
    qa_save_value *result = o13;
    return result;
  }
  if (ev_is(kind1, "beam")) {
    qa_save_value *o14 = ev_object();
    ev_set(o14, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"beam", 0}}, 1));
    ev_set(o14, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o14, "start", ev_read(reader, "start", read_event_vec3, identity));
    ev_set(o14, "end", ev_read(reader, "end", read_event_vec3, identity));
    ev_set(o14, "width", ev_f_finite(reader, "width"));
    ev_set(o14, "color", ev_f_finite(reader, "color"));
    ev_set(o14, "visible", ev_f_boolean(reader, "visible"));
    qa_save_value *result = o14;
    return result;
  }
  if (ev_is(kind1, "monster-beam")) {
    qa_save_value *o15 = ev_object();
    ev_set(o15, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"monster-beam", 0}}, 1));
    ev_set(o15, "effect", ev_f_choice(reader, "effect", (const ev_lit[]){{"parasite", 0}, {"medic", 0}}, 2));
    ev_set(o15, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o15, "start", ev_read(reader, "start", read_event_vec3, identity));
    ev_set(o15, "end", ev_read(reader, "end", read_event_vec3, identity));
    qa_save_value *result = o15;
    return result;
  }
  if (ev_is(kind1, "monster-muzzleflash")) {
    qa_save_value *o16 = ev_object();
    ev_set(o16, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"monster-muzzleflash", 0}}, 1));
    ev_set(o16, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o16, "flash", ev_f_finite(reader, "flash"));
    ev_set(o16, "origin", ev_read(reader, "origin", read_event_vec3, identity));
    ev_set(o16, "direction", ev_read(reader, "direction", read_event_vec3, identity));
    qa_save_value *result = o16;
    return result;
  }
  if (ev_is(kind1, "entity-event")) {
    qa_save_value *o17 = ev_object();
    ev_set(o17, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"entity-event", 0}}, 1));
    ev_set(o17, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o17, "event", ev_f_finite(reader, "event"));
    qa_save_value *result = o17;
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_q2_presentation_event(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "model")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o2, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o2, "path", ev_clone(ev_get(value, "path")));
    ev_set(o2, "attachedModels", write_event_q2_presentation_event_model_attached_models(ev_get(value, "attachedModels")));
    ev_set(o2, "frame", ev_clone(ev_get(value, "frame")));
    ev_set(o2, "oldFrame", ev_clone(ev_get(value, "oldFrame")));
    ev_set(o2, "scale", ev_clone(ev_get(value, "scale")));
    ev_set(o2, "alpha", ev_clone(ev_get(value, "alpha")));
    ev_set(o2, "skin", ev_clone(ev_get(value, "skin")));
    ev_set(o2, "effects", ev_clone(ev_get(value, "effects")));
    ev_set(o2, "renderFlags", ev_clone(ev_get(value, "renderFlags")));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "visibility")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o3, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o3, "visible", ev_clone(ev_get(value, "visible")));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "sound")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o4, "actor", write_event_q1_event_effect_actor(ev_get(value, "actor")));
    ev_set(o4, "origin", write_event_vec3(ev_get(value, "origin")));
    ev_set(o4, "path", ev_clone(ev_get(value, "path")));
    ev_set(o4, "channel", ev_clone(ev_get(value, "channel")));
    ev_set(o4, "volume", ev_clone(ev_get(value, "volume")));
    ev_set(o4, "attenuation", ev_clone(ev_get(value, "attenuation")));
    ev_set(o4, "reliable", ev_clone(ev_get(value, "reliable")));
    ev_set(o4, "loop", ev_clone(ev_get(value, "loop")));
    if (!ev_undefined_is(ev_get(value, "loopOwner"))) {
      ev_set(o4, "loopOwner", write_provider_id(ev_get(value, "loopOwner")));
    }
    if (!ev_undefined_is(ev_get(value, "local"))) {
      ev_set(o4, "local", ev_clone(ev_get(value, "local")));
    }
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "centerprint")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o5, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o5, "text", ev_clone(ev_get(value, "text")));
    if (!ev_undefined_is(ev_get(value, "instant"))) {
      ev_set(o5, "instant", ev_clone(ev_get(value, "instant")));
    }
    if (!ev_undefined_is(ev_get(value, "durationSeconds"))) {
      ev_set(o5, "durationSeconds", ev_clone(ev_get(value, "durationSeconds")));
    }
    qa_save_value *result = o5;
    return result;
  }
  if (ev_is(kind1, "print")) {
    qa_save_value *o6 = ev_object();
    ev_set(o6, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o6, "actor", write_event_q1_event_effect_actor(ev_get(value, "actor")));
    ev_set(o6, "level", ev_clone(ev_get(value, "level")));
    ev_set(o6, "text", ev_clone(ev_get(value, "text")));
    qa_save_value *result = o6;
    return result;
  }
  if (ev_is(kind1, "help")) {
    qa_save_value *o7 = ev_object();
    ev_set(o7, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o7, "slot", ev_clone(ev_get(value, "slot")));
    ev_set(o7, "text", ev_clone(ev_get(value, "text")));
    qa_save_value *result = o7;
    return result;
  }
  if (ev_is(kind1, "lightstyle")) {
    qa_save_value *result = write_event_q1_event_lightstyle(value);
    return result;
  }
  if (ev_is(kind1, "music")) {
    qa_save_value *o8 = ev_object();
    ev_set(o8, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o8, "track", ev_clone(ev_get(value, "track")));
    qa_save_value *result = o8;
    return result;
  }
  if (ev_is(kind1, "effect")) {
    qa_save_value *o9 = ev_object();
    ev_set(o9, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o9, "effect", ev_clone(ev_get(value, "effect")));
    ev_set(o9, "origin", write_event_vec3(ev_get(value, "origin")));
    ev_set(o9, "direction", write_event_vec3(ev_get(value, "direction")));
    ev_set(o9, "count", ev_clone(ev_get(value, "count")));
    ev_set(o9, "color", ev_clone(ev_get(value, "color")));
    qa_save_value *result = o9;
    return result;
  }
  if (ev_is(kind1, "damage-indicator")) {
    qa_save_value *o10 = ev_object();
    ev_set(o10, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o10, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o10, "origin", write_event_vec3(ev_get(value, "origin")));
    ev_set(o10, "amount", ev_clone(ev_get(value, "amount")));
    qa_save_value *result = o10;
    return result;
  }
  if (ev_is(kind1, "pickup")) {
    qa_save_value *o11 = ev_object();
    ev_set(o11, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o11, "player", write_actor_id(ev_get(value, "player")));
    ev_set(o11, "item", write_item_id(ev_get(value, "item")));
    ev_set(o11, "icon", ev_clone(ev_get(value, "icon")));
    ev_set(o11, "name", ev_clone(ev_get(value, "name")));
    qa_save_value *result = o11;
    return result;
  }
  if (ev_is(kind1, "poi")) {
    qa_save_value *o12 = ev_object();
    ev_set(o12, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o12, "origin", write_event_vec3(ev_get(value, "origin")));
    ev_set(o12, "message", ev_clone(ev_get(value, "message")));
    ev_set(o12, "fields", write_event_q2_presentation_event_poi_fields(ev_get(value, "fields")));
    qa_save_value *result = o12;
    return result;
  }
  if (ev_is(kind1, "dynamic-light")) {
    qa_save_value *o13 = ev_object();
    ev_set(o13, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o13, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o13, "origin", write_event_vec3(ev_get(value, "origin")));
    ev_set(o13, "color", write_event_vec3(ev_get(value, "color")));
    ev_set(o13, "visible", ev_clone(ev_get(value, "visible")));
    ev_set(o13, "radius", ev_clone(ev_get(value, "radius")));
    ev_set(o13, "intensity", ev_clone(ev_get(value, "intensity")));
    ev_set(o13, "resolution", ev_clone(ev_get(value, "resolution")));
    ev_set(o13, "fadeStart", ev_clone(ev_get(value, "fadeStart")));
    ev_set(o13, "fadeEnd", ev_clone(ev_get(value, "fadeEnd")));
    ev_set(o13, "lightstyle", ev_clone(ev_get(value, "lightstyle")));
    ev_set(o13, "cone", write_event_q2_presentation_event_dynamic_light_cone(ev_get(value, "cone")));
    qa_save_value *result = o13;
    return result;
  }
  if (ev_is(kind1, "beam")) {
    qa_save_value *o14 = ev_object();
    ev_set(o14, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o14, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o14, "start", write_event_vec3(ev_get(value, "start")));
    ev_set(o14, "end", write_event_vec3(ev_get(value, "end")));
    ev_set(o14, "width", ev_clone(ev_get(value, "width")));
    ev_set(o14, "color", ev_clone(ev_get(value, "color")));
    ev_set(o14, "visible", ev_clone(ev_get(value, "visible")));
    qa_save_value *result = o14;
    return result;
  }
  if (ev_is(kind1, "monster-beam")) {
    qa_save_value *o15 = ev_object();
    ev_set(o15, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o15, "effect", ev_clone(ev_get(value, "effect")));
    ev_set(o15, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o15, "start", write_event_vec3(ev_get(value, "start")));
    ev_set(o15, "end", write_event_vec3(ev_get(value, "end")));
    qa_save_value *result = o15;
    return result;
  }
  if (ev_is(kind1, "monster-muzzleflash")) {
    qa_save_value *o16 = ev_object();
    ev_set(o16, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o16, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o16, "flash", ev_clone(ev_get(value, "flash")));
    ev_set(o16, "origin", write_event_vec3(ev_get(value, "origin")));
    ev_set(o16, "direction", write_event_vec3(ev_get(value, "direction")));
    qa_save_value *result = o16;
    return result;
  }
  if (ev_is(kind1, "entity-event")) {
    qa_save_value *o17 = ev_object();
    ev_set(o17, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o17, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o17, "event", ev_clone(ev_get(value, "event")));
    qa_save_value *result = o17;
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_q2_weapon_event_view_weapon_weapon(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *c1;
  if (ev_r_null(reader)) {
    c1 = ev_null();
  } else {
    c1 = ev_r_string(reader);
  }
  qa_save_value *result = c1;
  return result;
}

static qa_save_value *write_event_q2_weapon_event_view_weapon_weapon(const qa_save_value *value) {
  qa_save_value *c1;
  if (ev_null_is(value)) {
    c1 = ev_null();
  } else {
    c1 = ev_clone(value);
  }
  qa_save_value *result = c1;
  return result;
}

static qa_save_value *read_event_q2_weapon_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "muzzleflash")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"muzzleflash", 0}}, 1));
    ev_set(o2, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o2, "flash", ev_f_finite(reader, "flash"));
    ev_set(o2, "silenced", ev_f_boolean(reader, "silenced"));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "beam")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"beam", 0}}, 1));
    ev_set(o3, "effect", ev_f_choice(reader, "effect", (const ev_lit[]){{"rail", 0}, {"rail-water", 0}, {"bfg-laser", 0}, {"bfg-zap", 0}, {"bubble-trail", 0}, {"bfg-lightning", 0}, {"heatbeam", 0}, {"monster-heatbeam", 0}}, 8));
    qa_save_reader f4;
    ev_field(reader, "actor", &f4);
    ev_set(o3, "actor", ev_nullable(&f4, read_actor_id, identity));
    ev_set(o3, "start", ev_read(reader, "start", read_event_vec3, identity));
    ev_set(o3, "end", ev_read(reader, "end", read_event_vec3, identity));
    ev_set(o3, "duration", ev_f_finite(reader, "duration"));
    qa_save_value *result = o3;
    qa_save_reader_destroy(&f4);
    return result;
  }
  if (ev_is(kind1, "view-weapon")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"view-weapon", 0}}, 1));
    ev_set(o5, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o5, "weapon", ev_read(reader, "weapon", read_event_q2_weapon_event_view_weapon_weapon, identity));
    ev_set(o5, "model", ev_f_string(reader, "model"));
    ev_set(o5, "playerModel", ev_f_finite(reader, "playerModel"));
    ev_set(o5, "frame", ev_f_finite(reader, "frame"));
    ev_set(o5, "skin", ev_f_finite(reader, "skin"));
    ev_set(o5, "rate", ev_f_finite(reader, "rate"));
    ev_set(o5, "kickOrigin", ev_read(reader, "kickOrigin", read_event_vec3, identity));
    ev_set(o5, "kickAngles", ev_read(reader, "kickAngles", read_event_vec3, identity));
    qa_save_value *result = o5;
    return result;
  }
  if (ev_is(kind1, "player-animation")) {
    qa_save_value *o6 = ev_object();
    ev_set(o6, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"player-animation", 0}}, 1));
    ev_set(o6, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o6, "priority", ev_f_choice(reader, "priority", (const ev_lit[]){{"attack", 0}, {"pain", 0}, {"reverse", 0}}, 3));
    ev_set(o6, "first", ev_f_finite(reader, "first"));
    ev_set(o6, "last", ev_f_finite(reader, "last"));
    ev_set(o6, "resetTime", ev_f_boolean(reader, "resetTime"));
    qa_save_value *result = o6;
    return result;
  }
  if (ev_is(kind1, "invisibility-reveal")) {
    qa_save_value *o7 = ev_object();
    ev_set(o7, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"invisibility-reveal", 0}}, 1));
    ev_set(o7, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o7, "until", ev_f_finite(reader, "until"));
    qa_save_value *result = o7;
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_q2_weapon_event(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "muzzleflash")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o2, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o2, "flash", ev_clone(ev_get(value, "flash")));
    ev_set(o2, "silenced", ev_clone(ev_get(value, "silenced")));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "beam")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o3, "effect", ev_clone(ev_get(value, "effect")));
    qa_save_value *c4;
    if (ev_null_is(ev_get(value, "actor"))) {
      c4 = ev_null();
    } else {
      c4 = write_actor_id(ev_get(value, "actor"));
    }
    ev_set(o3, "actor", c4);
    ev_set(o3, "start", write_event_vec3(ev_get(value, "start")));
    ev_set(o3, "end", write_event_vec3(ev_get(value, "end")));
    ev_set(o3, "duration", ev_clone(ev_get(value, "duration")));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "view-weapon")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o5, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o5, "weapon", write_event_q2_weapon_event_view_weapon_weapon(ev_get(value, "weapon")));
    ev_set(o5, "model", ev_clone(ev_get(value, "model")));
    ev_set(o5, "playerModel", ev_clone(ev_get(value, "playerModel")));
    ev_set(o5, "frame", ev_clone(ev_get(value, "frame")));
    ev_set(o5, "skin", ev_clone(ev_get(value, "skin")));
    ev_set(o5, "rate", ev_clone(ev_get(value, "rate")));
    ev_set(o5, "kickOrigin", write_event_vec3(ev_get(value, "kickOrigin")));
    ev_set(o5, "kickAngles", write_event_vec3(ev_get(value, "kickAngles")));
    qa_save_value *result = o5;
    return result;
  }
  if (ev_is(kind1, "player-animation")) {
    qa_save_value *o6 = ev_object();
    ev_set(o6, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o6, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o6, "priority", ev_clone(ev_get(value, "priority")));
    ev_set(o6, "first", ev_clone(ev_get(value, "first")));
    ev_set(o6, "last", ev_clone(ev_get(value, "last")));
    ev_set(o6, "resetTime", ev_clone(ev_get(value, "resetTime")));
    qa_save_value *result = o6;
    return result;
  }
  if (ev_is(kind1, "invisibility-reveal")) {
    qa_save_value *o7 = ev_object();
    ev_set(o7, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o7, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o7, "until", ev_clone(ev_get(value, "until")));
    qa_save_value *result = o7;
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_q2_ctf_score_row_carried_flag(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *c1;
  if (ev_r_null(reader)) {
    c1 = ev_null();
  } else {
    c1 = ev_r_choice(reader, (const ev_lit[]){{NULL, 1.0}, {NULL, 2.0}}, 2);
  }
  qa_save_value *result = c1;
  return result;
}

static qa_save_value *write_event_q2_ctf_score_row_carried_flag(const qa_save_value *value) {
  qa_save_value *result = ev_clone(value);
  return result;
}

static qa_save_value *read_event_q2_ctf_score_row(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "slot", ev_f_finite(reader, "slot"));
  ev_set(o1, "name", ev_f_string(reader, "name"));
  ev_set(o1, "score", ev_f_finite(reader, "score"));
  ev_set(o1, "ping", ev_f_finite(reader, "ping"));
  ev_set(o1, "carriedFlag", ev_read(reader, "carriedFlag", read_event_q2_ctf_score_row_carried_flag, identity));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_q2_ctf_score_row(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "slot", ev_clone(ev_get(value, "slot")));
  ev_set(o1, "name", ev_clone(ev_get(value, "name")));
  ev_set(o1, "score", ev_clone(ev_get(value, "score")));
  ev_set(o1, "ping", ev_clone(ev_get(value, "ping")));
  ev_set(o1, "carriedFlag", write_event_q2_ctf_score_row_carried_flag(ev_get(value, "carriedFlag")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q2_ctf_event_scoreboard_red(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *result = ev_list(reader, read_event_q2_ctf_score_row, identity);
  return result;
}

static qa_save_value *write_event_q2_ctf_event_scoreboard_red(const qa_save_value *value) {
  qa_save_value *result = ev_map(value, write_event_q2_ctf_score_row);
  return result;
}

static qa_save_value *read_event_q2_ctf_event_scoreboard_captures(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  size_t items_length = ev_bounded_count(reader);
  if (!(items_length == 2)) {
    qa_save_reader_fail(reader, "%s", "invalid tuple length");
  }
  qa_save_reader item0;
  ev_item(reader, 0, &item0);
  qa_save_reader item1;
  ev_item(reader, 1, &item1);
  qa_save_value *a1 = ev_array();
  ev_push(a1, ev_r_finite(&item0));
  ev_push(a1, ev_r_finite(&item1));
  qa_save_value *result = a1;
  qa_save_reader_destroy(&item1);
  qa_save_reader_destroy(&item0);
  return result;
}

static qa_save_value *write_event_q2_ctf_event_scoreboard_captures(const qa_save_value *value) {
  qa_save_value *a1 = ev_array();
  ev_push(a1, ev_clone(ev_at(value, 0)));
  ev_push(a1, ev_clone(ev_at(value, 1)));
  qa_save_value *result = a1;
  return result;
}

static qa_save_value *read_event_q2_ctf_event_hud_flag_states(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  size_t items_length = ev_bounded_count(reader);
  if (!(items_length == 2)) {
    qa_save_reader_fail(reader, "%s", "invalid tuple length");
  }
  qa_save_reader item0;
  ev_item(reader, 0, &item0);
  qa_save_reader item1;
  ev_item(reader, 1, &item1);
  qa_save_value *a1 = ev_array();
  ev_push(a1, ev_r_choice(&item0, (const ev_lit[]){{"base", 0}, {"dropped", 0}, {"taken", 0}}, 3));
  ev_push(a1, ev_r_choice(&item1, (const ev_lit[]){{"base", 0}, {"dropped", 0}, {"taken", 0}}, 3));
  qa_save_value *result = a1;
  qa_save_reader_destroy(&item1);
  qa_save_reader_destroy(&item0);
  return result;
}

static qa_save_value *write_event_q2_ctf_event_hud_flag_states(const qa_save_value *value) {
  qa_save_value *a1 = ev_array();
  ev_push(a1, ev_clone(ev_at(value, 0)));
  ev_push(a1, ev_clone(ev_at(value, 1)));
  qa_save_value *result = a1;
  return result;
}

static qa_save_value *read_event_q2_ctf_event_hud_tech(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *c1;
  if (ev_r_null(reader)) {
    c1 = ev_null();
  } else {
    c1 = ev_r_choice(reader, (const ev_lit[]){{"item_tech1", 0}, {"item_tech2", 0}, {"item_tech3", 0}, {"item_tech4", 0}}, 4);
  }
  qa_save_value *result = c1;
  return result;
}

static qa_save_value *write_event_q2_ctf_event_hud_tech(const qa_save_value *value) {
  qa_save_value *result = ev_clone(value);
  return result;
}

static qa_save_value *read_event_q2_ctf_event_menu_entries_item_action(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *c1;
  if (ev_r_null(reader)) {
    c1 = ev_null();
  } else {
    c1 = ev_r_choice(reader, (const ev_lit[]){{"join-red", 0}, {"join-blue", 0}, {"observer", 0}, {"chase", 0}, {"credits", 0}, {"match", 0}, {"ready", 0}, {"notready", 0}, {"admin-settings", 0}, {"admin-start", 0}, {"admin-cancel", 0}, {"close", 0}}, 12);
  }
  qa_save_value *result = c1;
  return result;
}

static qa_save_value *write_event_q2_ctf_event_menu_entries_item_action(const qa_save_value *value) {
  qa_save_value *result = ev_clone(value);
  return result;
}

static qa_save_value *read_event_q2_ctf_event_menu_entries_item(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "label", ev_f_string(reader, "label"));
  ev_set(o1, "action", ev_read(reader, "action", read_event_q2_ctf_event_menu_entries_item_action, identity));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_q2_ctf_event_menu_entries_item(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "label", ev_clone(ev_get(value, "label")));
  ev_set(o1, "action", write_event_q2_ctf_event_menu_entries_item_action(ev_get(value, "action")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q2_ctf_event_menu_entries(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *result = ev_list(reader, read_event_q2_ctf_event_menu_entries_item, identity);
  return result;
}

static qa_save_value *write_event_q2_ctf_event_menu_entries(const qa_save_value *value) {
  qa_save_value *result = ev_map(value, write_event_q2_ctf_event_menu_entries_item);
  return result;
}

static qa_save_value *read_event_q2_ctf_admin_settings(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "matchMinutes", ev_f_finite(reader, "matchMinutes"));
  ev_set(o1, "setupMinutes", ev_f_finite(reader, "setupMinutes"));
  ev_set(o1, "startSeconds", ev_f_finite(reader, "startSeconds"));
  ev_set(o1, "weaponsStay", ev_f_boolean(reader, "weaponsStay"));
  ev_set(o1, "instantItems", ev_f_boolean(reader, "instantItems"));
  ev_set(o1, "quadDrop", ev_f_boolean(reader, "quadDrop"));
  ev_set(o1, "instantWeapons", ev_f_boolean(reader, "instantWeapons"));
  ev_set(o1, "matchLock", ev_f_boolean(reader, "matchLock"));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_q2_ctf_admin_settings(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "matchMinutes", ev_clone(ev_get(value, "matchMinutes")));
  ev_set(o1, "setupMinutes", ev_clone(ev_get(value, "setupMinutes")));
  ev_set(o1, "startSeconds", ev_clone(ev_get(value, "startSeconds")));
  ev_set(o1, "weaponsStay", ev_clone(ev_get(value, "weaponsStay")));
  ev_set(o1, "instantItems", ev_clone(ev_get(value, "instantItems")));
  ev_set(o1, "quadDrop", ev_clone(ev_get(value, "quadDrop")));
  ev_set(o1, "instantWeapons", ev_clone(ev_get(value, "instantWeapons")));
  ev_set(o1, "matchLock", ev_clone(ev_get(value, "matchLock")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q2_ctf_event_grapple_cable(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"grapple-cable", 0}}, 1));
  ev_set(o1, "actor", ev_read(reader, "actor", read_actor_id, identity));
  ev_set(o1, "start", ev_read(reader, "start", read_event_vec3, identity));
  ev_set(o1, "end", ev_read(reader, "end", read_event_vec3, identity));
  ev_set(o1, "offset", ev_read(reader, "offset", read_event_vec3, identity));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_q2_ctf_event_grapple_cable(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "kind", ev_clone(ev_get(value, "kind")));
  ev_set(o1, "actor", write_actor_id(ev_get(value, "actor")));
  ev_set(o1, "start", write_event_vec3(ev_get(value, "start")));
  ev_set(o1, "end", write_event_vec3(ev_get(value, "end")));
  ev_set(o1, "offset", write_event_vec3(ev_get(value, "offset")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q2_ctf_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "scoreboard")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"scoreboard", 0}}, 1));
    ev_set(o2, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o2, "red", ev_read(reader, "red", read_event_q2_ctf_event_scoreboard_red, identity));
    ev_set(o2, "blue", ev_read(reader, "blue", read_event_q2_ctf_event_scoreboard_red, identity));
    ev_set(o2, "spectators", ev_read(reader, "spectators", read_event_q2_ctf_event_scoreboard_red, identity));
    ev_set(o2, "captures", ev_read(reader, "captures", read_event_q2_ctf_event_scoreboard_captures, identity));
    ev_set(o2, "totals", ev_read(reader, "totals", read_event_q2_ctf_event_scoreboard_captures, identity));
    ev_set(o2, "layout", ev_f_string(reader, "layout"));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "hud")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"hud", 0}}, 1));
    ev_set(o3, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o3, "captures", ev_read(reader, "captures", read_event_q2_ctf_event_scoreboard_captures, identity));
    ev_set(o3, "flagStates", ev_read(reader, "flagStates", read_event_q2_ctf_event_hud_flag_states, identity));
    ev_set(o3, "team", ev_f_choice(reader, "team", (const ev_lit[]){{NULL, 0.0}, {NULL, 1.0}, {NULL, 2.0}}, 3));
    ev_set(o3, "carriedFlag", ev_read(reader, "carriedFlag", read_event_q2_ctf_score_row_carried_flag, identity));
    ev_set(o3, "tech", ev_read(reader, "tech", read_event_q2_ctf_event_hud_tech, identity));
    ev_set(o3, "idTarget", ev_read(reader, "idTarget", read_event_q1_event_effect_actor, identity));
    ev_set(o3, "blinkTeam", ev_read(reader, "blinkTeam", read_event_q2_ctf_score_row_carried_flag, identity));
    ev_set(o3, "match", ev_f_string(reader, "match"));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "menu")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"menu", 0}}, 1));
    ev_set(o4, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o4, "title", ev_f_string(reader, "title"));
    ev_set(o4, "entries", ev_read(reader, "entries", read_event_q2_ctf_event_menu_entries, identity));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "match-status")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"match-status", 0}}, 1));
    ev_set(o5, "text", ev_f_string(reader, "text"));
    qa_save_value *result = o5;
    return result;
  }
  if (ev_is(kind1, "admin-settings")) {
    qa_save_value *o6 = ev_object();
    ev_set(o6, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"admin-settings", 0}}, 1));
    ev_set(o6, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o6, "settings", ev_read(reader, "settings", read_event_q2_ctf_admin_settings, identity));
    qa_save_value *result = o6;
    return result;
  }
  if (ev_is(kind1, "grapple-cable")) {
    qa_save_value *result = read_event_q2_ctf_event_grapple_cable(reader, identity);
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_q2_ctf_event(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "scoreboard")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o2, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o2, "red", write_event_q2_ctf_event_scoreboard_red(ev_get(value, "red")));
    ev_set(o2, "blue", write_event_q2_ctf_event_scoreboard_red(ev_get(value, "blue")));
    ev_set(o2, "spectators", write_event_q2_ctf_event_scoreboard_red(ev_get(value, "spectators")));
    ev_set(o2, "captures", write_event_q2_ctf_event_scoreboard_captures(ev_get(value, "captures")));
    ev_set(o2, "totals", write_event_q2_ctf_event_scoreboard_captures(ev_get(value, "totals")));
    ev_set(o2, "layout", ev_clone(ev_get(value, "layout")));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "hud")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o3, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o3, "captures", write_event_q2_ctf_event_scoreboard_captures(ev_get(value, "captures")));
    ev_set(o3, "flagStates", write_event_q2_ctf_event_hud_flag_states(ev_get(value, "flagStates")));
    ev_set(o3, "team", ev_clone(ev_get(value, "team")));
    ev_set(o3, "carriedFlag", write_event_q2_ctf_score_row_carried_flag(ev_get(value, "carriedFlag")));
    ev_set(o3, "tech", write_event_q2_ctf_event_hud_tech(ev_get(value, "tech")));
    ev_set(o3, "idTarget", write_event_q1_event_effect_actor(ev_get(value, "idTarget")));
    ev_set(o3, "blinkTeam", write_event_q2_ctf_score_row_carried_flag(ev_get(value, "blinkTeam")));
    ev_set(o3, "match", ev_clone(ev_get(value, "match")));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "menu")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o4, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o4, "title", ev_clone(ev_get(value, "title")));
    ev_set(o4, "entries", write_event_q2_ctf_event_menu_entries(ev_get(value, "entries")));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "match-status")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o5, "text", ev_clone(ev_get(value, "text")));
    qa_save_value *result = o5;
    return result;
  }
  if (ev_is(kind1, "admin-settings")) {
    qa_save_value *o6 = ev_object();
    ev_set(o6, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o6, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o6, "settings", write_event_q2_ctf_admin_settings(ev_get(value, "settings")));
    qa_save_value *result = o6;
    return result;
  }
  if (ev_is(kind1, "grapple-cable")) {
    qa_save_value *result = write_event_q2_ctf_event_grapple_cable(value);
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_lmctf_event_menu_entries_item(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "label", ev_f_string(reader, "label"));
  ev_set(o1, "command", ev_read(reader, "command", read_event_q2_weapon_event_view_weapon_weapon, identity));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_lmctf_event_menu_entries_item(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "label", ev_clone(ev_get(value, "label")));
  ev_set(o1, "command", write_event_q2_weapon_event_view_weapon_weapon(ev_get(value, "command")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_lmctf_event_menu_entries(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *result = ev_list(reader, read_event_lmctf_event_menu_entries_item, identity);
  return result;
}

static qa_save_value *write_event_lmctf_event_menu_entries(const qa_save_value *value) {
  qa_save_value *result = ev_map(value, write_event_lmctf_event_menu_entries_item);
  return result;
}

static qa_save_value *read_event_lmctf_score_row(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "actor", ev_read(reader, "actor", read_actor_id, identity));
  ev_set(o1, "slot", ev_f_finite(reader, "slot"));
  ev_set(o1, "name", ev_f_string(reader, "name"));
  ev_set(o1, "team", ev_f_choice(reader, "team", (const ev_lit[]){{NULL, 0.0}, {NULL, 1.0}, {NULL, 2.0}}, 3));
  ev_set(o1, "score", ev_f_finite(reader, "score"));
  ev_set(o1, "ping", ev_f_finite(reader, "ping"));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_lmctf_score_row(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "actor", write_actor_id(ev_get(value, "actor")));
  ev_set(o1, "slot", ev_clone(ev_get(value, "slot")));
  ev_set(o1, "name", ev_clone(ev_get(value, "name")));
  ev_set(o1, "team", ev_clone(ev_get(value, "team")));
  ev_set(o1, "score", ev_clone(ev_get(value, "score")));
  ev_set(o1, "ping", ev_clone(ev_get(value, "ping")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_lmctf_event_scoreboard_rows(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *result = ev_list(reader, read_event_lmctf_score_row, identity);
  return result;
}

static qa_save_value *write_event_lmctf_event_scoreboard_rows(const qa_save_value *value) {
  qa_save_value *result = ev_map(value, write_event_lmctf_score_row);
  return result;
}

static qa_save_value *read_event_lmctf_event_hud_rune(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *c1;
  if (ev_r_null(reader)) {
    c1 = ev_null();
  } else {
    c1 = ev_r_choice(reader, (const ev_lit[]){{"damage", 0}, {"resist", 0}, {"haste", 0}, {"regen", 0}, {"vampire", 0}}, 5);
  }
  qa_save_value *result = c1;
  return result;
}

static qa_save_value *write_event_lmctf_event_hud_rune(const qa_save_value *value) {
  qa_save_value *result = ev_clone(value);
  return result;
}

static qa_save_value *read_event_lmctf_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "grapple-cable")) {
    qa_save_value *result = read_event_q2_ctf_event_grapple_cable(reader, identity);
    return result;
  }
  if (ev_is(kind1, "menu")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"menu", 0}}, 1));
    ev_set(o2, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o2, "title", ev_f_string(reader, "title"));
    ev_set(o2, "entries", ev_read(reader, "entries", read_event_lmctf_event_menu_entries, identity));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "scoreboard")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"scoreboard", 0}}, 1));
    ev_set(o3, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o3, "rows", ev_read(reader, "rows", read_event_lmctf_event_scoreboard_rows, identity));
    ev_set(o3, "layout", ev_f_string(reader, "layout"));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "hud")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"hud", 0}}, 1));
    ev_set(o4, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o4, "team", ev_f_choice(reader, "team", (const ev_lit[]){{NULL, 0.0}, {NULL, 1.0}, {NULL, 2.0}}, 3));
    ev_set(o4, "carriedFlag", ev_f_boolean(reader, "carriedFlag"));
    ev_set(o4, "rune", ev_read(reader, "rune", read_event_lmctf_event_hud_rune, identity));
    ev_set(o4, "layout", ev_f_string(reader, "layout"));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "score-log")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"score-log", 0}}, 1));
    ev_set(o5, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o5, "victim", ev_read(reader, "victim", read_event_q1_event_effect_actor, identity));
    ev_set(o5, "name", ev_f_string(reader, "name"));
    ev_set(o5, "amount", ev_f_finite(reader, "amount"));
    ev_set(o5, "seconds", ev_f_finite(reader, "seconds"));
    qa_save_value *result = o5;
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_lmctf_event(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "grapple-cable")) {
    qa_save_value *result = write_event_q2_ctf_event_grapple_cable(value);
    return result;
  }
  if (ev_is(kind1, "menu")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o2, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o2, "title", ev_clone(ev_get(value, "title")));
    ev_set(o2, "entries", write_event_lmctf_event_menu_entries(ev_get(value, "entries")));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "scoreboard")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o3, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o3, "rows", write_event_lmctf_event_scoreboard_rows(ev_get(value, "rows")));
    ev_set(o3, "layout", ev_clone(ev_get(value, "layout")));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "hud")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o4, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o4, "team", ev_clone(ev_get(value, "team")));
    ev_set(o4, "carriedFlag", ev_clone(ev_get(value, "carriedFlag")));
    ev_set(o4, "rune", write_event_lmctf_event_hud_rune(ev_get(value, "rune")));
    ev_set(o4, "layout", ev_clone(ev_get(value, "layout")));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "score-log")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o5, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o5, "victim", write_event_q1_event_effect_actor(ev_get(value, "victim")));
    ev_set(o5, "name", ev_clone(ev_get(value, "name")));
    ev_set(o5, "amount", ev_clone(ev_get(value, "amount")));
    ev_set(o5, "seconds", ev_clone(ev_get(value, "seconds")));
    qa_save_value *result = o5;
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_q2_mission_pack_player_effect(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "tracker-pain")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"tracker-pain", 0}}, 1));
    ev_set(o2, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o2, "until", ev_f_finite(reader, "until"));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "nuke-blind")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"nuke-blind", 0}}, 1));
    ev_set(o3, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o3, "until", ev_f_finite(reader, "until"));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "ir")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"ir", 0}}, 1));
    ev_set(o4, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o4, "until", ev_f_finite(reader, "until"));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "sphere-camera")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"sphere-camera", 0}}, 1));
    ev_set(o5, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o5, "sphere", ev_read(reader, "sphere", read_event_q1_event_effect_actor, identity));
    ev_set(o5, "origin", ev_read(reader, "origin", read_event_vec3, identity));
    ev_set(o5, "angles", ev_read(reader, "angles", read_event_vec3, identity));
    qa_save_value *result = o5;
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_q2_mission_pack_player_effect(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "tracker-pain")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o2, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o2, "until", ev_clone(ev_get(value, "until")));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "nuke-blind")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o3, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o3, "until", ev_clone(ev_get(value, "until")));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "ir")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o4, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o4, "until", ev_clone(ev_get(value, "until")));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "sphere-camera")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o5, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o5, "sphere", write_event_q1_event_effect_actor(ev_get(value, "sphere")));
    ev_set(o5, "origin", write_event_vec3(ev_get(value, "origin")));
    ev_set(o5, "angles", write_event_vec3(ev_get(value, "angles")));
    qa_save_value *result = o5;
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_q2_mission_pack_entity_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "steam")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"steam", 0}}, 1));
    ev_set(o2, "id", ev_f_finite(reader, "id"));
    ev_set(o2, "origin", ev_read(reader, "origin", read_event_vec3, identity));
    ev_set(o2, "direction", ev_read(reader, "direction", read_event_vec3, identity));
    ev_set(o2, "count", ev_f_finite(reader, "count"));
    ev_set(o2, "color", ev_f_finite(reader, "color"));
    ev_set(o2, "speed", ev_f_finite(reader, "speed"));
    ev_set(o2, "milliseconds", ev_f_finite(reader, "milliseconds"));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "force-wall")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"force-wall", 0}}, 1));
    ev_set(o3, "start", ev_read(reader, "start", read_event_vec3, identity));
    ev_set(o3, "end", ev_read(reader, "end", read_event_vec3, identity));
    ev_set(o3, "color", ev_f_finite(reader, "color"));
    qa_save_value *result = o3;
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_q2_mission_pack_entity_event(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "steam")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o2, "id", ev_clone(ev_get(value, "id")));
    ev_set(o2, "origin", write_event_vec3(ev_get(value, "origin")));
    ev_set(o2, "direction", write_event_vec3(ev_get(value, "direction")));
    ev_set(o2, "count", ev_clone(ev_get(value, "count")));
    ev_set(o2, "color", ev_clone(ev_get(value, "color")));
    ev_set(o2, "speed", ev_clone(ev_get(value, "speed")));
    ev_set(o2, "milliseconds", ev_clone(ev_get(value, "milliseconds")));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "force-wall")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o3, "start", write_event_vec3(ev_get(value, "start")));
    ev_set(o3, "end", write_event_vec3(ev_get(value, "end")));
    ev_set(o3, "color", ev_clone(ev_get(value, "color")));
    qa_save_value *result = o3;
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_q2_composition_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "ctf")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"ctf", 0}}, 1));
    ev_set(o2, "event", ev_read(reader, "event", read_event_q2_ctf_event, identity));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "lmctf")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"lmctf", 0}}, 1));
    ev_set(o3, "event", ev_read(reader, "event", read_event_lmctf_event, identity));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "grapple-prediction")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"grapple-prediction", 0}}, 1));
    ev_set(o4, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o4, "suppressed", ev_f_boolean(reader, "suppressed"));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "kick")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"kick", 0}}, 1));
    ev_set(o5, "actor", ev_read(reader, "actor", read_actor_id, identity));
    qa_save_value *result = o5;
    return result;
  }
  if (ev_is(kind1, "missionpack-player")) {
    qa_save_value *o6 = ev_object();
    ev_set(o6, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"missionpack-player", 0}}, 1));
    ev_set(o6, "event", ev_read(reader, "event", read_event_q2_mission_pack_player_effect, identity));
    qa_save_value *result = o6;
    return result;
  }
  if (ev_is(kind1, "missionpack-entity")) {
    qa_save_value *o7 = ev_object();
    ev_set(o7, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"missionpack-entity", 0}}, 1));
    ev_set(o7, "event", ev_read(reader, "event", read_event_q2_mission_pack_entity_event, identity));
    qa_save_value *result = o7;
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_q2_composition_event(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "ctf")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o2, "event", write_event_q2_ctf_event(ev_get(value, "event")));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "lmctf")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o3, "event", write_event_lmctf_event(ev_get(value, "event")));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "grapple-prediction")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o4, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o4, "suppressed", ev_clone(ev_get(value, "suppressed")));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "kick")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o5, "actor", write_actor_id(ev_get(value, "actor")));
    qa_save_value *result = o5;
    return result;
  }
  if (ev_is(kind1, "missionpack-player")) {
    qa_save_value *o6 = ev_object();
    ev_set(o6, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o6, "event", write_event_q2_mission_pack_player_effect(ev_get(value, "event")));
    qa_save_value *result = o6;
    return result;
  }
  if (ev_is(kind1, "missionpack-entity")) {
    qa_save_value *o7 = ev_object();
    ev_set(o7, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o7, "event", write_event_q2_mission_pack_entity_event(ev_get(value, "event")));
    qa_save_value *result = o7;
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_vec4(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "x", ev_f_finite(reader, "x"));
  ev_set(o1, "y", ev_f_finite(reader, "y"));
  ev_set(o1, "z", ev_f_finite(reader, "z"));
  ev_set(o1, "w", ev_f_finite(reader, "w"));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_vec4(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "x", ev_clone(ev_get(value, "x")));
  ev_set(o1, "y", ev_clone(ev_get(value, "y")));
  ev_set(o1, "z", ev_clone(ev_get(value, "z")));
  ev_set(o1, "w", ev_clone(ev_get(value, "w")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_debug_line(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "start", ev_read(reader, "start", read_event_vec3, identity));
  ev_set(o1, "end", ev_read(reader, "end", read_event_vec3, identity));
  ev_set(o1, "color", ev_read(reader, "color", read_event_vec4, identity));
  ev_set(o1, "depthTest", ev_f_boolean(reader, "depthTest"));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_debug_line(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "start", write_event_vec3(ev_get(value, "start")));
  ev_set(o1, "end", write_event_vec3(ev_get(value, "end")));
  ev_set(o1, "color", write_event_vec4(ev_get(value, "color")));
  ev_set(o1, "depthTest", ev_clone(ev_get(value, "depthTest")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q2_rerelease_event_debug_shapes_lines(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *result = ev_list(reader, read_event_debug_line, identity);
  return result;
}

static qa_save_value *write_event_q2_rerelease_event_debug_shapes_lines(const qa_save_value *value) {
  qa_save_value *result = ev_map(value, write_event_debug_line);
  return result;
}

static qa_save_value *read_event_world_text_input_orientation(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "billboard")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"billboard", 0}}, 1));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "fixed")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"fixed", 0}}, 1));
    ev_set(o3, "angles", ev_read(reader, "angles", read_event_vec3, identity));
    qa_save_value *result = o3;
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_world_text_input_orientation(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "billboard")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "fixed")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o3, "angles", write_event_vec3(ev_get(value, "angles")));
    qa_save_value *result = o3;
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_world_text_input(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "text", ev_f_string(reader, "text"));
  ev_set(o1, "origin", ev_read(reader, "origin", read_event_vec3, identity));
  ev_set(o1, "color", ev_read(reader, "color", read_event_vec4, identity));
  ev_set(o1, "cellSize", ev_f_positive(reader, "cellSize"));
  if (!ev_f_undefined(reader, "distanceCullFactor")) {
    ev_set(o1, "distanceCullFactor", ev_f_finite(reader, "distanceCullFactor"));
  }
  ev_set(o1, "orientation", ev_read(reader, "orientation", read_event_world_text_input_orientation, identity));
  ev_set(o1, "depthTest", ev_f_boolean(reader, "depthTest"));
  ev_set(o1, "font", ev_f_choice(reader, "font", (const ev_lit[]){{"classic", 0}, {"selected", 0}}, 2));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_world_text_input(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "text", ev_clone(ev_get(value, "text")));
  ev_set(o1, "origin", write_event_vec3(ev_get(value, "origin")));
  ev_set(o1, "color", write_event_vec4(ev_get(value, "color")));
  ev_set(o1, "cellSize", ev_clone(ev_get(value, "cellSize")));
  if (!ev_undefined_is(ev_get(value, "distanceCullFactor"))) {
    ev_set(o1, "distanceCullFactor", ev_clone(ev_get(value, "distanceCullFactor")));
  }
  ev_set(o1, "orientation", write_event_world_text_input_orientation(ev_get(value, "orientation")));
  ev_set(o1, "depthTest", ev_clone(ev_get(value, "depthTest")));
  ev_set(o1, "font", ev_clone(ev_get(value, "font")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q2_fog(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "density", ev_f_finite(reader, "density"));
  ev_set(o1, "color", ev_read(reader, "color", read_event_vec3, identity));
  ev_set(o1, "skyFactor", ev_f_finite(reader, "skyFactor"));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_q2_fog(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "density", ev_clone(ev_get(value, "density")));
  ev_set(o1, "color", write_event_vec3(ev_get(value, "color")));
  ev_set(o1, "skyFactor", ev_clone(ev_get(value, "skyFactor")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q2_height_fog(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "startColor", ev_read(reader, "startColor", read_event_vec3, identity));
  ev_set(o1, "startDistance", ev_f_finite(reader, "startDistance"));
  ev_set(o1, "endColor", ev_read(reader, "endColor", read_event_vec3, identity));
  ev_set(o1, "endDistance", ev_f_finite(reader, "endDistance"));
  ev_set(o1, "falloff", ev_f_finite(reader, "falloff"));
  ev_set(o1, "density", ev_f_finite(reader, "density"));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_q2_height_fog(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "startColor", write_event_vec3(ev_get(value, "startColor")));
  ev_set(o1, "startDistance", ev_clone(ev_get(value, "startDistance")));
  ev_set(o1, "endColor", write_event_vec3(ev_get(value, "endColor")));
  ev_set(o1, "endDistance", ev_clone(ev_get(value, "endDistance")));
  ev_set(o1, "falloff", ev_clone(ev_get(value, "falloff")));
  ev_set(o1, "density", ev_clone(ev_get(value, "density")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q2_fog_state(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "fog", ev_read(reader, "fog", read_event_q2_fog, identity));
  ev_set(o1, "heightFog", ev_read(reader, "heightFog", read_event_q2_height_fog, identity));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_q2_fog_state(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "fog", write_event_q2_fog(ev_get(value, "fog")));
  ev_set(o1, "heightFog", write_event_q2_height_fog(ev_get(value, "heightFog")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q2_rerelease_level_entry(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "map", ev_f_string(reader, "map"));
  ev_set(o1, "name", ev_f_string(reader, "name"));
  ev_set(o1, "visitOrder", ev_f_finite(reader, "visitOrder"));
  ev_set(o1, "totalSecrets", ev_f_finite(reader, "totalSecrets"));
  ev_set(o1, "foundSecrets", ev_f_finite(reader, "foundSecrets"));
  ev_set(o1, "totalMonsters", ev_f_finite(reader, "totalMonsters"));
  ev_set(o1, "killedMonsters", ev_f_finite(reader, "killedMonsters"));
  ev_set(o1, "time", ev_f_finite(reader, "time"));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_q2_rerelease_level_entry(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "map", ev_clone(ev_get(value, "map")));
  ev_set(o1, "name", ev_clone(ev_get(value, "name")));
  ev_set(o1, "visitOrder", ev_clone(ev_get(value, "visitOrder")));
  ev_set(o1, "totalSecrets", ev_clone(ev_get(value, "totalSecrets")));
  ev_set(o1, "foundSecrets", ev_clone(ev_get(value, "foundSecrets")));
  ev_set(o1, "totalMonsters", ev_clone(ev_get(value, "totalMonsters")));
  ev_set(o1, "killedMonsters", ev_clone(ev_get(value, "killedMonsters")));
  ev_set(o1, "time", ev_clone(ev_get(value, "time")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q2_rerelease_event_end_of_unit_levels(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *result = ev_list(reader, read_event_q2_rerelease_level_entry, identity);
  return result;
}

static qa_save_value *write_event_q2_rerelease_event_end_of_unit_levels(const qa_save_value *value) {
  qa_save_value *result = ev_map(value, write_event_q2_rerelease_level_entry);
  return result;
}

static qa_save_value *read_event_q2_rerelease_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "debug-shapes")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"debug-shapes", 0}}, 1));
    ev_set(o2, "lines", ev_read(reader, "lines", read_event_q2_rerelease_event_debug_shapes_lines, identity));
    ev_set(o2, "lifetimeMilliseconds", ev_f_finite(reader, "lifetimeMilliseconds"));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "world-text")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"world-text", 0}}, 1));
    ev_set(o3, "text", ev_read(reader, "text", read_event_world_text_input, identity));
    ev_set(o3, "lifetime", ev_f_non_negative(reader, "lifetime"));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "localized-print")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"localized-print", 0}}, 1));
    ev_set(o4, "actor", ev_read(reader, "actor", read_event_q1_event_effect_actor, identity));
    ev_set(o4, "level", ev_f_choice(reader, "level", (const ev_lit[]){{"low", 0}, {"medium", 0}, {"high", 0}, {"chat", 0}}, 4));
    ev_set(o4, "text", ev_f_string(reader, "text"));
    ev_set(o4, "args", ev_read(reader, "args", read_event_q2_presentation_event_model_attached_models, identity));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "mission-objective")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"mission-objective", 0}}, 1));
    ev_set(o5, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o5, "text", ev_f_string(reader, "text"));
    ev_set(o5, "args", ev_read(reader, "args", read_event_q2_presentation_event_model_attached_models, identity));
    ev_set(o5, "talkSound", ev_f_boolean(reader, "talkSound"));
    qa_save_value *result = o5;
    return result;
  }
  if (ev_is(kind1, "mission-status")) {
    qa_save_value *o6 = ev_object();
    ev_set(o6, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"mission-status", 0}}, 1));
    ev_set(o6, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o6, "iconVisible", ev_f_boolean(reader, "iconVisible"));
    qa_save_value *result = o6;
    return result;
  }
  if (ev_is(kind1, "screen-blend")) {
    qa_save_value *o7 = ev_object();
    ev_set(o7, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"screen-blend", 0}}, 1));
    ev_set(o7, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o7, "blend", ev_read(reader, "blend", read_event_vec4, identity));
    qa_save_value *result = o7;
    return result;
  }
  if (ev_is(kind1, "help-computer")) {
    qa_save_value *o8 = ev_object();
    ev_set(o8, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"help-computer", 0}}, 1));
    ev_set(o8, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o8, "visible", ev_f_boolean(reader, "visible"));
    ev_set(o8, "primary", ev_f_string(reader, "primary"));
    ev_set(o8, "secondary", ev_f_string(reader, "secondary"));
    ev_set(o8, "slowTime", ev_f_boolean(reader, "slowTime"));
    qa_save_value *result = o8;
    return result;
  }
  if (ev_is(kind1, "fog")) {
    qa_save_value *o9 = ev_object();
    ev_set(o9, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"fog", 0}}, 1));
    ev_set(o9, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o9, "value", ev_read(reader, "value", read_event_q2_fog_state, identity));
    ev_set(o9, "transitionMilliseconds", ev_f_finite(reader, "transitionMilliseconds"));
    qa_save_value *result = o9;
    return result;
  }
  if (ev_is(kind1, "flashlight")) {
    qa_save_value *o10 = ev_object();
    ev_set(o10, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"flashlight", 0}}, 1));
    ev_set(o10, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o10, "enabled", ev_f_boolean(reader, "enabled"));
    ev_set(o10, "hand", ev_f_choice(reader, "hand", (const ev_lit[]){{"right", 0}, {"left", 0}, {"center", 0}}, 3));
    qa_save_value *result = o10;
    return result;
  }
  if (ev_is(kind1, "poi")) {
    qa_save_value *o11 = ev_object();
    ev_set(o11, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"poi", 0}}, 1));
    ev_set(o11, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o11, "position", ev_read(reader, "position", read_event_vec3, identity));
    ev_set(o11, "image", ev_f_string(reader, "image"));
    ev_set(o11, "duration", ev_f_finite(reader, "duration"));
    ev_set(o11, "color", ev_f_finite(reader, "color"));
    qa_save_value *result = o11;
    return result;
  }
  if (ev_is(kind1, "remove-poi")) {
    qa_save_value *o12 = ev_object();
    ev_set(o12, "kind", ev_string("remove-poi"));
    ev_set(o12, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o12, "key", ev_f_finite(reader, "key"));
    qa_save_value *result = o12;
    return result;
  }
  if (ev_is(kind1, "keyed-poi")) {
    qa_save_value *o13 = ev_object();
    ev_set(o13, "kind", ev_string("keyed-poi"));
    ev_set(o13, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o13, "key", ev_f_finite(reader, "key"));
    ev_set(o13, "position", ev_read(reader, "position", read_event_vec3, identity));
    ev_set(o13, "image", ev_f_string(reader, "image"));
    ev_set(o13, "duration", ev_f_finite(reader, "duration"));
    ev_set(o13, "color", ev_f_finite(reader, "color"));
    ev_set(o13, "flags", ev_f_finite(reader, "flags"));
    qa_save_value *result = o13;
    return result;
  }
  if (ev_is(kind1, "directional-damage")) {
    qa_save_value *o14 = ev_object();
    ev_set(o14, "kind", ev_string("directional-damage"));
    ev_set(o14, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o14, "direction", ev_read(reader, "direction", read_event_vec3, identity));
    ev_set(o14, "damage", ev_f_finite(reader, "damage"));
    ev_set(o14, "health", ev_f_boolean(reader, "health"));
    ev_set(o14, "armor", ev_f_boolean(reader, "armor"));
    ev_set(o14, "shield", ev_f_boolean(reader, "shield"));
    qa_save_value *result = o14;
    return result;
  }
  if (ev_is(kind1, "help-path")) {
    qa_save_value *o15 = ev_object();
    ev_set(o15, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"help-path", 0}}, 1));
    ev_set(o15, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o15, "first", ev_f_boolean(reader, "first"));
    ev_set(o15, "position", ev_read(reader, "position", read_event_vec3, identity));
    ev_set(o15, "direction", ev_read(reader, "direction", read_event_vec3, identity));
    qa_save_value *result = o15;
    return result;
  }
  if (ev_is(kind1, "coop-respawn")) {
    qa_save_value *o16 = ev_object();
    ev_set(o16, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"coop-respawn", 0}}, 1));
    ev_set(o16, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o16, "state", ev_f_choice(reader, "state", (const ev_lit[]){{"none", 0}, {"in-combat", 0}, {"bad-area", 0}, {"blocked", 0}, {"waiting", 0}, {"no-lives", 0}}, 6));
    ev_set(o16, "lives", ev_f_finite(reader, "lives"));
    qa_save_value *result = o16;
    return result;
  }
  if (ev_is(kind1, "autosave")) {
    qa_save_value *o17 = ev_object();
    ev_set(o17, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"autosave", 0}}, 1));
    qa_save_value *result = o17;
    return result;
  }
  if (ev_is(kind1, "alpha")) {
    qa_save_value *result = read_event_q1_addon_event_alpha(reader, identity);
    return result;
  }
  if (ev_is(kind1, "end-of-unit")) {
    qa_save_value *o18 = ev_object();
    ev_set(o18, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"end-of-unit", 0}}, 1));
    ev_set(o18, "levels", ev_read(reader, "levels", read_event_q2_rerelease_event_end_of_unit_levels, identity));
    ev_set(o18, "buttonTime", ev_f_finite(reader, "buttonTime"));
    qa_save_value *result = o18;
    return result;
  }
  if (ev_is(kind1, "player-dogtag")) {
    qa_save_value *o19 = ev_object();
    ev_set(o19, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"player-dogtag", 0}}, 1));
    ev_set(o19, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o19, "value", ev_f_string(reader, "value"));
    qa_save_value *result = o19;
    return result;
  }
  if (ev_is(kind1, "dynamic-light")) {
    qa_save_value *o20 = ev_object();
    ev_set(o20, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"dynamic-light", 0}}, 1));
    ev_set(o20, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o20, "origin", ev_read(reader, "origin", read_event_vec3, identity));
    ev_set(o20, "radius", ev_f_finite(reader, "radius"));
    ev_set(o20, "color", ev_read(reader, "color", read_event_vec3, identity));
    ev_set(o20, "visible", ev_f_boolean(reader, "visible"));
    qa_save_value *result = o20;
    return result;
  }
  if (ev_is(kind1, "restart-level")) {
    qa_save_value *o21 = ev_object();
    ev_set(o21, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"restart-level", 0}}, 1));
    ev_set(o21, "map", ev_f_string(reader, "map"));
    qa_save_value *result = o21;
    return result;
  }
  if (ev_is(kind1, "story")) {
    qa_save_value *o22 = ev_object();
    ev_set(o22, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"story", 0}}, 1));
    ev_set(o22, "text", ev_f_string(reader, "text"));
    qa_save_value *result = o22;
    return result;
  }
  if (ev_is(kind1, "achievement")) {
    qa_save_value *o23 = ev_object();
    ev_set(o23, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"achievement", 0}}, 1));
    ev_set(o23, "id", ev_f_string(reader, "id"));
    qa_save_value *result = o23;
    return result;
  }
  if (ev_is(kind1, "sky")) {
    qa_save_value *o24 = ev_object();
    ev_set(o24, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"sky", 0}}, 1));
    ev_set(o24, "name", ev_f_string(reader, "name"));
    ev_set(o24, "rotation", ev_f_finite(reader, "rotation"));
    ev_set(o24, "autoRotate", ev_f_boolean(reader, "autoRotate"));
    ev_set(o24, "axis", ev_read(reader, "axis", read_event_vec3, identity));
    qa_save_value *result = o24;
    return result;
  }
  if (ev_is(kind1, "healthbar")) {
    qa_save_value *o25 = ev_object();
    ev_set(o25, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"healthbar", 0}}, 1));
    ev_set(o25, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o25, "slot", ev_f_finite(reader, "slot"));
    ev_set(o25, "target", ev_read(reader, "target", read_actor_id, identity));
    ev_set(o25, "name", ev_f_string(reader, "name"));
    ev_set(o25, "fraction", ev_f_finite(reader, "fraction"));
    ev_set(o25, "visible", ev_f_boolean(reader, "visible"));
    qa_save_value *result = o25;
    return result;
  }
  if (ev_is(kind1, "item-visibility")) {
    qa_save_value *o26 = ev_object();
    ev_set(o26, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"item-visibility", 0}}, 1));
    ev_set(o26, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o26, "item", ev_read(reader, "item", read_actor_id, identity));
    ev_set(o26, "visible", ev_f_boolean(reader, "visible"));
    qa_save_value *result = o26;
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_q2_rerelease_event(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "debug-shapes")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o2, "lines", write_event_q2_rerelease_event_debug_shapes_lines(ev_get(value, "lines")));
    ev_set(o2, "lifetimeMilliseconds", ev_clone(ev_get(value, "lifetimeMilliseconds")));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "world-text")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o3, "text", write_event_world_text_input(ev_get(value, "text")));
    ev_set(o3, "lifetime", ev_clone(ev_get(value, "lifetime")));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "localized-print")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o4, "actor", write_event_q1_event_effect_actor(ev_get(value, "actor")));
    ev_set(o4, "level", ev_clone(ev_get(value, "level")));
    ev_set(o4, "text", ev_clone(ev_get(value, "text")));
    ev_set(o4, "args", write_event_q2_presentation_event_model_attached_models(ev_get(value, "args")));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "mission-objective")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o5, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o5, "text", ev_clone(ev_get(value, "text")));
    ev_set(o5, "args", write_event_q2_presentation_event_model_attached_models(ev_get(value, "args")));
    ev_set(o5, "talkSound", ev_clone(ev_get(value, "talkSound")));
    qa_save_value *result = o5;
    return result;
  }
  if (ev_is(kind1, "mission-status")) {
    qa_save_value *o6 = ev_object();
    ev_set(o6, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o6, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o6, "iconVisible", ev_clone(ev_get(value, "iconVisible")));
    qa_save_value *result = o6;
    return result;
  }
  if (ev_is(kind1, "screen-blend")) {
    qa_save_value *o7 = ev_object();
    ev_set(o7, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o7, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o7, "blend", write_event_vec4(ev_get(value, "blend")));
    qa_save_value *result = o7;
    return result;
  }
  if (ev_is(kind1, "help-computer")) {
    qa_save_value *o8 = ev_object();
    ev_set(o8, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o8, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o8, "visible", ev_clone(ev_get(value, "visible")));
    ev_set(o8, "primary", ev_clone(ev_get(value, "primary")));
    ev_set(o8, "secondary", ev_clone(ev_get(value, "secondary")));
    ev_set(o8, "slowTime", ev_clone(ev_get(value, "slowTime")));
    qa_save_value *result = o8;
    return result;
  }
  if (ev_is(kind1, "fog")) {
    qa_save_value *o9 = ev_object();
    ev_set(o9, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o9, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o9, "value", write_event_q2_fog_state(ev_get(value, "value")));
    ev_set(o9, "transitionMilliseconds", ev_clone(ev_get(value, "transitionMilliseconds")));
    qa_save_value *result = o9;
    return result;
  }
  if (ev_is(kind1, "flashlight")) {
    qa_save_value *o10 = ev_object();
    ev_set(o10, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o10, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o10, "enabled", ev_clone(ev_get(value, "enabled")));
    ev_set(o10, "hand", ev_clone(ev_get(value, "hand")));
    qa_save_value *result = o10;
    return result;
  }
  if (ev_is(kind1, "poi")) {
    qa_save_value *o11 = ev_object();
    ev_set(o11, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o11, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o11, "position", write_event_vec3(ev_get(value, "position")));
    ev_set(o11, "image", ev_clone(ev_get(value, "image")));
    ev_set(o11, "duration", ev_clone(ev_get(value, "duration")));
    ev_set(o11, "color", ev_clone(ev_get(value, "color")));
    qa_save_value *result = o11;
    return result;
  }
  if (ev_is(kind1, "remove-poi")) {
    qa_save_value *o12 = ev_object();
    ev_spread(o12, value);
    ev_set(o12, "actor", write_actor_id(ev_get(value, "actor")));
    qa_save_value *result = o12;
    return result;
  }
  if (ev_is(kind1, "keyed-poi")) {
    qa_save_value *o13 = ev_object();
    ev_spread(o13, value);
    ev_set(o13, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o13, "position", write_event_vec3(ev_get(value, "position")));
    qa_save_value *result = o13;
    return result;
  }
  if (ev_is(kind1, "directional-damage")) {
    qa_save_value *o14 = ev_object();
    ev_spread(o14, value);
    ev_set(o14, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o14, "direction", write_event_vec3(ev_get(value, "direction")));
    qa_save_value *result = o14;
    return result;
  }
  if (ev_is(kind1, "help-path")) {
    qa_save_value *o15 = ev_object();
    ev_set(o15, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o15, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o15, "first", ev_clone(ev_get(value, "first")));
    ev_set(o15, "position", write_event_vec3(ev_get(value, "position")));
    ev_set(o15, "direction", write_event_vec3(ev_get(value, "direction")));
    qa_save_value *result = o15;
    return result;
  }
  if (ev_is(kind1, "coop-respawn")) {
    qa_save_value *o16 = ev_object();
    ev_set(o16, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o16, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o16, "state", ev_clone(ev_get(value, "state")));
    ev_set(o16, "lives", ev_clone(ev_get(value, "lives")));
    qa_save_value *result = o16;
    return result;
  }
  if (ev_is(kind1, "autosave")) {
    qa_save_value *o17 = ev_object();
    ev_set(o17, "kind", ev_clone(ev_get(value, "kind")));
    qa_save_value *result = o17;
    return result;
  }
  if (ev_is(kind1, "alpha")) {
    qa_save_value *result = write_event_q1_addon_event_alpha(value);
    return result;
  }
  if (ev_is(kind1, "end-of-unit")) {
    qa_save_value *o18 = ev_object();
    ev_set(o18, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o18, "levels", write_event_q2_rerelease_event_end_of_unit_levels(ev_get(value, "levels")));
    ev_set(o18, "buttonTime", ev_clone(ev_get(value, "buttonTime")));
    qa_save_value *result = o18;
    return result;
  }
  if (ev_is(kind1, "player-dogtag")) {
    qa_save_value *o19 = ev_object();
    ev_set(o19, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o19, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o19, "value", ev_clone(ev_get(value, "value")));
    qa_save_value *result = o19;
    return result;
  }
  if (ev_is(kind1, "dynamic-light")) {
    qa_save_value *o20 = ev_object();
    ev_set(o20, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o20, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o20, "origin", write_event_vec3(ev_get(value, "origin")));
    ev_set(o20, "radius", ev_clone(ev_get(value, "radius")));
    ev_set(o20, "color", write_event_vec3(ev_get(value, "color")));
    ev_set(o20, "visible", ev_clone(ev_get(value, "visible")));
    qa_save_value *result = o20;
    return result;
  }
  if (ev_is(kind1, "restart-level")) {
    qa_save_value *o21 = ev_object();
    ev_set(o21, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o21, "map", ev_clone(ev_get(value, "map")));
    qa_save_value *result = o21;
    return result;
  }
  if (ev_is(kind1, "story")) {
    qa_save_value *o22 = ev_object();
    ev_set(o22, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o22, "text", ev_clone(ev_get(value, "text")));
    qa_save_value *result = o22;
    return result;
  }
  if (ev_is(kind1, "achievement")) {
    qa_save_value *o23 = ev_object();
    ev_set(o23, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o23, "id", ev_clone(ev_get(value, "id")));
    qa_save_value *result = o23;
    return result;
  }
  if (ev_is(kind1, "sky")) {
    qa_save_value *o24 = ev_object();
    ev_set(o24, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o24, "name", ev_clone(ev_get(value, "name")));
    ev_set(o24, "rotation", ev_clone(ev_get(value, "rotation")));
    ev_set(o24, "autoRotate", ev_clone(ev_get(value, "autoRotate")));
    ev_set(o24, "axis", write_event_vec3(ev_get(value, "axis")));
    qa_save_value *result = o24;
    return result;
  }
  if (ev_is(kind1, "healthbar")) {
    qa_save_value *o25 = ev_object();
    ev_set(o25, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o25, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o25, "slot", ev_clone(ev_get(value, "slot")));
    ev_set(o25, "target", write_actor_id(ev_get(value, "target")));
    ev_set(o25, "name", ev_clone(ev_get(value, "name")));
    ev_set(o25, "fraction", ev_clone(ev_get(value, "fraction")));
    ev_set(o25, "visible", ev_clone(ev_get(value, "visible")));
    qa_save_value *result = o25;
    return result;
  }
  if (ev_is(kind1, "item-visibility")) {
    qa_save_value *o26 = ev_object();
    ev_set(o26, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o26, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o26, "item", write_actor_id(ev_get(value, "item")));
    ev_set(o26, "visible", ev_clone(ev_get(value, "visible")));
    qa_save_value *result = o26;
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_q2_player_view_selected_item(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *c1;
  if (ev_r_null(reader)) {
    c1 = ev_null();
  } else {
    c1 = read_item_id(reader, identity);
  }
  qa_save_value *result = c1;
  return result;
}

static qa_save_value *write_event_q2_player_view_selected_item(const qa_save_value *value) {
  qa_save_value *c1;
  if (ev_null_is(value)) {
    c1 = ev_null();
  } else {
    c1 = write_item_id(value);
  }
  qa_save_value *result = c1;
  return result;
}

static qa_save_value *read_event_q2_player_view_timer_value(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "item", ev_read(reader, "item", read_item_id, identity));
  ev_set(o1, "seconds", ev_f_finite(reader, "seconds"));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_q2_player_view_timer_value(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "item", write_item_id(ev_get(value, "item")));
  ev_set(o1, "seconds", ev_clone(ev_get(value, "seconds")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q2_player_view_timer(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *c1;
  if (ev_r_null(reader)) {
    c1 = ev_null();
  } else {
    c1 = read_event_q2_player_view_timer_value(reader, identity);
  }
  qa_save_value *result = c1;
  return result;
}

static qa_save_value *write_event_q2_player_view_timer(const qa_save_value *value) {
  qa_save_value *c1;
  if (ev_null_is(value)) {
    c1 = ev_null();
  } else {
    c1 = write_event_q2_player_view_timer_value(value);
  }
  qa_save_value *result = c1;
  return result;
}

static qa_save_value *read_event_q2_player_view(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "angles", ev_read(reader, "angles", read_event_vec3, identity));
  ev_set(o1, "offset", ev_read(reader, "offset", read_event_vec3, identity));
  ev_set(o1, "kickAngles", ev_read(reader, "kickAngles", read_event_vec3, identity));
  ev_set(o1, "gunAngles", ev_read(reader, "gunAngles", read_event_vec3, identity));
  ev_set(o1, "gunOffset", ev_read(reader, "gunOffset", read_event_vec3, identity));
  ev_set(o1, "blend", ev_read(reader, "blend", read_event_vec4, identity));
  ev_set(o1, "fov", ev_f_finite(reader, "fov"));
  ev_set(o1, "underwater", ev_f_boolean(reader, "underwater"));
  ev_set(o1, "flashes", ev_f_finite(reader, "flashes"));
  ev_set(o1, "health", ev_f_finite(reader, "health"));
  ev_set(o1, "armor", ev_f_finite(reader, "armor"));
  ev_set(o1, "ammo", ev_f_finite(reader, "ammo"));
  ev_set(o1, "score", ev_f_finite(reader, "score"));
  ev_set(o1, "selectedItem", ev_read(reader, "selectedItem", read_event_q2_player_view_selected_item, identity));
  ev_set(o1, "timer", ev_read(reader, "timer", read_event_q2_player_view_timer, identity));
  ev_set(o1, "spectator", ev_f_boolean(reader, "spectator"));
  ev_set(o1, "layouts", ev_f_finite(reader, "layouts"));
  if (!ev_f_undefined(reader, "helpIcon")) {
    ev_set(o1, "helpIcon", ev_f_boolean(reader, "helpIcon"));
  }
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_q2_player_view(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "angles", write_event_vec3(ev_get(value, "angles")));
  ev_set(o1, "offset", write_event_vec3(ev_get(value, "offset")));
  ev_set(o1, "kickAngles", write_event_vec3(ev_get(value, "kickAngles")));
  ev_set(o1, "gunAngles", write_event_vec3(ev_get(value, "gunAngles")));
  ev_set(o1, "gunOffset", write_event_vec3(ev_get(value, "gunOffset")));
  ev_set(o1, "blend", write_event_vec4(ev_get(value, "blend")));
  ev_set(o1, "fov", ev_clone(ev_get(value, "fov")));
  ev_set(o1, "underwater", ev_clone(ev_get(value, "underwater")));
  ev_set(o1, "flashes", ev_clone(ev_get(value, "flashes")));
  ev_set(o1, "health", ev_clone(ev_get(value, "health")));
  ev_set(o1, "armor", ev_clone(ev_get(value, "armor")));
  ev_set(o1, "ammo", ev_clone(ev_get(value, "ammo")));
  ev_set(o1, "score", ev_clone(ev_get(value, "score")));
  ev_set(o1, "selectedItem", write_event_q2_player_view_selected_item(ev_get(value, "selectedItem")));
  ev_set(o1, "timer", write_event_q2_player_view_timer(ev_get(value, "timer")));
  ev_set(o1, "spectator", ev_clone(ev_get(value, "spectator")));
  ev_set(o1, "layouts", ev_clone(ev_get(value, "layouts")));
  if (!ev_undefined_is(ev_get(value, "helpIcon"))) {
    ev_set(o1, "helpIcon", ev_clone(ev_get(value, "helpIcon")));
  }
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q2_score_row(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "slot", ev_f_finite(reader, "slot"));
  ev_set(o1, "name", ev_f_string(reader, "name"));
  ev_set(o1, "score", ev_f_finite(reader, "score"));
  ev_set(o1, "ping", ev_f_finite(reader, "ping"));
  ev_set(o1, "minutes", ev_f_finite(reader, "minutes"));
  ev_set(o1, "spectator", ev_f_boolean(reader, "spectator"));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_q2_score_row(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "slot", ev_clone(ev_get(value, "slot")));
  ev_set(o1, "name", ev_clone(ev_get(value, "name")));
  ev_set(o1, "score", ev_clone(ev_get(value, "score")));
  ev_set(o1, "ping", ev_clone(ev_get(value, "ping")));
  ev_set(o1, "minutes", ev_clone(ev_get(value, "minutes")));
  ev_set(o1, "spectator", ev_clone(ev_get(value, "spectator")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q2_player_event_scoreboard_rows(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *result = ev_list(reader, read_event_q2_score_row, identity);
  return result;
}

static qa_save_value *write_event_q2_player_event_scoreboard_rows(const qa_save_value *value) {
  qa_save_value *result = ev_map(value, write_event_q2_score_row);
  return result;
}

static qa_save_value *read_event_inventory_count_policy(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "stack")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"stack", 0}}, 1));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "source-counter")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"source-counter", 0}}, 1));
    ev_set(o3, "arithmetic", ev_f_choice(reader, "arithmetic", (const ev_lit[]){{"binary32", 0}, {"binary64", 0}, {"int32", 0}}, 3));
    qa_save_value *result = o3;
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_inventory_count_policy(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "stack")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "source-counter")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o3, "arithmetic", ev_clone(ev_get(value, "arithmetic")));
    qa_save_value *result = o3;
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_inventory_entry(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "item", ev_read(reader, "item", read_item_id, identity));
  ev_set(o1, "count", ev_f_finite(reader, "count"));
  ev_set(o1, "capacity", ev_f_finite(reader, "capacity"));
  if (!ev_f_undefined(reader, "countPolicy")) {
    ev_set(o1, "countPolicy", ev_read(reader, "countPolicy", read_event_inventory_count_policy, identity));
  }
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_inventory_entry(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "item", write_item_id(ev_get(value, "item")));
  ev_set(o1, "count", ev_clone(ev_get(value, "count")));
  ev_set(o1, "capacity", ev_clone(ev_get(value, "capacity")));
  if (!ev_undefined_is(ev_get(value, "countPolicy"))) {
    ev_set(o1, "countPolicy", write_event_inventory_count_policy(ev_get(value, "countPolicy")));
  }
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q2_player_event_inventory_entries(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *result = ev_list(reader, read_event_inventory_entry, identity);
  return result;
}

static qa_save_value *write_event_q2_player_event_inventory_entries(const qa_save_value *value) {
  qa_save_value *result = ev_map(value, write_event_inventory_entry);
  return result;
}

static qa_save_value *read_event_q2_player_event_inventory_labels_item(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "item", ev_read(reader, "item", read_item_id, identity));
  ev_set(o1, "name", ev_f_string(reader, "name"));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_q2_player_event_inventory_labels_item(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "item", write_item_id(ev_get(value, "item")));
  ev_set(o1, "name", ev_clone(ev_get(value, "name")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q2_player_event_inventory_labels(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *result = ev_list(reader, read_event_q2_player_event_inventory_labels_item, identity);
  return result;
}

static qa_save_value *write_event_q2_player_event_inventory_labels(const qa_save_value *value) {
  qa_save_value *result = ev_map(value, write_event_q2_player_event_inventory_labels_item);
  return result;
}

static qa_save_value *read_event_q2_player_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "print")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"print", 0}}, 1));
    ev_set(o2, "target", ev_read(reader, "target", read_event_q1_event_effect_actor, identity));
    ev_set(o2, "level", ev_f_choice(reader, "level", (const ev_lit[]){{"low", 0}, {"medium", 0}, {"high", 0}, {"chat", 0}}, 4));
    ev_set(o2, "text", ev_f_string(reader, "text"));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "userinfo")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"userinfo", 0}}, 1));
    ev_set(o3, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o3, "slot", ev_f_finite(reader, "slot"));
    ev_set(o3, "name", ev_f_string(reader, "name"));
    ev_set(o3, "skin", ev_f_string(reader, "skin"));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "stufftext")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"stufftext", 0}}, 1));
    ev_set(o4, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o4, "text", ev_f_string(reader, "text"));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "view")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"view", 0}}, 1));
    ev_set(o5, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o5, "view", ev_read(reader, "view", read_event_q2_player_view, identity));
    qa_save_value *result = o5;
    return result;
  }
  if (ev_is(kind1, "scoreboard")) {
    qa_save_value *o6 = ev_object();
    ev_set(o6, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"scoreboard", 0}}, 1));
    ev_set(o6, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o6, "rows", ev_read(reader, "rows", read_event_q2_player_event_scoreboard_rows, identity));
    ev_set(o6, "killer", ev_read(reader, "killer", read_event_q1_event_effect_actor, identity));
    ev_set(o6, "reliable", ev_f_boolean(reader, "reliable"));
    qa_save_value *result = o6;
    return result;
  }
  if (ev_is(kind1, "inventory")) {
    qa_save_value *o7 = ev_object();
    ev_set(o7, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"inventory", 0}}, 1));
    ev_set(o7, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o7, "entries", ev_read(reader, "entries", read_event_q2_player_event_inventory_entries, identity));
    if (!ev_f_undefined(reader, "visible")) {
      ev_set(o7, "visible", ev_f_boolean(reader, "visible"));
    }
    if (!ev_f_undefined(reader, "selected")) {
      ev_set(o7, "selected", ev_read(reader, "selected", read_event_q2_player_view_selected_item, identity));
    }
    if (!ev_f_undefined(reader, "labels")) {
      ev_set(o7, "labels", ev_read(reader, "labels", read_event_q2_player_event_inventory_labels, identity));
    }
    qa_save_value *result = o7;
    return result;
  }
  if (ev_is(kind1, "help")) {
    qa_save_value *o8 = ev_object();
    ev_set(o8, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"help", 0}}, 1));
    ev_set(o8, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o8, "visible", ev_f_boolean(reader, "visible"));
    qa_save_value *result = o8;
    return result;
  }
  if (ev_is(kind1, "load-menu")) {
    qa_save_value *o9 = ev_object();
    ev_set(o9, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"load-menu", 0}}, 1));
    ev_set(o9, "actor", ev_read(reader, "actor", read_actor_id, identity));
    qa_save_value *result = o9;
    return result;
  }
  if (ev_is(kind1, "trail")) {
    qa_save_value *o10 = ev_object();
    ev_set(o10, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"trail", 0}}, 1));
    ev_set(o10, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o10, "origin", ev_read(reader, "origin", read_event_vec3, identity));
    ev_set(o10, "time", ev_f_finite(reader, "time"));
    qa_save_value *result = o10;
    return result;
  }
  if (ev_is(kind1, "chase")) {
    qa_save_value *o11 = ev_object();
    ev_set(o11, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"chase", 0}}, 1));
    ev_set(o11, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o11, "target", ev_read(reader, "target", read_event_q1_event_effect_actor, identity));
    qa_save_value *result = o11;
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_q2_player_event(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "print")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o2, "target", write_event_q1_event_effect_actor(ev_get(value, "target")));
    ev_set(o2, "level", ev_clone(ev_get(value, "level")));
    ev_set(o2, "text", ev_clone(ev_get(value, "text")));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "userinfo")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o3, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o3, "slot", ev_clone(ev_get(value, "slot")));
    ev_set(o3, "name", ev_clone(ev_get(value, "name")));
    ev_set(o3, "skin", ev_clone(ev_get(value, "skin")));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "stufftext")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o4, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o4, "text", ev_clone(ev_get(value, "text")));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "view")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o5, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o5, "view", write_event_q2_player_view(ev_get(value, "view")));
    qa_save_value *result = o5;
    return result;
  }
  if (ev_is(kind1, "scoreboard")) {
    qa_save_value *o6 = ev_object();
    ev_set(o6, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o6, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o6, "rows", write_event_q2_player_event_scoreboard_rows(ev_get(value, "rows")));
    ev_set(o6, "killer", write_event_q1_event_effect_actor(ev_get(value, "killer")));
    ev_set(o6, "reliable", ev_clone(ev_get(value, "reliable")));
    qa_save_value *result = o6;
    return result;
  }
  if (ev_is(kind1, "inventory")) {
    qa_save_value *o7 = ev_object();
    ev_set(o7, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o7, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o7, "entries", write_event_q2_player_event_inventory_entries(ev_get(value, "entries")));
    if (!ev_undefined_is(ev_get(value, "visible"))) {
      ev_set(o7, "visible", ev_clone(ev_get(value, "visible")));
    }
    if (!ev_undefined_is(ev_get(value, "selected"))) {
      ev_set(o7, "selected", write_event_q2_player_view_selected_item(ev_get(value, "selected")));
    }
    if (!ev_undefined_is(ev_get(value, "labels"))) {
      ev_set(o7, "labels", write_event_q2_player_event_inventory_labels(ev_get(value, "labels")));
    }
    qa_save_value *result = o7;
    return result;
  }
  if (ev_is(kind1, "help")) {
    qa_save_value *o8 = ev_object();
    ev_set(o8, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o8, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o8, "visible", ev_clone(ev_get(value, "visible")));
    qa_save_value *result = o8;
    return result;
  }
  if (ev_is(kind1, "load-menu")) {
    qa_save_value *o9 = ev_object();
    ev_set(o9, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o9, "actor", write_actor_id(ev_get(value, "actor")));
    qa_save_value *result = o9;
    return result;
  }
  if (ev_is(kind1, "trail")) {
    qa_save_value *o10 = ev_object();
    ev_set(o10, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o10, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o10, "origin", write_event_vec3(ev_get(value, "origin")));
    ev_set(o10, "time", ev_clone(ev_get(value, "time")));
    qa_save_value *result = o10;
    return result;
  }
  if (ev_is(kind1, "chase")) {
    qa_save_value *o11 = ev_object();
    ev_set(o11, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o11, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o11, "target", write_event_q1_event_effect_actor(ev_get(value, "target")));
    qa_save_value *result = o11;
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_q3_character_presentation_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "sequence", ev_f_finite(reader, "sequence"));
  ev_set(o1, "timeMilliseconds", ev_f_finite(reader, "timeMilliseconds"));
  ev_set(o1, "event", ev_f_finite(reader, "event"));
  ev_set(o1, "parameter", ev_f_finite(reader, "parameter"));
  ev_set(o1, "actor", ev_read(reader, "actor", read_actor_id, identity));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_q3_character_presentation_event(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "sequence", ev_clone(ev_get(value, "sequence")));
  ev_set(o1, "timeMilliseconds", ev_clone(ev_get(value, "timeMilliseconds")));
  ev_set(o1, "event", ev_clone(ev_get(value, "event")));
  ev_set(o1, "parameter", ev_clone(ev_get(value, "parameter")));
  ev_set(o1, "actor", write_actor_id(ev_get(value, "actor")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q3_source_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "print") || ev_is(kind1, "log")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_f_choice(reader, "kind", (const ev_lit[]){{"print", 0}, {"log", 0}}, 2));
    ev_set(o2, "text", ev_f_string(reader, "text"));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "sound")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_string("sound"));
    ev_set(o3, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o3, "origin", ev_read(reader, "origin", read_event_vec3, identity));
    ev_set(o3, "velocity", ev_read(reader, "velocity", read_event_vec3, identity));
    ev_set(o3, "path", ev_f_string(reader, "path"));
    ev_set(o3, "channel", ev_f_integer(reader, "channel", 0.0));
    ev_set(o3, "volume", ev_f_finite(reader, "volume"));
    ev_set(o3, "loop", ev_f_boolean(reader, "loop"));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "server-command")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"server-command", 0}}, 1));
    ev_set(o4, "client", ev_f_finite(reader, "client"));
    ev_set(o4, "text", ev_f_string(reader, "text"));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "console-command")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"console-command", 0}}, 1));
    ev_set(o5, "execution", ev_f_choice(reader, "execution", (const ev_lit[]){{"append", 0}, {"now", 0}}, 2));
    ev_set(o5, "text", ev_f_string(reader, "text"));
    qa_save_value *result = o5;
    return result;
  }
  if (ev_is(kind1, "drop-client")) {
    qa_save_value *o6 = ev_object();
    ev_set(o6, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"drop-client", 0}}, 1));
    ev_set(o6, "client", ev_f_finite(reader, "client"));
    ev_set(o6, "reason", ev_f_string(reader, "reason"));
    qa_save_value *result = o6;
    return result;
  }
  if (ev_is(kind1, "configstring")) {
    qa_save_value *o7 = ev_object();
    ev_set(o7, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"configstring", 0}}, 1));
    ev_set(o7, "index", ev_f_finite(reader, "index"));
    ev_set(o7, "value", ev_f_string(reader, "value"));
    qa_save_value *result = o7;
    return result;
  }
  if (ev_is(kind1, "player-event")) {
    qa_save_reader sequence;
    ev_field(reader, "sequence", &sequence);
    qa_save_reader source;
    ev_field(reader, "source", &source);
    qa_save_value *o8 = ev_object();
    ev_set(o8, "kind", ev_string("player-event"));
    ev_set(o8, "actor", ev_read(reader, "actor", read_actor_id, identity));
    qa_save_value *o9 = ev_object();
    qa_save_reader f10;
    ev_field(&source, "module", &f10);
    ev_set(o9, "module", read_module(&f10));
    ev_set(o9, "abiProfile", ev_f_choice(&source, "abiProfile", (const ev_lit[]){{"q3-modern", 0}, {"q3-1.16n-base", 0}}, 2));
    ev_set(o8, "source", o9);
    ev_set(o8, "playerState", ev_read(reader, "playerState", read_source_player_state, identity));
    qa_save_reader f11;
    ev_field(reader, "event", &f11);
    ev_set(o8, "event", source_client_integer(&f11, -2147483648.0, 2147483647.0));
    qa_save_reader f12;
    ev_field(reader, "parameter", &f12);
    ev_set(o8, "parameter", source_client_integer(&f12, -2147483648.0, 2147483647.0));
    qa_save_value *c13;
    if (ev_take_eq(ev_f_choice(&sequence, "kind", (const ev_lit[]){{"external", 0}, {"predictable", 0}}, 2), "external")) {
      qa_save_value *o14 = ev_object();
      ev_set(o14, "kind", ev_string("external"));
      qa_save_reader f15;
      ev_field(&sequence, "time", &f15);
      ev_set(o14, "time", source_client_integer(&f15, -2147483648.0, 2147483647.0));
      c13 = o14;
      qa_save_reader_destroy(&f15);
    } else {
      qa_save_value *o16 = ev_object();
      ev_set(o16, "kind", ev_string("predictable"));
      qa_save_reader f17;
      ev_field(&sequence, "sequence", &f17);
      ev_set(o16, "sequence", source_client_integer(&f17, -2147483648.0, 2147483647.0));
      c13 = o16;
      qa_save_reader_destroy(&f17);
    }
    ev_set(o8, "sequence", c13);
    ev_set(o8, "origin", ev_read(reader, "origin", read_event_vec3, identity));
    ev_set(o8, "time", ev_f_finite(reader, "time"));
    qa_save_value *result = o8;
    qa_save_reader_destroy(&f12);
    qa_save_reader_destroy(&f11);
    qa_save_reader_destroy(&f10);
    qa_save_reader_destroy(&source);
    qa_save_reader_destroy(&sequence);
    return result;
  }
  if (ev_is(kind1, "entity-event")) {
    qa_save_value *o18 = ev_object();
    ev_set(o18, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"entity-event", 0}}, 1));
    ev_set(o18, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o18, "state", ev_read(reader, "state", read_entity_state, identity));
    ev_set(o18, "origin", ev_read(reader, "origin", read_event_vec3, identity));
    ev_set(o18, "time", ev_f_finite(reader, "time"));
    qa_save_value *result = o18;
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_q3_source_event(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "print") || ev_is(kind1, "log")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o2, "text", ev_clone(ev_get(value, "text")));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "sound")) {
    qa_save_value *o3 = ev_object();
    ev_spread(o3, value);
    ev_set(o3, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o3, "origin", write_event_vec3(ev_get(value, "origin")));
    ev_set(o3, "velocity", write_event_vec3(ev_get(value, "velocity")));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "server-command")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o4, "client", ev_clone(ev_get(value, "client")));
    ev_set(o4, "text", ev_clone(ev_get(value, "text")));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "console-command")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o5, "execution", ev_clone(ev_get(value, "execution")));
    ev_set(o5, "text", ev_clone(ev_get(value, "text")));
    qa_save_value *result = o5;
    return result;
  }
  if (ev_is(kind1, "drop-client")) {
    qa_save_value *o6 = ev_object();
    ev_set(o6, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o6, "client", ev_clone(ev_get(value, "client")));
    ev_set(o6, "reason", ev_clone(ev_get(value, "reason")));
    qa_save_value *result = o6;
    return result;
  }
  if (ev_is(kind1, "configstring")) {
    qa_save_value *o7 = ev_object();
    ev_set(o7, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o7, "index", ev_clone(ev_get(value, "index")));
    ev_set(o7, "value", ev_clone(ev_get(value, "value")));
    qa_save_value *result = o7;
    return result;
  }
  if (ev_is(kind1, "player-event")) {
    qa_save_value *o8 = ev_object();
    ev_spread(o8, value);
    ev_set(o8, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o8, "playerState", write_source_player_state(ev_get(value, "playerState")));
    ev_set(o8, "origin", write_event_vec3(ev_get(value, "origin")));
    qa_save_value *result = o8;
    return result;
  }
  if (ev_is(kind1, "entity-event")) {
    qa_save_value *o9 = ev_object();
    ev_set(o9, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o9, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o9, "state", write_entity_state(ev_get(value, "state")));
    ev_set(o9, "origin", write_event_vec3(ev_get(value, "origin")));
    ev_set(o9, "time", ev_clone(ev_get(value, "time")));
    qa_save_value *result = o9;
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_q1_client_metadata(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *kind = ev_f_choice(reader, "kind", (const ev_lit[]){{"name", 0}, {"social", 0}, {"player-info", 0}, {"colors", 0}, {"frags", 0}, {"ping", 0}}, 6);
  qa_save_reader f1;
  ev_field(reader, "slot", &f1);
  qa_save_value *slot = source_client_integer(&f1, 0.0, 255.0);
  if (((ev_eq(kind, "name") || ev_eq(kind, "social")) || ev_eq(kind, "player-info"))) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(kind));
    ev_set(o2, "slot", ev_clone(slot));
    ev_set(o2, "value", ev_f_string(reader, "value"));
    qa_save_value *result = o2;
    qa_save_value_free(slot);
    qa_save_reader_destroy(&f1);
    qa_save_value_free(kind);
    return result;
  }
  qa_save_value *o3 = ev_object();
  ev_set(o3, "kind", ev_clone(kind));
  ev_set(o3, "slot", ev_clone(slot));
  qa_save_reader f4;
  ev_field(reader, "value", &f4);
  ev_set(o3, "value", source_client_integer(&f4, -32768.0, 32767.0));
  qa_save_value *result = o3;
  qa_save_reader_destroy(&f4);
  qa_save_value_free(slot);
  qa_save_reader_destroy(&f1);
  qa_save_value_free(kind);
  return result;
}

static qa_save_value *read_presentation_owner(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *generation = ev_f_integer(reader, "generation", 1.0);
  if (!ev_safe_integer(generation)) {
    qa_save_reader_fail(reader, "%s", "Invalid presentation generation");
  }
  qa_save_value *o1 = ev_object();
  qa_save_reader f2;
  ev_field(reader, "provider", &f2);
  ev_set(o1, "provider", ev_namespaced(&f2));
  ev_set(o1, "generation", ev_clone(generation));
  qa_save_value *result = o1;
  qa_save_reader_destroy(&f2);
  qa_save_value_free(generation);
  return result;
}

static qa_save_value *read_event_simulation_presentation_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *event = read_source_presentation_event(reader, identity);
  qa_save_reader owner;
  ev_field(reader, "owner", &owner);
  qa_save_value *c1;
  if (ev_r_undefined(&owner)) {
    c1 = ev_clone(event);
  } else {
    qa_save_value *o2 = ev_object();
    ev_spread(o2, event);
    ev_set(o2, "owner", read_presentation_owner(&owner, identity));
    c1 = o2;
  }
  qa_save_value *result = c1;
  qa_save_reader_destroy(&owner);
  qa_save_value_free(event);
  return result;
}

static qa_save_value *read_source_presentation_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "debug-graph")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_string("debug-graph"));
    qa_save_value *o3 = ev_object();
    qa_save_reader f4;
    ev_field(reader, "event", &f4);
    ev_set(o3, "kind", ev_f_literal(&f4, "kind", (const ev_lit[]){{"sample", 0}}, 1));
    qa_save_reader f5;
    ev_field(reader, "event", &f5);
    ev_set(o3, "value", ev_f_finite(&f5, "value"));
    qa_save_reader f6;
    ev_field(reader, "event", &f6);
    ev_set(o3, "color", ev_f_integer(&f6, "color", -9007199254740991.0));
    ev_set(o2, "event", o3);
    ev_set(o2, "content", ev_read(reader, "content", read_content_id, identity));
    ev_set(o2, "sequence", ev_f_integer(reader, "sequence", 0.0));
    ev_set(o2, "seconds", ev_f_finite(reader, "seconds"));
    if (!ev_f_undefined(reader, "recipient")) {
      ev_set(o2, "recipient", ev_read(reader, "recipient", read_actor_id, identity));
    }
    qa_save_value *result = o2;
    qa_save_reader_destroy(&f6);
    qa_save_reader_destroy(&f5);
    qa_save_reader_destroy(&f4);
    return result;
  }
  if (ev_is(kind1, "presentation-owner")) {
    qa_save_value *o7 = ev_object();
    ev_set(o7, "kind", ev_string("presentation-owner"));
    qa_save_value *o8 = ev_object();
    qa_save_reader f9;
    ev_field(reader, "event", &f9);
    ev_set(o8, "kind", ev_f_choice(&f9, "kind", (const ev_lit[]){{"retired", 0}, {"refreshed", 0}}, 2));
    qa_save_reader f10;
    ev_field(reader, "event", &f10);
    ev_set(o8, "owner", ev_read(&f10, "owner", read_presentation_owner, identity));
    ev_set(o7, "event", o8);
    ev_set(o7, "content", ev_read(reader, "content", read_content_id, identity));
    ev_set(o7, "sequence", ev_f_integer(reader, "sequence", 0.0));
    ev_set(o7, "seconds", ev_f_finite(reader, "seconds"));
    qa_save_value *result = o7;
    qa_save_reader_destroy(&f10);
    qa_save_reader_destroy(&f9);
    return result;
  }
  if (ev_is(kind1, "q1")) {
    qa_save_value *o11 = ev_object();
    ev_set(o11, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"q1", 0}}, 1));
    ev_set(o11, "event", ev_read(reader, "event", read_event_q1_event, identity));
    ev_set(o11, "sequence", ev_f_finite(reader, "sequence"));
    ev_set(o11, "content", ev_read(reader, "content", read_content_id, identity));
    ev_set(o11, "seconds", ev_f_finite(reader, "seconds"));
    if (!ev_f_undefined(reader, "sourceEntity")) {
      ev_set(o11, "sourceEntity", ev_read(reader, "sourceEntity", read_event_simulation_presentation_event_q1_source_entity, identity));
    }
    if (!ev_f_undefined(reader, "recipient")) {
      ev_set(o11, "recipient", ev_read(reader, "recipient", read_actor_id, identity));
    }
    qa_save_value *result = o11;
    return result;
  }
  if (ev_is(kind1, "q1-fog")) {
    qa_save_value *o12 = ev_object();
    ev_set(o12, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"q1-fog", 0}}, 1));
    qa_save_value *o13 = ev_object();
    qa_save_reader f14;
    ev_field(reader, "event", &f14);
    ev_set(o13, "kind", ev_f_literal(&f14, "kind", (const ev_lit[]){{"transition", 0}}, 1));
    qa_save_reader f15;
    ev_field(reader, "event", &f15);
    ev_set(o13, "player", ev_read(&f15, "player", read_event_q1_event_effect_actor, identity));
    qa_save_reader f16;
    ev_field(reader, "event", &f16);
    ev_set(o13, "transition", ev_read(&f16, "transition", read_event_q1_fog_transition, identity));
    qa_save_reader f17;
    ev_field(reader, "event", &f17);
    ev_set(o13, "skyFactor", ev_f_finite(&f17, "skyFactor"));
    ev_set(o12, "event", o13);
    ev_set(o12, "sequence", ev_f_finite(reader, "sequence"));
    ev_set(o12, "content", ev_read(reader, "content", read_content_id, identity));
    ev_set(o12, "seconds", ev_f_finite(reader, "seconds"));
    if (!ev_f_undefined(reader, "sourceEntity")) {
      ev_set(o12, "sourceEntity", ev_read(reader, "sourceEntity", read_event_simulation_presentation_event_q1_source_entity, identity));
    }
    if (!ev_f_undefined(reader, "recipient")) {
      ev_set(o12, "recipient", ev_read(reader, "recipient", read_actor_id, identity));
    }
    qa_save_value *result = o12;
    qa_save_reader_destroy(&f17);
    qa_save_reader_destroy(&f16);
    qa_save_reader_destroy(&f15);
    qa_save_reader_destroy(&f14);
    return result;
  }
  if (ev_is(kind1, "q1-sky")) {
    qa_save_value *o18 = ev_object();
    ev_set(o18, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"q1-sky", 0}}, 1));
    qa_save_value *o19 = ev_object();
    qa_save_reader f20;
    ev_field(reader, "event", &f20);
    ev_set(o19, "kind", ev_f_literal(&f20, "kind", (const ev_lit[]){{"skybox", 0}}, 1));
    qa_save_reader f21;
    ev_field(reader, "event", &f21);
    ev_set(o19, "name", ev_f_string(&f21, "name"));
    ev_set(o18, "event", o19);
    ev_set(o18, "sequence", ev_f_finite(reader, "sequence"));
    ev_set(o18, "content", ev_read(reader, "content", read_content_id, identity));
    ev_set(o18, "seconds", ev_f_finite(reader, "seconds"));
    if (!ev_f_undefined(reader, "sourceEntity")) {
      ev_set(o18, "sourceEntity", ev_read(reader, "sourceEntity", read_event_simulation_presentation_event_q1_source_entity, identity));
    }
    if (!ev_f_undefined(reader, "recipient")) {
      ev_set(o18, "recipient", ev_read(reader, "recipient", read_actor_id, identity));
    }
    qa_save_value *result = o18;
    qa_save_reader_destroy(&f21);
    qa_save_reader_destroy(&f20);
    return result;
  }
  if (ev_is(kind1, "q1-client")) {
    qa_save_value *o22 = ev_object();
    ev_set(o22, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"q1-client", 0}}, 1));
    ev_set(o22, "event", ev_read(reader, "event", read_q1_client_metadata, identity));
    ev_set(o22, "sequence", ev_f_finite(reader, "sequence"));
    ev_set(o22, "content", ev_read(reader, "content", read_content_id, identity));
    ev_set(o22, "seconds", ev_f_finite(reader, "seconds"));
    if (!ev_f_undefined(reader, "sourceEntity")) {
      ev_set(o22, "sourceEntity", ev_read(reader, "sourceEntity", read_event_simulation_presentation_event_q1_source_entity, identity));
    }
    if (!ev_f_undefined(reader, "recipient")) {
      ev_set(o22, "recipient", ev_read(reader, "recipient", read_actor_id, identity));
    }
    qa_save_value *result = o22;
    return result;
  }
  if (ev_is(kind1, "q1-session")) {
    qa_save_value *o23 = ev_object();
    ev_set(o23, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"q1-session", 0}}, 1));
    qa_save_value *o24 = ev_object();
    qa_save_reader f25;
    ev_field(reader, "event", &f25);
    ev_set(o24, "kind", ev_f_choice(&f25, "kind", (const ev_lit[]){{"level-completed", 0}, {"back-to-lobby", 0}}, 2));
    ev_set(o23, "event", o24);
    ev_set(o23, "sequence", ev_f_finite(reader, "sequence"));
    ev_set(o23, "content", ev_read(reader, "content", read_content_id, identity));
    ev_set(o23, "seconds", ev_f_finite(reader, "seconds"));
    if (!ev_f_undefined(reader, "sourceEntity")) {
      ev_set(o23, "sourceEntity", ev_read(reader, "sourceEntity", read_event_simulation_presentation_event_q1_source_entity, identity));
    }
    if (!ev_f_undefined(reader, "recipient")) {
      ev_set(o23, "recipient", ev_read(reader, "recipient", read_actor_id, identity));
    }
    qa_save_value *result = o23;
    qa_save_reader_destroy(&f25);
    return result;
  }
  if (ev_is(kind1, "music")) {
    qa_save_value *o26 = ev_object();
    ev_set(o26, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"music", 0}}, 1));
    ev_set(o26, "event", ev_read(reader, "event", read_music_event, identity));
    ev_set(o26, "sequence", ev_f_finite(reader, "sequence"));
    ev_set(o26, "content", ev_read(reader, "content", read_content_id, identity));
    ev_set(o26, "seconds", ev_f_finite(reader, "seconds"));
    if (!ev_f_undefined(reader, "sourceEntity")) {
      ev_set(o26, "sourceEntity", ev_read(reader, "sourceEntity", read_event_simulation_presentation_event_q1_source_entity, identity));
    }
    if (!ev_f_undefined(reader, "recipient")) {
      ev_set(o26, "recipient", ev_read(reader, "recipient", read_actor_id, identity));
    }
    qa_save_value *result = o26;
    return result;
  }
  if (ev_is(kind1, "q1-composition")) {
    qa_save_value *o27 = ev_object();
    ev_set(o27, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"q1-composition", 0}}, 1));
    ev_set(o27, "event", ev_read(reader, "event", read_event_q1_composition_event, identity));
    ev_set(o27, "sequence", ev_f_finite(reader, "sequence"));
    ev_set(o27, "content", ev_read(reader, "content", read_content_id, identity));
    ev_set(o27, "seconds", ev_f_finite(reader, "seconds"));
    if (!ev_f_undefined(reader, "sourceEntity")) {
      ev_set(o27, "sourceEntity", ev_read(reader, "sourceEntity", read_event_simulation_presentation_event_q1_source_entity, identity));
    }
    if (!ev_f_undefined(reader, "recipient")) {
      ev_set(o27, "recipient", ev_read(reader, "recipient", read_actor_id, identity));
    }
    qa_save_value *result = o27;
    return result;
  }
  if (ev_is(kind1, "q1-level")) {
    qa_save_value *o28 = ev_object();
    ev_set(o28, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"q1-level", 0}}, 1));
    ev_set(o28, "event", ev_read(reader, "event", read_event_q1_intermission_result, identity));
    ev_set(o28, "sequence", ev_f_finite(reader, "sequence"));
    ev_set(o28, "content", ev_read(reader, "content", read_content_id, identity));
    ev_set(o28, "seconds", ev_f_finite(reader, "seconds"));
    if (!ev_f_undefined(reader, "sourceEntity")) {
      ev_set(o28, "sourceEntity", ev_read(reader, "sourceEntity", read_event_simulation_presentation_event_q1_source_entity, identity));
    }
    if (!ev_f_undefined(reader, "recipient")) {
      ev_set(o28, "recipient", ev_read(reader, "recipient", read_actor_id, identity));
    }
    qa_save_value *result = o28;
    return result;
  }
  if (ev_is(kind1, "q2")) {
    qa_save_value *o29 = ev_object();
    ev_set(o29, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"q2", 0}}, 1));
    ev_set(o29, "event", ev_read(reader, "event", read_event_q2_presentation_event, identity));
    ev_set(o29, "sequence", ev_f_finite(reader, "sequence"));
    ev_set(o29, "content", ev_read(reader, "content", read_content_id, identity));
    ev_set(o29, "seconds", ev_f_finite(reader, "seconds"));
    if (!ev_f_undefined(reader, "sourceEntity")) {
      ev_set(o29, "sourceEntity", ev_read(reader, "sourceEntity", read_event_simulation_presentation_event_q1_source_entity, identity));
    }
    if (!ev_f_undefined(reader, "recipient")) {
      ev_set(o29, "recipient", ev_read(reader, "recipient", read_actor_id, identity));
    }
    qa_save_value *result = o29;
    return result;
  }
  if (ev_is(kind1, "q2-weapon")) {
    qa_save_value *o30 = ev_object();
    ev_set(o30, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"q2-weapon", 0}}, 1));
    ev_set(o30, "event", ev_read(reader, "event", read_event_q2_weapon_event, identity));
    ev_set(o30, "sequence", ev_f_finite(reader, "sequence"));
    ev_set(o30, "content", ev_read(reader, "content", read_content_id, identity));
    ev_set(o30, "seconds", ev_f_finite(reader, "seconds"));
    if (!ev_f_undefined(reader, "sourceEntity")) {
      ev_set(o30, "sourceEntity", ev_read(reader, "sourceEntity", read_event_simulation_presentation_event_q1_source_entity, identity));
    }
    if (!ev_f_undefined(reader, "recipient")) {
      ev_set(o30, "recipient", ev_read(reader, "recipient", read_actor_id, identity));
    }
    qa_save_value *result = o30;
    return result;
  }
  if (ev_is(kind1, "view-reset")) {
    qa_save_value *o31 = ev_object();
    ev_set(o31, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"view-reset", 0}}, 1));
    ev_set(o31, "reason", ev_f_choice(reader, "reason", (const ev_lit[]){{"spawn", 0}, {"teleport", 0}, {"freeze", 0}, {"source", 0}}, 4));
    ev_set(o31, "actor", ev_read(reader, "actor", read_actor_id, identity));
    ev_set(o31, "angles", ev_read(reader, "angles", read_event_vec3, identity));
    ev_set(o31, "sequence", ev_f_finite(reader, "sequence"));
    ev_set(o31, "content", ev_read(reader, "content", read_content_id, identity));
    ev_set(o31, "seconds", ev_f_finite(reader, "seconds"));
    if (!ev_f_undefined(reader, "sourceEntity")) {
      ev_set(o31, "sourceEntity", ev_read(reader, "sourceEntity", read_event_simulation_presentation_event_q1_source_entity, identity));
    }
    if (!ev_f_undefined(reader, "recipient")) {
      ev_set(o31, "recipient", ev_read(reader, "recipient", read_actor_id, identity));
    }
    qa_save_value *result = o31;
    return result;
  }
  if (ev_is(kind1, "q2-composition")) {
    qa_save_value *o32 = ev_object();
    ev_set(o32, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"q2-composition", 0}}, 1));
    ev_set(o32, "event", ev_read(reader, "event", read_event_q2_composition_event, identity));
    ev_set(o32, "sequence", ev_f_finite(reader, "sequence"));
    ev_set(o32, "content", ev_read(reader, "content", read_content_id, identity));
    ev_set(o32, "seconds", ev_f_finite(reader, "seconds"));
    if (!ev_f_undefined(reader, "sourceEntity")) {
      ev_set(o32, "sourceEntity", ev_read(reader, "sourceEntity", read_event_simulation_presentation_event_q1_source_entity, identity));
    }
    if (!ev_f_undefined(reader, "recipient")) {
      ev_set(o32, "recipient", ev_read(reader, "recipient", read_actor_id, identity));
    }
    qa_save_value *result = o32;
    return result;
  }
  if (ev_is(kind1, "q2-rerelease")) {
    qa_save_value *o33 = ev_object();
    ev_set(o33, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"q2-rerelease", 0}}, 1));
    ev_set(o33, "event", ev_read(reader, "event", read_event_q2_rerelease_event, identity));
    ev_set(o33, "sequence", ev_f_finite(reader, "sequence"));
    ev_set(o33, "content", ev_read(reader, "content", read_content_id, identity));
    ev_set(o33, "seconds", ev_f_finite(reader, "seconds"));
    if (!ev_f_undefined(reader, "sourceEntity")) {
      ev_set(o33, "sourceEntity", ev_read(reader, "sourceEntity", read_event_simulation_presentation_event_q1_source_entity, identity));
    }
    if (!ev_f_undefined(reader, "recipient")) {
      ev_set(o33, "recipient", ev_read(reader, "recipient", read_actor_id, identity));
    }
    qa_save_value *result = o33;
    return result;
  }
  if (ev_is(kind1, "q2-player")) {
    qa_save_value *o34 = ev_object();
    ev_set(o34, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"q2-player", 0}}, 1));
    ev_set(o34, "event", ev_read(reader, "event", read_event_q2_player_event, identity));
    ev_set(o34, "sequence", ev_f_finite(reader, "sequence"));
    ev_set(o34, "content", ev_read(reader, "content", read_content_id, identity));
    ev_set(o34, "seconds", ev_f_finite(reader, "seconds"));
    if (!ev_f_undefined(reader, "sourceEntity")) {
      ev_set(o34, "sourceEntity", ev_read(reader, "sourceEntity", read_event_simulation_presentation_event_q1_source_entity, identity));
    }
    if (!ev_f_undefined(reader, "recipient")) {
      ev_set(o34, "recipient", ev_read(reader, "recipient", read_actor_id, identity));
    }
    qa_save_value *result = o34;
    return result;
  }
  if (ev_is(kind1, "q3-character")) {
    qa_save_value *o35 = ev_object();
    ev_set(o35, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"q3-character", 0}}, 1));
    ev_set(o35, "event", ev_read(reader, "event", read_event_q3_character_presentation_event, identity));
    ev_set(o35, "sequence", ev_f_finite(reader, "sequence"));
    ev_set(o35, "content", ev_read(reader, "content", read_content_id, identity));
    ev_set(o35, "seconds", ev_f_finite(reader, "seconds"));
    if (!ev_f_undefined(reader, "sourceEntity")) {
      ev_set(o35, "sourceEntity", ev_read(reader, "sourceEntity", read_event_simulation_presentation_event_q1_source_entity, identity));
    }
    if (!ev_f_undefined(reader, "recipient")) {
      ev_set(o35, "recipient", ev_read(reader, "recipient", read_actor_id, identity));
    }
    qa_save_value *result = o35;
    return result;
  }
  if (ev_is(kind1, "q3-source")) {
    qa_save_value *o36 = ev_object();
    ev_set(o36, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"q3-source", 0}}, 1));
    ev_set(o36, "event", ev_read(reader, "event", read_event_q3_source_event, identity));
    ev_set(o36, "sequence", ev_f_finite(reader, "sequence"));
    ev_set(o36, "content", ev_read(reader, "content", read_content_id, identity));
    ev_set(o36, "seconds", ev_f_finite(reader, "seconds"));
    if (!ev_f_undefined(reader, "sourceEntity")) {
      ev_set(o36, "sourceEntity", ev_read(reader, "sourceEntity", read_event_simulation_presentation_event_q1_source_entity, identity));
    }
    if (!ev_f_undefined(reader, "recipient")) {
      ev_set(o36, "recipient", ev_read(reader, "recipient", read_actor_id, identity));
    }
    qa_save_value *result = o36;
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_simulation_presentation_event(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  qa_save_value *s2 = write_source_presentation_event(value);
  ev_spread(o1, s2);
  qa_save_value_free(s2);
  if (!ev_undefined_is(ev_get(value, "owner"))) {
    ev_set(o1, "owner", ev_clone(ev_get(value, "owner")));
  }
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_source_presentation_event(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "debug-graph")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o2, "event", ev_clone(ev_get(value, "event")));
    ev_set(o2, "content", ev_clone(ev_get(value, "content")));
    ev_set(o2, "sequence", ev_clone(ev_get(value, "sequence")));
    ev_set(o2, "seconds", ev_clone(ev_get(value, "seconds")));
    if (!ev_undefined_is(ev_get(value, "recipient"))) {
      ev_set(o2, "recipient", write_actor_id(ev_get(value, "recipient")));
    }
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "presentation-owner")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o3, "event", ev_clone(ev_get(value, "event")));
    ev_set(o3, "content", ev_clone(ev_get(value, "content")));
    ev_set(o3, "sequence", ev_clone(ev_get(value, "sequence")));
    ev_set(o3, "seconds", ev_clone(ev_get(value, "seconds")));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "q1")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o4, "event", write_event_q1_event(ev_get(value, "event")));
    ev_set(o4, "sequence", ev_clone(ev_get(value, "sequence")));
    ev_set(o4, "content", write_content_id(ev_get(value, "content")));
    ev_set(o4, "seconds", ev_clone(ev_get(value, "seconds")));
    if (!ev_undefined_is(ev_get(value, "sourceEntity"))) {
      ev_set(o4, "sourceEntity", write_event_simulation_presentation_event_q1_source_entity(ev_get(value, "sourceEntity")));
    }
    if (!ev_undefined_is(ev_get(value, "recipient"))) {
      ev_set(o4, "recipient", write_actor_id(ev_get(value, "recipient")));
    }
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "q1-fog")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_clone(ev_get(value, "kind")));
    qa_save_value *o6 = ev_object();
    ev_set(o6, "kind", ev_clone(ev_get(ev_get(value, "event"), "kind")));
    ev_set(o6, "player", write_event_q1_event_effect_actor(ev_get(ev_get(value, "event"), "player")));
    ev_set(o6, "transition", write_event_q1_fog_transition(ev_get(ev_get(value, "event"), "transition")));
    ev_set(o6, "skyFactor", ev_clone(ev_get(ev_get(value, "event"), "skyFactor")));
    ev_set(o5, "event", o6);
    ev_set(o5, "sequence", ev_clone(ev_get(value, "sequence")));
    ev_set(o5, "content", write_content_id(ev_get(value, "content")));
    ev_set(o5, "seconds", ev_clone(ev_get(value, "seconds")));
    if (!ev_undefined_is(ev_get(value, "sourceEntity"))) {
      ev_set(o5, "sourceEntity", write_event_simulation_presentation_event_q1_source_entity(ev_get(value, "sourceEntity")));
    }
    if (!ev_undefined_is(ev_get(value, "recipient"))) {
      ev_set(o5, "recipient", write_actor_id(ev_get(value, "recipient")));
    }
    qa_save_value *result = o5;
    return result;
  }
  if (ev_is(kind1, "q1-sky")) {
    qa_save_value *o7 = ev_object();
    ev_set(o7, "kind", ev_clone(ev_get(value, "kind")));
    qa_save_value *o8 = ev_object();
    ev_set(o8, "kind", ev_clone(ev_get(ev_get(value, "event"), "kind")));
    ev_set(o8, "name", ev_clone(ev_get(ev_get(value, "event"), "name")));
    ev_set(o7, "event", o8);
    ev_set(o7, "sequence", ev_clone(ev_get(value, "sequence")));
    ev_set(o7, "content", write_content_id(ev_get(value, "content")));
    ev_set(o7, "seconds", ev_clone(ev_get(value, "seconds")));
    if (!ev_undefined_is(ev_get(value, "sourceEntity"))) {
      ev_set(o7, "sourceEntity", write_event_simulation_presentation_event_q1_source_entity(ev_get(value, "sourceEntity")));
    }
    if (!ev_undefined_is(ev_get(value, "recipient"))) {
      ev_set(o7, "recipient", write_actor_id(ev_get(value, "recipient")));
    }
    qa_save_value *result = o7;
    return result;
  }
  if (ev_is(kind1, "q1-client")) {
    qa_save_value *o9 = ev_object();
    ev_set(o9, "kind", ev_clone(ev_get(value, "kind")));
    qa_save_value *o10 = ev_object();
    ev_set(o10, "kind", ev_clone(ev_get(ev_get(value, "event"), "kind")));
    ev_set(o10, "slot", ev_clone(ev_get(ev_get(value, "event"), "slot")));
    ev_set(o10, "value", ev_clone(ev_get(ev_get(value, "event"), "value")));
    ev_set(o9, "event", o10);
    ev_set(o9, "sequence", ev_clone(ev_get(value, "sequence")));
    ev_set(o9, "content", write_content_id(ev_get(value, "content")));
    ev_set(o9, "seconds", ev_clone(ev_get(value, "seconds")));
    if (!ev_undefined_is(ev_get(value, "sourceEntity"))) {
      ev_set(o9, "sourceEntity", write_event_simulation_presentation_event_q1_source_entity(ev_get(value, "sourceEntity")));
    }
    if (!ev_undefined_is(ev_get(value, "recipient"))) {
      ev_set(o9, "recipient", write_actor_id(ev_get(value, "recipient")));
    }
    qa_save_value *result = o9;
    return result;
  }
  if (ev_is(kind1, "q1-session")) {
    qa_save_value *o11 = ev_object();
    ev_set(o11, "kind", ev_clone(ev_get(value, "kind")));
    qa_save_value *o12 = ev_object();
    ev_set(o12, "kind", ev_clone(ev_get(ev_get(value, "event"), "kind")));
    ev_set(o11, "event", o12);
    ev_set(o11, "sequence", ev_clone(ev_get(value, "sequence")));
    ev_set(o11, "content", write_content_id(ev_get(value, "content")));
    ev_set(o11, "seconds", ev_clone(ev_get(value, "seconds")));
    if (!ev_undefined_is(ev_get(value, "sourceEntity"))) {
      ev_set(o11, "sourceEntity", write_event_simulation_presentation_event_q1_source_entity(ev_get(value, "sourceEntity")));
    }
    if (!ev_undefined_is(ev_get(value, "recipient"))) {
      ev_set(o11, "recipient", write_actor_id(ev_get(value, "recipient")));
    }
    qa_save_value *result = o11;
    return result;
  }
  if (ev_is(kind1, "music")) {
    qa_save_value *o13 = ev_object();
    ev_set(o13, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o13, "event", write_music_event(ev_get(value, "event")));
    ev_set(o13, "sequence", ev_clone(ev_get(value, "sequence")));
    ev_set(o13, "content", write_content_id(ev_get(value, "content")));
    ev_set(o13, "seconds", ev_clone(ev_get(value, "seconds")));
    if (!ev_undefined_is(ev_get(value, "sourceEntity"))) {
      ev_set(o13, "sourceEntity", write_event_simulation_presentation_event_q1_source_entity(ev_get(value, "sourceEntity")));
    }
    if (!ev_undefined_is(ev_get(value, "recipient"))) {
      ev_set(o13, "recipient", write_actor_id(ev_get(value, "recipient")));
    }
    qa_save_value *result = o13;
    return result;
  }
  if (ev_is(kind1, "q1-composition")) {
    qa_save_value *o14 = ev_object();
    ev_set(o14, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o14, "event", write_event_q1_composition_event(ev_get(value, "event")));
    ev_set(o14, "sequence", ev_clone(ev_get(value, "sequence")));
    ev_set(o14, "content", write_content_id(ev_get(value, "content")));
    ev_set(o14, "seconds", ev_clone(ev_get(value, "seconds")));
    if (!ev_undefined_is(ev_get(value, "sourceEntity"))) {
      ev_set(o14, "sourceEntity", write_event_simulation_presentation_event_q1_source_entity(ev_get(value, "sourceEntity")));
    }
    if (!ev_undefined_is(ev_get(value, "recipient"))) {
      ev_set(o14, "recipient", write_actor_id(ev_get(value, "recipient")));
    }
    qa_save_value *result = o14;
    return result;
  }
  if (ev_is(kind1, "q1-level")) {
    qa_save_value *o15 = ev_object();
    ev_set(o15, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o15, "event", write_event_q1_intermission_result(ev_get(value, "event")));
    ev_set(o15, "sequence", ev_clone(ev_get(value, "sequence")));
    ev_set(o15, "content", write_content_id(ev_get(value, "content")));
    ev_set(o15, "seconds", ev_clone(ev_get(value, "seconds")));
    if (!ev_undefined_is(ev_get(value, "sourceEntity"))) {
      ev_set(o15, "sourceEntity", write_event_simulation_presentation_event_q1_source_entity(ev_get(value, "sourceEntity")));
    }
    if (!ev_undefined_is(ev_get(value, "recipient"))) {
      ev_set(o15, "recipient", write_actor_id(ev_get(value, "recipient")));
    }
    qa_save_value *result = o15;
    return result;
  }
  if (ev_is(kind1, "q2")) {
    qa_save_value *o16 = ev_object();
    ev_set(o16, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o16, "event", write_event_q2_presentation_event(ev_get(value, "event")));
    ev_set(o16, "sequence", ev_clone(ev_get(value, "sequence")));
    ev_set(o16, "content", write_content_id(ev_get(value, "content")));
    ev_set(o16, "seconds", ev_clone(ev_get(value, "seconds")));
    if (!ev_undefined_is(ev_get(value, "sourceEntity"))) {
      ev_set(o16, "sourceEntity", write_event_simulation_presentation_event_q1_source_entity(ev_get(value, "sourceEntity")));
    }
    if (!ev_undefined_is(ev_get(value, "recipient"))) {
      ev_set(o16, "recipient", write_actor_id(ev_get(value, "recipient")));
    }
    qa_save_value *result = o16;
    return result;
  }
  if (ev_is(kind1, "q2-weapon")) {
    qa_save_value *o17 = ev_object();
    ev_set(o17, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o17, "event", write_event_q2_weapon_event(ev_get(value, "event")));
    ev_set(o17, "sequence", ev_clone(ev_get(value, "sequence")));
    ev_set(o17, "content", write_content_id(ev_get(value, "content")));
    ev_set(o17, "seconds", ev_clone(ev_get(value, "seconds")));
    if (!ev_undefined_is(ev_get(value, "sourceEntity"))) {
      ev_set(o17, "sourceEntity", write_event_simulation_presentation_event_q1_source_entity(ev_get(value, "sourceEntity")));
    }
    if (!ev_undefined_is(ev_get(value, "recipient"))) {
      ev_set(o17, "recipient", write_actor_id(ev_get(value, "recipient")));
    }
    qa_save_value *result = o17;
    return result;
  }
  if (ev_is(kind1, "view-reset")) {
    qa_save_value *o18 = ev_object();
    ev_set(o18, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o18, "reason", ev_clone(ev_get(value, "reason")));
    ev_set(o18, "actor", write_actor_id(ev_get(value, "actor")));
    ev_set(o18, "angles", write_event_vec3(ev_get(value, "angles")));
    ev_set(o18, "sequence", ev_clone(ev_get(value, "sequence")));
    ev_set(o18, "content", write_content_id(ev_get(value, "content")));
    ev_set(o18, "seconds", ev_clone(ev_get(value, "seconds")));
    if (!ev_undefined_is(ev_get(value, "sourceEntity"))) {
      ev_set(o18, "sourceEntity", write_event_simulation_presentation_event_q1_source_entity(ev_get(value, "sourceEntity")));
    }
    if (!ev_undefined_is(ev_get(value, "recipient"))) {
      ev_set(o18, "recipient", write_actor_id(ev_get(value, "recipient")));
    }
    qa_save_value *result = o18;
    return result;
  }
  if (ev_is(kind1, "q2-composition")) {
    qa_save_value *o19 = ev_object();
    ev_set(o19, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o19, "event", write_event_q2_composition_event(ev_get(value, "event")));
    ev_set(o19, "sequence", ev_clone(ev_get(value, "sequence")));
    ev_set(o19, "content", write_content_id(ev_get(value, "content")));
    ev_set(o19, "seconds", ev_clone(ev_get(value, "seconds")));
    if (!ev_undefined_is(ev_get(value, "sourceEntity"))) {
      ev_set(o19, "sourceEntity", write_event_simulation_presentation_event_q1_source_entity(ev_get(value, "sourceEntity")));
    }
    if (!ev_undefined_is(ev_get(value, "recipient"))) {
      ev_set(o19, "recipient", write_actor_id(ev_get(value, "recipient")));
    }
    qa_save_value *result = o19;
    return result;
  }
  if (ev_is(kind1, "q2-rerelease")) {
    qa_save_value *o20 = ev_object();
    ev_set(o20, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o20, "event", write_event_q2_rerelease_event(ev_get(value, "event")));
    ev_set(o20, "sequence", ev_clone(ev_get(value, "sequence")));
    ev_set(o20, "content", write_content_id(ev_get(value, "content")));
    ev_set(o20, "seconds", ev_clone(ev_get(value, "seconds")));
    if (!ev_undefined_is(ev_get(value, "sourceEntity"))) {
      ev_set(o20, "sourceEntity", write_event_simulation_presentation_event_q1_source_entity(ev_get(value, "sourceEntity")));
    }
    if (!ev_undefined_is(ev_get(value, "recipient"))) {
      ev_set(o20, "recipient", write_actor_id(ev_get(value, "recipient")));
    }
    qa_save_value *result = o20;
    return result;
  }
  if (ev_is(kind1, "q2-player")) {
    qa_save_value *o21 = ev_object();
    ev_set(o21, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o21, "event", write_event_q2_player_event(ev_get(value, "event")));
    ev_set(o21, "sequence", ev_clone(ev_get(value, "sequence")));
    ev_set(o21, "content", write_content_id(ev_get(value, "content")));
    ev_set(o21, "seconds", ev_clone(ev_get(value, "seconds")));
    if (!ev_undefined_is(ev_get(value, "sourceEntity"))) {
      ev_set(o21, "sourceEntity", write_event_simulation_presentation_event_q1_source_entity(ev_get(value, "sourceEntity")));
    }
    if (!ev_undefined_is(ev_get(value, "recipient"))) {
      ev_set(o21, "recipient", write_actor_id(ev_get(value, "recipient")));
    }
    qa_save_value *result = o21;
    return result;
  }
  if (ev_is(kind1, "q3-character")) {
    qa_save_value *o22 = ev_object();
    ev_set(o22, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o22, "event", write_event_q3_character_presentation_event(ev_get(value, "event")));
    ev_set(o22, "sequence", ev_clone(ev_get(value, "sequence")));
    ev_set(o22, "content", write_content_id(ev_get(value, "content")));
    ev_set(o22, "seconds", ev_clone(ev_get(value, "seconds")));
    if (!ev_undefined_is(ev_get(value, "sourceEntity"))) {
      ev_set(o22, "sourceEntity", write_event_simulation_presentation_event_q1_source_entity(ev_get(value, "sourceEntity")));
    }
    if (!ev_undefined_is(ev_get(value, "recipient"))) {
      ev_set(o22, "recipient", write_actor_id(ev_get(value, "recipient")));
    }
    qa_save_value *result = o22;
    return result;
  }
  if (ev_is(kind1, "q3-source")) {
    qa_save_value *o23 = ev_object();
    ev_set(o23, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o23, "event", write_event_q3_source_event(ev_get(value, "event")));
    ev_set(o23, "sequence", ev_clone(ev_get(value, "sequence")));
    ev_set(o23, "content", write_content_id(ev_get(value, "content")));
    ev_set(o23, "seconds", ev_clone(ev_get(value, "seconds")));
    if (!ev_undefined_is(ev_get(value, "sourceEntity"))) {
      ev_set(o23, "sourceEntity", write_event_simulation_presentation_event_q1_source_entity(ev_get(value, "sourceEntity")));
    }
    if (!ev_undefined_is(ev_get(value, "recipient"))) {
      ev_set(o23, "recipient", write_actor_id(ev_get(value, "recipient")));
    }
    qa_save_value *result = o23;
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_source_time(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "seconds")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"seconds", 0}}, 1));
    ev_set(o2, "value", ev_f_finite(reader, "value"));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "milliseconds")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"milliseconds", 0}}, 1));
    ev_set(o3, "value", ev_f_finite(reader, "value"));
    qa_save_value *result = o3;
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_source_time(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "seconds")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o2, "value", ev_clone(ev_get(value, "value")));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "milliseconds")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o3, "value", ev_clone(ev_get(value, "value")));
    qa_save_value *result = o3;
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_event_audience(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "world")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"world", 0}}, 1));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "seat")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"seat", 0}}, 1));
    ev_set(o3, "seat", ev_read(reader, "seat", read_seat_id, identity));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "client")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"client", 0}}, 1));
    ev_set(o4, "client", ev_read(reader, "client", read_client_id, identity));
    qa_save_value *result = o4;
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_event_audience(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "world")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "seat")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o3, "seat", write_seat_id(ev_get(value, "seat")));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "client")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o4, "client", write_client_id(ev_get(value, "client")));
    qa_save_value *result = o4;
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_q2_native_cause_classic(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "edition", ev_f_literal(reader, "edition", (const ev_lit[]){{"classic", 0}}, 1));
  ev_set(o1, "game", ev_f_choice(reader, "game", (const ev_lit[]){{"base", 0}, {"xatrix", 0}, {"rogue", 0}, {"ctf", 0}}, 4));
  ev_set(o1, "value", ev_f_finite(reader, "value"));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_q2_native_cause_classic(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "edition", ev_clone(ev_get(value, "edition")));
  ev_set(o1, "game", ev_clone(ev_get(value, "game")));
  ev_set(o1, "value", ev_clone(ev_get(value, "value")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q2_native_cause_rerelease(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "edition", ev_f_literal(reader, "edition", (const ev_lit[]){{"rerelease", 0}}, 1));
  ev_set(o1, "id", ev_f_finite(reader, "id"));
  ev_set(o1, "friendlyFire", ev_f_boolean(reader, "friendlyFire"));
  ev_set(o1, "noPointLoss", ev_f_boolean(reader, "noPointLoss"));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_q2_native_cause_rerelease(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "edition", ev_clone(ev_get(value, "edition")));
  ev_set(o1, "id", ev_clone(ev_get(value, "id")));
  ev_set(o1, "friendlyFire", ev_clone(ev_get(value, "friendlyFire")));
  ev_set(o1, "noPointLoss", ev_clone(ev_get(value, "noPointLoss")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q2_native_cause(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "edition");
  if (ev_is(kind1, "classic")) {
    qa_save_value *result = read_event_q2_native_cause_classic(reader, identity);
    return result;
  }
  if (ev_is(kind1, "rerelease")) {
    qa_save_value *result = read_event_q2_native_cause_rerelease(reader, identity);
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_q2_native_cause(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "edition"));
  if (ev_is(kind1, "classic")) {
    qa_save_value *result = write_event_q2_native_cause_classic(value);
    return result;
  }
  if (ev_is(kind1, "rerelease")) {
    qa_save_value *result = write_event_q2_native_cause_rerelease(value);
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_attack_provenance_cause(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "q1")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"q1", 0}}, 1));
    ev_set(o2, "deathType", ev_f_string(reader, "deathType"));
    if (!ev_f_undefined(reader, "armorEffect")) {
      ev_set(o2, "armorEffect", ev_f_choice(reader, "armorEffect", (const ev_lit[]){{"bypass", 0}, {"half-effectiveness", 0}}, 2));
    }
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "q2")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"q2", 0}}, 1));
    ev_set(o3, "meansOfDeath", ev_f_finite(reader, "meansOfDeath"));
    ev_set(o3, "damageFlags", ev_f_finite(reader, "damageFlags"));
    if (!ev_f_undefined(reader, "native")) {
      ev_set(o3, "native", ev_read(reader, "native", read_event_q2_native_cause, identity));
    }
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "q3")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"q3", 0}}, 1));
    ev_set(o4, "meansOfDeath", ev_f_finite(reader, "meansOfDeath"));
    ev_set(o4, "damageFlags", ev_f_finite(reader, "damageFlags"));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "environment")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"environment", 0}}, 1));
    ev_set(o5, "hazard", ev_f_choice(reader, "hazard", (const ev_lit[]){{"fall", 0}, {"drown", 0}, {"lava", 0}, {"slime", 0}, {"crush", 0}, {"trigger", 0}}, 6));
    qa_save_value *result = o5;
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_attack_provenance_cause(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "q1")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o2, "deathType", ev_clone(ev_get(value, "deathType")));
    if (!ev_undefined_is(ev_get(value, "armorEffect"))) {
      ev_set(o2, "armorEffect", ev_clone(ev_get(value, "armorEffect")));
    }
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "q2")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o3, "meansOfDeath", ev_clone(ev_get(value, "meansOfDeath")));
    ev_set(o3, "damageFlags", ev_clone(ev_get(value, "damageFlags")));
    if (!ev_undefined_is(ev_get(value, "native"))) {
      ev_set(o3, "native", write_event_q2_native_cause(ev_get(value, "native")));
    }
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "q3")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o4, "meansOfDeath", ev_clone(ev_get(value, "meansOfDeath")));
    ev_set(o4, "damageFlags", ev_clone(ev_get(value, "damageFlags")));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "environment")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o5, "hazard", ev_clone(ev_get(value, "hazard")));
    qa_save_value *result = o5;
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_attack_provenance(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "sequence", ev_f_finite(reader, "sequence"));
  ev_set(o1, "time", ev_read(reader, "time", read_event_source_time, identity));
  ev_set(o1, "attacker", ev_read(reader, "attacker", read_event_q1_event_effect_actor, identity));
  ev_set(o1, "inflictor", ev_read(reader, "inflictor", read_event_q1_event_effect_actor, identity));
  if (!ev_f_undefined(reader, "originatingProjectile")) {
    ev_set(o1, "originatingProjectile", ev_read(reader, "originatingProjectile", read_actor_id, identity));
  }
  ev_set(o1, "weapon", ev_read(reader, "weapon", read_event_q2_player_view_selected_item, identity));
  ev_set(o1, "weaponProvider", ev_read(reader, "weaponProvider", read_provider_id, identity));
  if (!ev_f_undefined(reader, "damagePowerupOwner")) {
    ev_set(o1, "damagePowerupOwner", ev_read(reader, "damagePowerupOwner", read_provider_id, identity));
  }
  ev_set(o1, "combatProvider", ev_read(reader, "combatProvider", read_provider_id, identity));
  ev_set(o1, "inventoryProvider", ev_read(reader, "inventoryProvider", read_provider_id, identity));
  ev_set(o1, "movementProvider", ev_read(reader, "movementProvider", read_provider_id, identity));
  ev_set(o1, "cause", ev_read(reader, "cause", read_event_attack_provenance_cause, identity));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_attack_provenance(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "sequence", ev_clone(ev_get(value, "sequence")));
  ev_set(o1, "time", write_event_source_time(ev_get(value, "time")));
  ev_set(o1, "attacker", write_event_q1_event_effect_actor(ev_get(value, "attacker")));
  ev_set(o1, "inflictor", write_event_q1_event_effect_actor(ev_get(value, "inflictor")));
  if (!ev_undefined_is(ev_get(value, "originatingProjectile"))) {
    ev_set(o1, "originatingProjectile", write_actor_id(ev_get(value, "originatingProjectile")));
  }
  ev_set(o1, "weapon", write_event_q2_player_view_selected_item(ev_get(value, "weapon")));
  ev_set(o1, "weaponProvider", write_provider_id(ev_get(value, "weaponProvider")));
  if (!ev_undefined_is(ev_get(value, "damagePowerupOwner"))) {
    ev_set(o1, "damagePowerupOwner", write_provider_id(ev_get(value, "damagePowerupOwner")));
  }
  ev_set(o1, "combatProvider", write_provider_id(ev_get(value, "combatProvider")));
  ev_set(o1, "inventoryProvider", write_provider_id(ev_get(value, "inventoryProvider")));
  ev_set(o1, "movementProvider", write_provider_id(ev_get(value, "movementProvider")));
  ev_set(o1, "cause", write_event_attack_provenance_cause(ev_get(value, "cause")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_damage_request(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "attack", ev_read(reader, "attack", read_event_attack_provenance, identity));
  ev_set(o1, "target", ev_read(reader, "target", read_actor_id, identity));
  ev_set(o1, "amount", ev_f_finite(reader, "amount"));
  ev_set(o1, "knockback", ev_f_finite(reader, "knockback"));
  ev_set(o1, "direction", ev_read(reader, "direction", read_event_vec3, identity));
  ev_set(o1, "point", ev_read(reader, "point", read_event_vec3, identity));
  ev_set(o1, "normal", ev_read(reader, "normal", read_event_vec3, identity));
  ev_set(o1, "delivery", ev_f_choice(reader, "delivery", (const ev_lit[]){{"direct", 0}, {"radius", 0}}, 2));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_damage_request(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "attack", write_event_attack_provenance(ev_get(value, "attack")));
  ev_set(o1, "target", write_actor_id(ev_get(value, "target")));
  ev_set(o1, "amount", ev_clone(ev_get(value, "amount")));
  ev_set(o1, "knockback", ev_clone(ev_get(value, "knockback")));
  ev_set(o1, "direction", write_event_vec3(ev_get(value, "direction")));
  ev_set(o1, "point", write_event_vec3(ev_get(value, "point")));
  ev_set(o1, "normal", write_event_vec3(ev_get(value, "normal")));
  ev_set(o1, "delivery", ev_clone(ev_get(value, "delivery")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_kind_none(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"none", 0}}, 1));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_kind_none(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "kind", ev_clone(ev_get(value, "kind")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_powered_protection_state(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "none")) {
    qa_save_value *result = read_event_kind_none(reader, identity);
    return result;
  }
  if (ev_is(kind1, "screen") || ev_is(kind1, "shield")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_f_choice(reader, "kind", (const ev_lit[]){{"screen", 0}, {"shield", 0}}, 2));
    ev_set(o2, "cells", ev_f_finite(reader, "cells"));
    qa_save_value *result = o2;
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_powered_protection_state(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "none")) {
    qa_save_value *result = write_event_kind_none(value);
    return result;
  }
  if (ev_is(kind1, "screen") || ev_is(kind1, "shield")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o2, "cells", ev_clone(ev_get(value, "cells")));
    qa_save_value *result = o2;
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_regular_armor_state(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "none")) {
    qa_save_value *result = read_event_kind_none(reader, identity);
    return result;
  }
  if (ev_is(kind1, "q1")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"q1", 0}}, 1));
    ev_set(o2, "points", ev_f_finite(reader, "points"));
    ev_set(o2, "absorption", ev_f_finite(reader, "absorption"));
    ev_set(o2, "item", ev_read(reader, "item", read_item_id, identity));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "q2")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"q2", 0}}, 1));
    ev_set(o3, "points", ev_f_finite(reader, "points"));
    ev_set(o3, "normalProtection", ev_f_finite(reader, "normalProtection"));
    ev_set(o3, "energyProtection", ev_f_finite(reader, "energyProtection"));
    ev_set(o3, "item", ev_read(reader, "item", read_item_id, identity));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "q3")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"q3", 0}}, 1));
    ev_set(o4, "points", ev_f_finite(reader, "points"));
    ev_set(o4, "protection", ev_f_finite(reader, "protection"));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "source")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_string("source"));
    ev_set(o5, "points", ev_f_finite(reader, "points"));
    qa_save_reader f6;
    ev_field(reader, "item", &f6);
    ev_set(o5, "item", ev_nullable(&f6, read_item_id, identity));
    qa_save_value *result = o5;
    qa_save_reader_destroy(&f6);
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_regular_armor_state(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "none")) {
    qa_save_value *result = write_event_kind_none(value);
    return result;
  }
  if (ev_is(kind1, "q1")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o2, "points", ev_clone(ev_get(value, "points")));
    ev_set(o2, "absorption", ev_clone(ev_get(value, "absorption")));
    ev_set(o2, "item", write_item_id(ev_get(value, "item")));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "q2")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o3, "points", ev_clone(ev_get(value, "points")));
    ev_set(o3, "normalProtection", ev_clone(ev_get(value, "normalProtection")));
    ev_set(o3, "energyProtection", ev_clone(ev_get(value, "energyProtection")));
    ev_set(o3, "item", write_item_id(ev_get(value, "item")));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "q3")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o4, "points", ev_clone(ev_get(value, "points")));
    ev_set(o4, "protection", ev_clone(ev_get(value, "protection")));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "source")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_string("source"));
    ev_set(o5, "points", ev_clone(ev_get(value, "points")));
    qa_save_value *c6;
    if (ev_null_is(ev_get(value, "item"))) {
      c6 = ev_null();
    } else {
      c6 = write_item_id(ev_get(value, "item"));
    }
    ev_set(o5, "item", c6);
    qa_save_value *result = o5;
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_armor_state(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "regular", ev_read(reader, "regular", read_event_regular_armor_state, identity));
  ev_set(o1, "powered", ev_read(reader, "powered", read_event_powered_protection_state, identity));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_armor_state(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "regular", write_event_regular_armor_state(ev_get(value, "regular")));
  ev_set(o1, "powered", write_event_powered_protection_state(ev_get(value, "powered")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_damage_mutation(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "health")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"health", 0}}, 1));
    ev_set(o2, "before", ev_f_finite(reader, "before"));
    ev_set(o2, "after", ev_f_finite(reader, "after"));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "armor")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"armor", 0}}, 1));
    ev_set(o3, "before", ev_read(reader, "before", read_event_armor_state, identity));
    ev_set(o3, "after", ev_read(reader, "after", read_event_armor_state, identity));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "source-velocity")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"source-velocity", 0}}, 1));
    ev_set(o4, "before", ev_read(reader, "before", read_event_vec3, identity));
    ev_set(o4, "after", ev_read(reader, "after", read_event_vec3, identity));
    ev_set(o4, "movementProvider", ev_read(reader, "movementProvider", read_provider_id, identity));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "impulse")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"impulse", 0}}, 1));
    ev_set(o5, "impulse", ev_read(reader, "impulse", read_event_vec3, identity));
    ev_set(o5, "movementProvider", ev_read(reader, "movementProvider", read_provider_id, identity));
    qa_save_value *result = o5;
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_damage_mutation(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "health")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o2, "before", ev_clone(ev_get(value, "before")));
    ev_set(o2, "after", ev_clone(ev_get(value, "after")));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "armor")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o3, "before", write_event_armor_state(ev_get(value, "before")));
    ev_set(o3, "after", write_event_armor_state(ev_get(value, "after")));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "source-velocity")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o4, "before", write_event_vec3(ev_get(value, "before")));
    ev_set(o4, "after", write_event_vec3(ev_get(value, "after")));
    ev_set(o4, "movementProvider", write_provider_id(ev_get(value, "movementProvider")));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "impulse")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o5, "impulse", write_event_vec3(ev_get(value, "impulse")));
    ev_set(o5, "movementProvider", write_provider_id(ev_get(value, "movementProvider")));
    qa_save_value *result = o5;
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_damage_decision_mutations(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *result = ev_list(reader, read_event_damage_mutation, identity);
  return result;
}

static qa_save_value *write_event_damage_decision_mutations(const qa_save_value *value) {
  qa_save_value *result = ev_map(value, write_event_damage_mutation);
  return result;
}

static qa_save_value *read_event_damage_decision_feedback(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "q2")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"q2", 0}}, 1));
    ev_set(o2, "powerArmor", ev_f_finite(reader, "powerArmor"));
    ev_set(o2, "armor", ev_f_finite(reader, "armor"));
    ev_set(o2, "blood", ev_f_finite(reader, "blood"));
    ev_set(o2, "knockback", ev_f_finite(reader, "knockback"));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "q3")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"q3", 0}}, 1));
    ev_set(o3, "knockback", ev_f_finite(reader, "knockback"));
    ev_set(o3, "battlesuit", ev_f_boolean(reader, "battlesuit"));
    qa_save_value *result = o3;
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_damage_decision_feedback(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "q2")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o2, "powerArmor", ev_clone(ev_get(value, "powerArmor")));
    ev_set(o2, "armor", ev_clone(ev_get(value, "armor")));
    ev_set(o2, "blood", ev_clone(ev_get(value, "blood")));
    ev_set(o2, "knockback", ev_clone(ev_get(value, "knockback")));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "q3")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o3, "knockback", ev_clone(ev_get(value, "knockback")));
    ev_set(o3, "battlesuit", ev_clone(ev_get(value, "battlesuit")));
    qa_save_value *result = o3;
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_damage_decision(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "request", ev_read(reader, "request", read_event_damage_request, identity));
  ev_set(o1, "mutations", ev_read(reader, "mutations", read_event_damage_decision_mutations, identity));
  ev_set(o1, "appliedDamage", ev_f_finite(reader, "appliedDamage"));
  ev_set(o1, "reaction", ev_f_choice(reader, "reaction", (const ev_lit[]){{"none", 0}, {"pain", 0}, {"death", 0}}, 3));
  if (!ev_f_undefined(reader, "feedback")) {
    ev_set(o1, "feedback", ev_read(reader, "feedback", read_event_damage_decision_feedback, identity));
  }
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_damage_decision(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "request", write_event_damage_request(ev_get(value, "request")));
  ev_set(o1, "mutations", write_event_damage_decision_mutations(ev_get(value, "mutations")));
  ev_set(o1, "appliedDamage", ev_clone(ev_get(value, "appliedDamage")));
  ev_set(o1, "reaction", ev_clone(ev_get(value, "reaction")));
  if (!ev_undefined_is(ev_get(value, "feedback"))) {
    ev_set(o1, "feedback", write_event_damage_decision_feedback(ev_get(value, "feedback")));
  }
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_damage_outcome(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "stale-target")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"stale-target", 0}}, 1));
    ev_set(o2, "request", ev_read(reader, "request", read_event_damage_request, identity));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "committed")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"committed", 0}}, 1));
    ev_set(o3, "decision", ev_read(reader, "decision", read_event_damage_decision, identity));
    ev_set(o3, "survived", ev_f_boolean(reader, "survived"));
    qa_save_value *result = o3;
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_damage_outcome(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "stale-target")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o2, "request", write_event_damage_request(ev_get(value, "request")));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "committed")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o3, "decision", write_event_damage_decision(ev_get(value, "decision")));
    ev_set(o3, "survived", ev_clone(ev_get(value, "survived")));
    qa_save_value *result = o3;
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_transition_decision_stay_blocked(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *result = ev_list(reader, read_objective_id, identity);
  return result;
}

static qa_save_value *write_event_transition_decision_stay_blocked(const qa_save_value *value) {
  qa_save_value *result = ev_map(value, write_objective_id);
  return result;
}

static qa_save_value *read_event_transition_decision(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "stay")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"stay", 0}}, 1));
    ev_set(o2, "blocked", ev_read(reader, "blocked", read_event_transition_decision_stay_blocked, identity));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "round")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"round", 0}}, 1));
    ev_set(o3, "winner", ev_read(reader, "winner", read_event_q2_weapon_event_view_weapon_weapon, identity));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "travel")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"travel", 0}}, 1));
    ev_set(o4, "map", ev_read(reader, "map", read_provider_id, identity));
    ev_set(o4, "spawnPoint", ev_f_string(reader, "spawnPoint"));
    ev_set(o4, "completeCampaign", ev_f_boolean(reader, "completeCampaign"));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "campaign-complete")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"campaign-complete", 0}}, 1));
    ev_set(o5, "campaign", ev_read(reader, "campaign", read_provider_id, identity));
    qa_save_value *result = o5;
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_transition_decision(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "stay")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o2, "blocked", write_event_transition_decision_stay_blocked(ev_get(value, "blocked")));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "round")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o3, "winner", write_event_q2_weapon_event_view_weapon_weapon(ev_get(value, "winner")));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "travel")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o4, "map", write_provider_id(ev_get(value, "map")));
    ev_set(o4, "spawnPoint", ev_clone(ev_get(value, "spawnPoint")));
    ev_set(o4, "completeCampaign", ev_clone(ev_get(value, "completeCampaign")));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "campaign-complete")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o5, "campaign", write_provider_id(ev_get(value, "campaign")));
    qa_save_value *result = o5;
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_network_event_sound_origin(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *c1;
  if (ev_r_null(reader)) {
    c1 = ev_null();
  } else {
    c1 = read_event_vec3(reader, identity);
  }
  qa_save_value *result = c1;
  return result;
}

static qa_save_value *write_event_network_event_sound_origin(const qa_save_value *value) {
  qa_save_value *c1;
  if (ev_null_is(value)) {
    c1 = ev_null();
  } else {
    c1 = write_event_vec3(value);
  }
  qa_save_value *result = c1;
  return result;
}

static qa_save_value *read_event_network_event_q2_inventory_counts(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *result = ev_list(reader, read_event_network_event_q2_inventory_counts_item_3, identity);
  return result;
}

static qa_save_value *write_event_network_event_q2_inventory_counts(const qa_save_value *value) {
  qa_save_value *result = ev_map(value, ev_map_identity);
  return result;
}

static qa_save_value *read_event_network_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "print")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"print", 0}}, 1));
    ev_set(o2, "level", ev_f_finite(reader, "level"));
    ev_set(o2, "text", ev_f_string(reader, "text"));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "center-print")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"center-print", 0}}, 1));
    ev_set(o3, "text", ev_f_string(reader, "text"));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "command-text")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"command-text", 0}}, 1));
    ev_set(o4, "text", ev_f_string(reader, "text"));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "config-string")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"config-string", 0}}, 1));
    ev_set(o5, "index", ev_f_finite(reader, "index"));
    ev_set(o5, "value", ev_f_string(reader, "value"));
    qa_save_value *result = o5;
    return result;
  }
  if (ev_is(kind1, "sound")) {
    qa_save_value *o6 = ev_object();
    ev_set(o6, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"sound", 0}}, 1));
    ev_set(o6, "entityNumber", ev_f_finite(reader, "entityNumber"));
    ev_set(o6, "channel", ev_f_finite(reader, "channel"));
    ev_set(o6, "soundIndex", ev_f_finite(reader, "soundIndex"));
    ev_set(o6, "origin", ev_read(reader, "origin", read_event_network_event_sound_origin, identity));
    ev_set(o6, "volume", ev_f_finite(reader, "volume"));
    ev_set(o6, "attenuation", ev_f_finite(reader, "attenuation"));
    ev_set(o6, "delaySeconds", ev_f_finite(reader, "delaySeconds"));
    qa_save_value *result = o6;
    return result;
  }
  if (ev_is(kind1, "q1-damage")) {
    qa_save_value *o7 = ev_object();
    ev_set(o7, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"q1-damage", 0}}, 1));
    ev_set(o7, "armor", ev_f_finite(reader, "armor"));
    ev_set(o7, "blood", ev_f_finite(reader, "blood"));
    ev_set(o7, "source", ev_read(reader, "source", read_event_vec3, identity));
    qa_save_value *result = o7;
    return result;
  }
  if (ev_is(kind1, "q1-particle")) {
    qa_save_value *o8 = ev_object();
    ev_set(o8, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"q1-particle", 0}}, 1));
    ev_set(o8, "origin", ev_read(reader, "origin", read_event_vec3, identity));
    ev_set(o8, "direction", ev_read(reader, "direction", read_event_vec3, identity));
    ev_set(o8, "count", ev_f_finite(reader, "count"));
    ev_set(o8, "color", ev_f_finite(reader, "color"));
    qa_save_value *result = o8;
    return result;
  }
  if (ev_is(kind1, "q2-layout")) {
    qa_save_value *o9 = ev_object();
    ev_set(o9, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"q2-layout", 0}}, 1));
    ev_set(o9, "program", ev_f_string(reader, "program"));
    qa_save_value *result = o9;
    return result;
  }
  if (ev_is(kind1, "q2-inventory")) {
    qa_save_value *o10 = ev_object();
    ev_set(o10, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"q2-inventory", 0}}, 1));
    ev_set(o10, "counts", ev_read(reader, "counts", read_event_network_event_q2_inventory_counts, identity));
    qa_save_value *result = o10;
    return result;
  }
  if (ev_is(kind1, "q2-muzzle-flash")) {
    qa_save_value *o11 = ev_object();
    ev_set(o11, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"q2-muzzle-flash", 0}}, 1));
    ev_set(o11, "entityNumber", ev_f_finite(reader, "entityNumber"));
    ev_set(o11, "flash", ev_f_finite(reader, "flash"));
    ev_set(o11, "monster", ev_f_boolean(reader, "monster"));
    qa_save_value *result = o11;
    return result;
  }
  if (ev_is(kind1, "q3-server-command")) {
    qa_save_value *o12 = ev_object();
    ev_set(o12, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"q3-server-command", 0}}, 1));
    ev_set(o12, "sequence", ev_f_finite(reader, "sequence"));
    ev_set(o12, "text", ev_f_string(reader, "text"));
    qa_save_value *result = o12;
    return result;
  }
  if (ev_is(kind1, "disconnect")) {
    qa_save_value *o13 = ev_object();
    ev_set(o13, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"disconnect", 0}}, 1));
    ev_set(o13, "reason", ev_f_string(reader, "reason"));
    qa_save_value *result = o13;
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_network_event(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "print")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o2, "level", ev_clone(ev_get(value, "level")));
    ev_set(o2, "text", ev_clone(ev_get(value, "text")));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "center-print")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o3, "text", ev_clone(ev_get(value, "text")));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "command-text")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o4, "text", ev_clone(ev_get(value, "text")));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "config-string")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o5, "index", ev_clone(ev_get(value, "index")));
    ev_set(o5, "value", ev_clone(ev_get(value, "value")));
    qa_save_value *result = o5;
    return result;
  }
  if (ev_is(kind1, "sound")) {
    qa_save_value *o6 = ev_object();
    ev_set(o6, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o6, "entityNumber", ev_clone(ev_get(value, "entityNumber")));
    ev_set(o6, "channel", ev_clone(ev_get(value, "channel")));
    ev_set(o6, "soundIndex", ev_clone(ev_get(value, "soundIndex")));
    ev_set(o6, "origin", write_event_network_event_sound_origin(ev_get(value, "origin")));
    ev_set(o6, "volume", ev_clone(ev_get(value, "volume")));
    ev_set(o6, "attenuation", ev_clone(ev_get(value, "attenuation")));
    ev_set(o6, "delaySeconds", ev_clone(ev_get(value, "delaySeconds")));
    qa_save_value *result = o6;
    return result;
  }
  if (ev_is(kind1, "q1-damage")) {
    qa_save_value *o7 = ev_object();
    ev_set(o7, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o7, "armor", ev_clone(ev_get(value, "armor")));
    ev_set(o7, "blood", ev_clone(ev_get(value, "blood")));
    ev_set(o7, "source", write_event_vec3(ev_get(value, "source")));
    qa_save_value *result = o7;
    return result;
  }
  if (ev_is(kind1, "q1-particle")) {
    qa_save_value *o8 = ev_object();
    ev_set(o8, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o8, "origin", write_event_vec3(ev_get(value, "origin")));
    ev_set(o8, "direction", write_event_vec3(ev_get(value, "direction")));
    ev_set(o8, "count", ev_clone(ev_get(value, "count")));
    ev_set(o8, "color", ev_clone(ev_get(value, "color")));
    qa_save_value *result = o8;
    return result;
  }
  if (ev_is(kind1, "q2-layout")) {
    qa_save_value *o9 = ev_object();
    ev_set(o9, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o9, "program", ev_clone(ev_get(value, "program")));
    qa_save_value *result = o9;
    return result;
  }
  if (ev_is(kind1, "q2-inventory")) {
    qa_save_value *o10 = ev_object();
    ev_set(o10, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o10, "counts", write_event_network_event_q2_inventory_counts(ev_get(value, "counts")));
    qa_save_value *result = o10;
    return result;
  }
  if (ev_is(kind1, "q2-muzzle-flash")) {
    qa_save_value *o11 = ev_object();
    ev_set(o11, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o11, "entityNumber", ev_clone(ev_get(value, "entityNumber")));
    ev_set(o11, "flash", ev_clone(ev_get(value, "flash")));
    ev_set(o11, "monster", ev_clone(ev_get(value, "monster")));
    qa_save_value *result = o11;
    return result;
  }
  if (ev_is(kind1, "q3-server-command")) {
    qa_save_value *o12 = ev_object();
    ev_set(o12, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o12, "sequence", ev_clone(ev_get(value, "sequence")));
    ev_set(o12, "text", ev_clone(ev_get(value, "text")));
    qa_save_value *result = o12;
    return result;
  }
  if (ev_is(kind1, "disconnect")) {
    qa_save_value *o13 = ev_object();
    ev_set(o13, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o13, "reason", ev_clone(ev_get(value, "reason")));
    qa_save_value *result = o13;
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_simulation_event_payload(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  const char *kind1 = ev_f_text(reader, "kind");
  if (ev_is(kind1, "sound")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"sound", 0}}, 1));
    ev_set(o2, "resource", ev_read(reader, "resource", read_resource_id, identity));
    ev_set(o2, "actor", ev_read(reader, "actor", read_event_q1_event_effect_actor, identity));
    ev_set(o2, "origin", ev_read(reader, "origin", read_event_vec3, identity));
    ev_set(o2, "channel", ev_f_finite(reader, "channel"));
    ev_set(o2, "volume", ev_f_finite(reader, "volume"));
    ev_set(o2, "attenuation", ev_f_finite(reader, "attenuation"));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "damage")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"damage", 0}}, 1));
    ev_set(o3, "outcome", ev_read(reader, "outcome", read_event_damage_outcome, identity));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "transition")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"transition", 0}}, 1));
    ev_set(o4, "decision", ev_read(reader, "decision", read_event_transition_decision, identity));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "message")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_f_literal(reader, "kind", (const ev_lit[]){{"message", 0}}, 1));
    ev_set(o5, "event", ev_read(reader, "event", read_event_network_event, identity));
    if (!ev_f_undefined(reader, "sourcePresentationSequence")) {
      ev_set(o5, "sourcePresentationSequence", ev_f_finite(reader, "sourcePresentationSequence"));
    }
    qa_save_value *result = o5;
    return result;
  }
  qa_save_reader_fail(reader, "%s", "unknown event variant");
}

static qa_save_value *write_event_simulation_event_payload(const qa_save_value *value) {
  const char *kind1 = ev_text(ev_get(value, "kind"));
  if (ev_is(kind1, "sound")) {
    qa_save_value *o2 = ev_object();
    ev_set(o2, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o2, "resource", write_resource_id(ev_get(value, "resource")));
    ev_set(o2, "actor", write_event_q1_event_effect_actor(ev_get(value, "actor")));
    ev_set(o2, "origin", write_event_vec3(ev_get(value, "origin")));
    ev_set(o2, "channel", ev_clone(ev_get(value, "channel")));
    ev_set(o2, "volume", ev_clone(ev_get(value, "volume")));
    ev_set(o2, "attenuation", ev_clone(ev_get(value, "attenuation")));
    qa_save_value *result = o2;
    return result;
  }
  if (ev_is(kind1, "damage")) {
    qa_save_value *o3 = ev_object();
    ev_set(o3, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o3, "outcome", write_event_damage_outcome(ev_get(value, "outcome")));
    qa_save_value *result = o3;
    return result;
  }
  if (ev_is(kind1, "transition")) {
    qa_save_value *o4 = ev_object();
    ev_set(o4, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o4, "decision", write_event_transition_decision(ev_get(value, "decision")));
    qa_save_value *result = o4;
    return result;
  }
  if (ev_is(kind1, "message")) {
    qa_save_value *o5 = ev_object();
    ev_set(o5, "kind", ev_clone(ev_get(value, "kind")));
    ev_set(o5, "event", write_event_network_event(ev_get(value, "event")));
    if (!ev_undefined_is(ev_get(value, "sourcePresentationSequence"))) {
      ev_set(o5, "sourcePresentationSequence", ev_clone(ev_get(value, "sourcePresentationSequence")));
    }
    qa_save_value *result = o5;
    return result;
  }
  return ev_undefined();  /* JS: a switch with no match returns undefined. */
}

static qa_save_value *read_event_simulation_event(const qa_save_reader *reader, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *o1 = ev_object();
  ev_set(o1, "sequence", ev_f_finite(reader, "sequence"));
  ev_set(o1, "time", ev_read(reader, "time", read_event_source_time, identity));
  ev_set(o1, "audience", ev_read(reader, "audience", read_event_event_audience, identity));
  ev_set(o1, "payload", ev_read(reader, "payload", read_event_simulation_event_payload, identity));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *write_event_simulation_event(const qa_save_value *value) {
  qa_save_value *o1 = ev_object();
  ev_set(o1, "sequence", ev_clone(ev_get(value, "sequence")));
  ev_set(o1, "time", write_event_source_time(ev_get(value, "time")));
  ev_set(o1, "audience", write_event_event_audience(ev_get(value, "audience")));
  ev_set(o1, "payload", write_event_simulation_event_payload(ev_get(value, "payload")));
  qa_save_value *result = o1;
  return result;
}

static qa_save_value *read_event_q1_event_message_args_item_1(const qa_save_reader *item, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *c1;
  if (ev_r_is_string(item)) {
    c1 = ev_r_string(item);
  } else {
    c1 = ev_r_finite(item);
  }
  qa_save_value *result = c1;
  return result;
}

static qa_save_value *read_event_q2_presentation_event_model_attached_models_item_2(const qa_save_reader *item, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *result = ev_r_string(item);
  return result;
}

static qa_save_value *read_event_network_event_q2_inventory_counts_item_3(const qa_save_reader *item, const qa_app_unified_identity_decoder *identity) {
  (void)identity;
  qa_save_value *result = ev_r_finite(item);
  return result;
}

qa_save_value *qa_app_unified_write_simulation_event(const qa_save_value *event) {
  return write_event_simulation_event(event);
}
qa_save_value *qa_app_unified_read_simulation_event(const qa_save_value *value,
    const qa_app_unified_identity_decoder *identity) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "unified.simulation-event")) qa_range_error("out of memory");
  ev_scope scope;
  ev_scope_open(&scope);
  qa_save_value *out = read_event_simulation_event(&reader, identity);
  qa_save_reader_destroy(&reader);
  return ev_scope_close(&scope, out);
}
qa_save_value *qa_app_unified_read_presentation_event(const qa_save_value *value,
    const qa_app_unified_identity_decoder *identity) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "unified.presentation-events")) qa_range_error("out of memory");
  ev_scope scope;
  ev_scope_open(&scope);
  qa_save_value *out = read_event_simulation_presentation_event(&reader, identity);
  qa_save_reader_destroy(&reader);
  return ev_scope_close(&scope, out);
}
qa_save_value *qa_app_unified_write_presentation_events(
    qa_save_value *const *events, size_t count) {
  qa_save_value *list = ev_array();
  for (size_t i = 0; i < count; i++) ev_push(list, write_event_simulation_presentation_event(events[i]));
  return list;
}

/* JSON.stringify ----------------------------------------------------------------- */
typedef struct buffer { char *data; size_t length, capacity; } buffer;
static void buffer_add(buffer *b, const char *text, size_t n) {
  if (b->length + n + 1 > b->capacity) {
    b->capacity = (b->length + n + 1) * 2;
    b->data = realloc(b->data, b->capacity);
    if (!b->data) qa_range_error("out of memory");
  }
  memcpy(b->data + b->length, text, n);
  b->length += n;
  b->data[b->length] = '\0';
}
static void buffer_adds(buffer *b, const char *text) { buffer_add(b, text, strlen(text)); }
static void json_escape(buffer *b, const char *text) {
  buffer_adds(b, "\"");
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
    char escaped[8];
    switch (*p) {
      case '"': buffer_adds(b, "\\\""); break;
      case '\\': buffer_adds(b, "\\\\"); break;
      case '\b': buffer_adds(b, "\\b"); break;
      case '\f': buffer_adds(b, "\\f"); break;
      case '\n': buffer_adds(b, "\\n"); break;
      case '\r': buffer_adds(b, "\\r"); break;
      case '\t': buffer_adds(b, "\\t"); break;
      default:
        if (*p < 0x20) { snprintf(escaped, sizeof escaped, "\\u%04x", *p); buffer_adds(b, escaped); }
        else buffer_add(b, (const char *)p, 1);
        break;
    }
  }
  buffer_adds(b, "\"");
}
/* Number::toString: shortest digits that round-trip, then the ECMAScript layout. */
static void json_number(buffer *b, double value) {
  if (value == 0) { buffer_adds(b, "0"); return; }
  char scientific[40];
  int precision = 0;
  for (; precision < 17; precision++) {
    snprintf(scientific, sizeof scientific, "%.*e", precision, value);
    if (strtod(scientific, NULL) == value) break;
  }
  const char *cursor = scientific;
  if (*cursor == '-') { buffer_adds(b, "-"); cursor++; }
  char digits[24];
  int k = 0;
  for (; *cursor && *cursor != 'e'; cursor++)
    if (*cursor != '.') digits[k++] = *cursor;
  int n = atoi(cursor + 1) + 1; /* value = 0.digits x 10^n */
  while (k > 1 && digits[k - 1] == '0') k--;
  digits[k] = '\0';
  char text[64];
  if (k <= n && n <= 21) {
    buffer_add(b, digits, (size_t)k);
    for (int i = k; i < n; i++) buffer_adds(b, "0");
  } else if (0 < n && n <= 21) {
    buffer_add(b, digits, (size_t)n);
    buffer_adds(b, ".");
    buffer_adds(b, digits + n);
  } else if (-6 < n && n <= 0) {
    buffer_adds(b, "0.");
    for (int i = n; i < 0; i++) buffer_adds(b, "0");
    buffer_adds(b, digits);
  } else {
    buffer_add(b, digits, 1);
    if (k > 1) { buffer_adds(b, "."); buffer_adds(b, digits + 1); }
    snprintf(text, sizeof text, "e%c%d", n - 1 < 0 ? '-' : '+', abs(n - 1));
    buffer_adds(b, text);
  }
}
static void json_write(buffer *b, const qa_save_value *value) {
  switch (value->kind) {
    case QA_SAVE_UNDEFINED: break; /* omitted by the object writer below */
    case QA_SAVE_NULL: buffer_adds(b, "null"); break;
    case QA_SAVE_BOOLEAN: buffer_adds(b, value->boolean ? "true" : "false"); break;
    case QA_SAVE_NUMBER:
      if (!isfinite(value->number)) buffer_adds(b, "null");
      else json_number(b, value->number);
      break;
    case QA_SAVE_BIGINT:
      qa_range_error("TypeError: JSON.stringify cannot serialize BigInt.");
      break;
    case QA_SAVE_STRING: json_escape(b, value->text); break;
    case QA_SAVE_BYTES: {
      /* JSON.stringify(Uint8Array) yields an index object. */
      buffer_adds(b, "{");
      for (size_t i = 0; i < value->byte_length; i++) {
        char text[32];
        if (i) buffer_adds(b, ",");
        snprintf(text, sizeof text, "\"%zu\":%u", i, value->bytes[i]);
        buffer_adds(b, text);
      }
      buffer_adds(b, "}");
      break;
    }
    case QA_SAVE_ARRAY:
      buffer_adds(b, "[");
      for (size_t i = 0; i < value->item_count; i++) {
        if (i) buffer_adds(b, ",");
        if (value->items[i]->kind == QA_SAVE_UNDEFINED) buffer_adds(b, "null");
        else json_write(b, value->items[i]);
      }
      buffer_adds(b, "]");
      break;
    case QA_SAVE_OBJECT: {
      buffer_adds(b, "{");
      bool first = true;
      for (size_t i = 0; i < value->entry_count; i++) {
        if (value->entries[i].value->kind == QA_SAVE_UNDEFINED) continue;
        if (!first) buffer_adds(b, ",");
        first = false;
        json_escape(b, value->entries[i].key);
        buffer_adds(b, ":");
        json_write(b, value->entries[i].value);
      }
      buffer_adds(b, "}");
      break;
    }
  }
}
/* The decoder accepts only finite numbers, so an event whose written form holds NaN or an
 * infinity is dropped here and reported through `dropped` (NULL = TS undefined). */
uint8_t *qa_app_unified_encode_presentation_events_reporting(
    qa_save_value *const *events, size_t count,
    void (*dropped)(void *ctx, size_t count), void *dropped_ctx, size_t *out_length) {
  if (count > 65536) qa_range_error("Too many unified presentation events");
  qa_save_value *list = qa_app_unified_write_presentation_events(events, count);
  size_t kept = 0;
  for (size_t i = 0; i < list->item_count; i++) {
    if (qa_app_unified_has_non_finite(list->items[i], 0)) qa_save_value_free(list->items[i]);
    else list->items[kept++] = list->items[i];
  }
  list->item_count = kept;
  if (kept != count && dropped) dropped(dropped_ctx, count - kept);
  buffer b = {0};
  json_write(&b, list);
  qa_save_value_free(list);
  if (b.length > MAX_EVENT_BYTES) { free(b.data); qa_range_error("Unified presentation events exceed byte limit"); }
  *out_length = b.length;
  return (uint8_t *)b.data;
}
uint8_t *qa_app_unified_encode_presentation_events(
    qa_save_value *const *events, size_t count, size_t *out_length) {
  return qa_app_unified_encode_presentation_events_reporting(events, count, NULL, NULL, out_length);
}

/* new TextDecoder('utf-8', {fatal: true}): WHATWG UTF-8 validation. */
static bool utf8_valid(const uint8_t *s, size_t length) {
  for (size_t i = 0; i < length;) {
    uint8_t c = s[i];
    size_t extra;
    uint8_t lower = 0x80, upper = 0xBF;
    if (c < 0x80) { i++; continue; }
    if (c >= 0xC2 && c <= 0xDF) extra = 1;
    else if (c >= 0xE0 && c <= 0xEF) {
      extra = 2;
      if (c == 0xE0) lower = 0xA0;
      if (c == 0xED) upper = 0x9F;
    } else if (c >= 0xF0 && c <= 0xF4) {
      extra = 3;
      if (c == 0xF0) lower = 0x90;
      if (c == 0xF4) upper = 0x8F;
    } else return false;
    if (length - i <= extra) return false;
    for (size_t k = 1; k <= extra; k++) {
      uint8_t next = s[i + k];
      if (next < lower || next > upper) return false;
      lower = 0x80;
      upper = 0xBF;
    }
    i += extra + 1;
  }
  return true;
}
/* JSON.parse: the last duplicate key wins and keeps the first position. */
static void json_dedupe_keys(qa_save_value *value) {
  if (value->kind == QA_SAVE_ARRAY) {
    for (size_t i = 0; i < value->item_count; i++) json_dedupe_keys(value->items[i]);
    return;
  }
  if (value->kind != QA_SAVE_OBJECT) return;
  size_t kept = 0;
  for (size_t i = 0; i < value->entry_count; i++) {
    qa_save_entry entry = value->entries[i];
    bool replaced = false;
    for (size_t k = 0; k < kept; k++) {
      if (strcmp(value->entries[k].key, entry.key) == 0) {
        qa_save_value_free(value->entries[k].value);
        value->entries[k].value = entry.value;
        free(entry.key);
        replaced = true;
        break;
      }
    }
    if (!replaced) value->entries[kept++] = entry;
  }
  value->entry_count = kept;
  for (size_t i = 0; i < kept; i++) json_dedupe_keys(value->entries[i].value);
}
typedef struct decoded_events { qa_save_value *parsed; qa_save_value **events; size_t count; } decoded_events;
static void decoded_events_free(void *arg) {
  decoded_events *owned = arg;
  for (size_t i = 0; i < owned->count; i++) qa_save_value_free(owned->events[i]);
  free(owned->events);
  qa_save_value_free(owned->parsed);
}
/* A raise mid-list frees the parsed tree and every event already read; the event being read
 * when the raise came is the reader's to release. */
qa_save_value **qa_app_unified_decode_presentation_events(
    const uint8_t *bytes, size_t length,
    const qa_app_unified_identity_decoder *identity, size_t *out_count) {
  if (length > MAX_EVENT_BYTES) qa_range_error("Unified presentation events exceed byte limit");
  if (!utf8_valid(bytes, length)) qa_raise("TypeError", 0, 0, "The encoded data was not valid for encoding utf-8");
  /* TextDecoder drops a leading byte order mark. */
  if (length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) { bytes += 3; length -= 3; }
  decoded_events owned = {NULL, NULL, 0};
  qa_cleanup guard;
  qa_cleanup_push(&guard, decoded_events_free, &owned);
  owned.parsed = qa_parse_json_value(bytes, length, false);
  if (!owned.parsed) qa_range_error("out of memory");
  json_dedupe_keys(owned.parsed);
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, owned.parsed, "unified.presentation-events")) qa_range_error("out of memory");
  size_t count = ev_bounded_count(&reader);
  owned.events = malloc((count ? count : 1) * sizeof *owned.events);
  if (!owned.events) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item;
    ev_item(&reader, i, &item);
    ev_scope scope;
    ev_scope_open(&scope);
    owned.events[i] = ev_scope_close(&scope, read_event_simulation_presentation_event(&item, identity));
    owned.count = i + 1;
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&reader);
  qa_cleanup_pop(&guard);
  qa_save_value_free(owned.parsed);
  *out_count = count;
  return owned.events;
}
