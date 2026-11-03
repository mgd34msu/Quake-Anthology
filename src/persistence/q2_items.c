/* Port of src/persistence/q2-items.ts. GPL-2.0-or-later. */
#include "qa/persistence/q2_items.h"

#include <stdlib.h>
#include <string.h>

static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static qa_save_reader item(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
/* TS readSavedActor (save-image.ts, not yet ported as a module):
 * {slot: integer(0), generation: integer(0)}. Local copy only. */
static qa_saved_actor_id read_actor(const qa_save_reader *reader) {
  qa_saved_actor_id actor;
  qa_save_reader slot = field(reader, "slot");
  actor.slot = qa_save_reader_integer_min(&slot, 0);
  qa_save_reader_destroy(&slot);
  qa_save_reader generation = field(reader, "generation");
  actor.generation = qa_save_reader_integer_min(&generation, 0);
  qa_save_reader_destroy(&generation);
  return actor;
}
static bool set_own(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL) return false;
  if (!qa_save_value_set(object, key, child)) { qa_save_value_free(child); return false; }
  return true;
}
static bool push_own(qa_save_value *array, qa_save_value *child) {
  if (child == NULL) return false;
  if (!qa_save_value_push(array, child)) { qa_save_value_free(child); return false; }
  return true;
}
__attribute__((unused)) static qa_save_value *str_value(const char *text) {
  return qa_save_value_string(text != NULL ? text : "");
}
__attribute__((unused)) static qa_save_value *vec_value(const qa_vec3 *vector) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "x", qa_save_value_number(vector->x)) ||
      !set_own(obj, "y", qa_save_value_number(vector->y)) ||
      !set_own(obj, "z", qa_save_value_number(vector->z))) {
    qa_save_value_free(obj);
    return NULL;
  }
  return obj;
}
static qa_save_value *actor_value(const qa_saved_actor_id *actor) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "slot", qa_save_value_number(actor->slot)) ||
      !set_own(obj, "generation", qa_save_value_number(actor->generation))) {
    qa_save_value_free(obj);
    return NULL;
  }
  return obj;
}
__attribute__((unused)) static qa_save_value *time_value(const qa_source_time *time) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "kind", qa_save_value_string(
          time->kind == QA_SOURCE_TIME_SECONDS ? "seconds" : "milliseconds")) ||
      !set_own(obj, "value", qa_save_value_number(time->value))) {
    qa_save_value_free(obj);
    return NULL;
  }
  return obj;
}
/* Moves every entry of `child` into `object` (TS object spread). */
__attribute__((unused)) static bool merge_into(qa_save_value *object, qa_save_value *child) {
  for (size_t i = 0; i < child->entry_count; i++) {
    qa_save_value *value = child->entries[i].value;
    child->entries[i].value = NULL;
    if (!qa_save_value_set(object, child->entries[i].key, value)) {
      child->entries[i].value = value;
      return false;
    }
  }
  return true;
}

static void read_q2_pickup_entry(const qa_save_reader *reader, qa_q2_item_pickup_checkpoint *out);
static qa_save_value *build_q2_pickup_entry(const qa_q2_item_pickup_checkpoint *in);
static void read_q2_player_powerups(const qa_save_reader *reader, qa_q2_player_powerups *out);
static qa_save_value *build_q2_player_powerups(const qa_q2_player_powerups *in);
static void read_q2_power_entry(const qa_save_reader *reader, qa_q2_item_power_checkpoint *out);
static qa_save_value *build_q2_power_entry(const qa_q2_item_power_checkpoint *in);
static void read_q2_items_checkpoint(const qa_save_reader *reader, qa_q2_items_checkpoint *out, qa_persist_decoded *owner);
static qa_save_value *build_q2_items_checkpoint(const qa_q2_items_checkpoint *in);

static void read_q2_pickup_entry(const qa_save_reader *reader, qa_q2_item_pickup_checkpoint *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "classname");
    (*out).classname = qa_save_reader_string(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "targetsUsed");
    (*out).targets_used = qa_save_reader_boolean(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "retained");
    (*out).retained = qa_save_reader_boolean(&f3);
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "expiresAt");
    if (!qa_save_reader_is_null(&f4)) { (*out).has_expires_at = true;
      (*out).expires_at = qa_save_reader_number(&f4);
    }
    qa_save_reader_destroy(&f4); }
}
static qa_save_value *build_q2_pickup_entry(const qa_q2_item_pickup_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "classname", str_value((*in).classname))) goto fail;
  if (!set_own(obj, "targetsUsed", qa_save_value_boolean((*in).targets_used))) goto fail;
  if (!set_own(obj, "retained", qa_save_value_boolean((*in).retained))) goto fail;
  if (!set_own(obj, "expiresAt", (*in).has_expires_at ? qa_save_value_number((*in).expires_at) : qa_save_value_null())) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_player_powerups(const qa_save_reader *reader, qa_q2_player_powerups *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "quadUntil");
    (*out).quad_until = qa_save_reader_number(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "invulnerabilityUntil");
    (*out).invulnerability_until = qa_save_reader_number(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "breatherUntil");
    (*out).breather_until = qa_save_reader_number(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "enviroUntil");
    (*out).enviro_until = qa_save_reader_number(&f3);
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "hasteUntil");
    (*out).haste_until = qa_save_reader_is_undefined(&f4) ? 0 : qa_save_reader_number(&f4);
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "regenUntil");
    (*out).regen_until = qa_save_reader_is_undefined(&f5) ? 0 : qa_save_reader_number(&f5);
    qa_save_reader_destroy(&f5); }
  { qa_save_reader f6 = field(reader, "flightUntil");
    (*out).flight_until = qa_save_reader_is_undefined(&f6) ? 0 : qa_save_reader_number(&f6);
    qa_save_reader_destroy(&f6); }
  { qa_save_reader f7 = field(reader, "invisUntil");
    (*out).invis_until = qa_save_reader_is_undefined(&f7) ? 0 : qa_save_reader_number(&f7);
    qa_save_reader_destroy(&f7); }
}
static qa_save_value *build_q2_player_powerups(const qa_q2_player_powerups *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "quadUntil", qa_save_value_number((*in).quad_until))) goto fail;
  if (!set_own(obj, "invulnerabilityUntil", qa_save_value_number((*in).invulnerability_until))) goto fail;
  if (!set_own(obj, "breatherUntil", qa_save_value_number((*in).breather_until))) goto fail;
  if (!set_own(obj, "enviroUntil", qa_save_value_number((*in).enviro_until))) goto fail;
  if (!set_own(obj, "hasteUntil", qa_save_value_number((*in).haste_until))) goto fail;
  if (!set_own(obj, "regenUntil", qa_save_value_number((*in).regen_until))) goto fail;
  if (!set_own(obj, "flightUntil", qa_save_value_number((*in).flight_until))) goto fail;
  if (!set_own(obj, "invisUntil", qa_save_value_number((*in).invis_until))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_power_entry(const qa_save_reader *reader, qa_q2_item_power_checkpoint *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "state");
    read_q2_player_powerups(&f1, &(*out).state);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_power_entry(const qa_q2_item_power_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "state", build_q2_player_powerups(&(*in).state))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_items_checkpoint(const qa_save_reader *reader, qa_q2_items_checkpoint *out, qa_persist_decoded *owner) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "powerCubeCount");
    (*out).power_cube_count = qa_save_reader_number(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "pickups");
    (*out).pickup_count = qa_save_reader_list_count(&f1);
    qa_q2_item_pickup_checkpoint *pickups_v = qa_persist_decoded_array(owner, (*out).pickup_count, sizeof *pickups_v);
    (*out).pickups = pickups_v;
    for (size_t i1 = 0; i1 < (*out).pickup_count; i1++) {
      qa_save_reader it2 = item(&f1, i1);
      read_q2_pickup_entry(&it2, &pickups_v[i1]);
      qa_save_reader_destroy(&it2);
    }
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "powers");
    (*out).power_count = qa_save_reader_list_count(&f2);
    qa_q2_item_power_checkpoint *powers_v = qa_persist_decoded_array(owner, (*out).power_count, sizeof *powers_v);
    (*out).powers = powers_v;
    for (size_t i3 = 0; i3 < (*out).power_count; i3++) {
      qa_save_reader it4 = item(&f2, i3);
      read_q2_power_entry(&it4, &powers_v[i3]);
      qa_save_reader_destroy(&it4);
    }
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "powerArmorBindings");
    (*out).binding_count = qa_save_reader_list_count(&f3);
    qa_saved_actor_id *power_armor_bindings_v = qa_persist_decoded_array(owner, (*out).binding_count, sizeof *power_armor_bindings_v);
    (*out).power_armor_bindings = power_armor_bindings_v;
    for (size_t i5 = 0; i5 < (*out).binding_count; i5++) {
      qa_save_reader it6 = item(&f3, i5);
      power_armor_bindings_v[i5] = read_actor(&it6);
      qa_save_reader_destroy(&it6);
    }
    qa_save_reader_destroy(&f3); }
}
static qa_save_value *build_q2_items_checkpoint(const qa_q2_items_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "powerCubeCount", qa_save_value_number((*in).power_cube_count))) goto fail;
  { qa_save_value *arr7 = qa_save_value_array();
    if (arr7 == NULL) goto fail;
    for (size_t i8 = 0; i8 < (*in).pickup_count; i8++) {
      if (!push_own(arr7, build_q2_pickup_entry(&(*in).pickups[i8]))) { qa_save_value_free(arr7); goto fail; }
    }
    if (!set_own(obj, "pickups", arr7)) goto fail; }
  { qa_save_value *arr9 = qa_save_value_array();
    if (arr9 == NULL) goto fail;
    for (size_t i10 = 0; i10 < (*in).power_count; i10++) {
      if (!push_own(arr9, build_q2_power_entry(&(*in).powers[i10]))) { qa_save_value_free(arr9); goto fail; }
    }
    if (!set_own(obj, "powers", arr9)) goto fail; }
  { qa_save_value *arr11 = qa_save_value_array();
    if (arr11 == NULL) goto fail;
    for (size_t i12 = 0; i12 < (*in).binding_count; i12++) {
      if (!push_own(arr11, actor_value(&(*in).power_armor_bindings[i12]))) { qa_save_value_free(arr11); goto fail; }
    }
    if (!set_own(obj, "powerArmorBindings", arr11)) goto fail; }
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

qa_q2_items_checkpoint qa_read_q2_items_checkpoint(const qa_save_reader *reader, qa_persist_decoded *owner) {
  qa_q2_items_checkpoint out;
  read_q2_items_checkpoint(reader, &out, owner);
  return out;
}
uint8_t *qa_encode_q2_items_checkpoint(const qa_q2_items_checkpoint *checkpoint, size_t *out_length) {
  qa_save_value *value = build_q2_items_checkpoint(checkpoint);
  if (value == NULL) return NULL;
  uint8_t *bytes = qa_encode_checkpoint_value(value, out_length);
  qa_save_value_free(value);
  return bytes;
}
bool qa_decode_q2_items_checkpoint(const uint8_t *bytes, size_t length, qa_q2_items_checkpoint *out,
                                   qa_persist_decoded *owner) {
  qa_save_value *root = qa_decode_checkpoint_value(bytes, length);
  if (root == NULL) return false;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, root, "q2-items")) { qa_save_value_free(root); return false; }
  owner->root = root;
  read_q2_items_checkpoint(&reader, out, owner);
  qa_save_reader_destroy(&reader);
  return true;
}
