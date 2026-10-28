/* Port of src/persistence/q2-hand-grenades.ts. GPL-2.0-or-later. */
#include "qa/persistence/q2_hand_grenades.h"

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


static qa_hand_action read_hand_action_value(const qa_save_reader *reader) {
  static const char *const kinds[] = {"idle", "disarmed", "preparing", "cooking", "releasing", "recovering"};
  qa_hand_action action;
  memset(&action, 0, sizeof action);
  qa_save_reader kind_reader = field(reader, "kind");
  const char *kind = qa_save_reader_choice_string(&kind_reader, kinds, 6);
  qa_save_reader_destroy(&kind_reader);
  if (strcmp(kind, "idle") == 0) { action.kind = QA_HAND_ACTION_IDLE; return action; }
  if (strcmp(kind, "disarmed") == 0) { action.kind = QA_HAND_ACTION_DISARMED; return action; }
  if (strcmp(kind, "preparing") == 0) {
    action.kind = QA_HAND_ACTION_PREPARING;
    qa_save_reader frame = field(reader, "frame");
    action.frame = qa_save_reader_integer_min(&frame, 1);
    if (action.frame > 11) qa_save_reader_fail(&frame, "hand grenade preparation exceeds hold frame");
    qa_save_reader_destroy(&frame);
    qa_save_reader next_at = field(reader, "nextAt");
    action.next_at = qa_save_reader_finite(&next_at);
    qa_save_reader_destroy(&next_at);
    qa_save_reader queued = field(reader, "releaseQueued");
    action.release_queued = qa_save_reader_boolean(&queued);
    qa_save_reader_destroy(&queued);
    return action;
  }
  if (strcmp(kind, "cooking") == 0) {
    action.kind = QA_HAND_ACTION_COOKING;
    qa_save_reader expires = field(reader, "expiresAt");
    action.expires_at = qa_save_reader_finite(&expires);
    qa_save_reader_destroy(&expires);
    return action;
  }
  if (strcmp(kind, "releasing") == 0) {
    action.kind = QA_HAND_ACTION_RELEASING;
    qa_save_reader expires = field(reader, "expiresAt");
    action.expires_at = qa_save_reader_finite(&expires);
    qa_save_reader_destroy(&expires);
    qa_save_reader throw_at = field(reader, "throwAt");
    action.throw_at = qa_save_reader_finite(&throw_at);
    qa_save_reader_destroy(&throw_at);
    return action;
  }
  action.kind = QA_HAND_ACTION_RECOVERING;
  qa_save_reader ready_at = field(reader, "readyAt");
  action.ready_at = qa_save_reader_finite(&ready_at);
  qa_save_reader_destroy(&ready_at);
  qa_save_reader require = field(reader, "requireRelease");
  action.require_release = qa_save_reader_boolean(&require);
  qa_save_reader_destroy(&require);
  return action;
}
static qa_save_value *build_hand_action_value(const qa_hand_action *in) {
  static const char *const names[] = {"idle", "disarmed", "preparing", "cooking", "releasing", "recovering"};
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  bool ok = set_own(obj, "kind", qa_save_value_string(names[in->kind]));
  if (ok && in->kind == QA_HAND_ACTION_PREPARING)
    ok = set_own(obj, "frame", qa_save_value_number(in->frame)) &&
         set_own(obj, "nextAt", qa_save_value_number(in->next_at)) &&
         set_own(obj, "releaseQueued", qa_save_value_boolean(in->release_queued));
  if (ok && in->kind == QA_HAND_ACTION_COOKING)
    ok = set_own(obj, "expiresAt", qa_save_value_number(in->expires_at));
  if (ok && in->kind == QA_HAND_ACTION_RELEASING)
    ok = set_own(obj, "expiresAt", qa_save_value_number(in->expires_at)) &&
         set_own(obj, "throwAt", qa_save_value_number(in->throw_at));
  if (ok && in->kind == QA_HAND_ACTION_RECOVERING)
    ok = set_own(obj, "readyAt", qa_save_value_number(in->ready_at)) &&
         set_own(obj, "requireRelease", qa_save_value_boolean(in->require_release));
  if (!ok) { qa_save_value_free(obj); return NULL; }
  return obj;
}

static void read_hand_grenade_loadout(const qa_save_reader *reader, qa_persist_hand_grenade_loadout *out);
static qa_save_value *build_hand_grenade_loadout(const qa_persist_hand_grenade_loadout *in);
static void read_hand_grenade_state(const qa_save_reader *reader, qa_hand_grenade_state *out);
static qa_save_value *build_hand_grenade_state(const qa_hand_grenade_state *in);
static void read_hand_grenades_actor(const qa_save_reader *reader, qa_hand_grenades_actor *out);
static qa_save_value *build_hand_grenades_actor(const qa_hand_grenades_actor *in);
static void read_hand_grenades_checkpoint(const qa_save_reader *reader, qa_persist_hand_grenades_checkpoint *out);
static qa_save_value *build_hand_grenades_checkpoint(const qa_persist_hand_grenades_checkpoint *in);

static void read_hand_grenade_loadout(const qa_save_reader *reader, qa_persist_hand_grenade_loadout *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "enabled");
    (*out).enabled = qa_save_reader_boolean(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "initialAmmo");
    (*out).initial_ammo = qa_save_reader_integer_min(&f1, 0);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "capacity");
    (*out).capacity = qa_save_reader_integer_min(&f2, 0);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "infiniteAmmo");
    (*out).infinite_ammo = qa_save_reader_boolean(&f3);
    qa_save_reader_destroy(&f3); }
  if (out->initial_ammo > out->capacity) qa_save_reader_fail(reader, "initial hand grenade allowance exceeds capacity");
}
static qa_save_value *build_hand_grenade_loadout(const qa_persist_hand_grenade_loadout *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "enabled", qa_save_value_boolean((*in).enabled))) goto fail;
  if (!set_own(obj, "initialAmmo", qa_save_value_number((*in).initial_ammo))) goto fail;
  if (!set_own(obj, "capacity", qa_save_value_number((*in).capacity))) goto fail;
  if (!set_own(obj, "infiniteAmmo", qa_save_value_boolean((*in).infinite_ammo))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_hand_grenade_state(const qa_save_reader *reader, qa_hand_grenade_state *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "config");
    read_hand_grenade_loadout(&f0, &(*out).config);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "action");
    (*out).action = read_hand_action_value(&f1);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_hand_grenade_state(const qa_hand_grenade_state *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "config", build_hand_grenade_loadout(&(*in).config))) goto fail;
  if (!set_own(obj, "action", build_hand_action_value(&(*in).action))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_hand_grenades_actor(const qa_save_reader *reader, qa_hand_grenades_actor *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "state");
    read_hand_grenade_state(&f1, &(*out).state);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_hand_grenades_actor(const qa_hand_grenades_actor *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "state", build_hand_grenade_state(&(*in).state))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_hand_grenades_checkpoint(const qa_save_reader *reader, qa_persist_hand_grenades_checkpoint *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "version");
    (*out).version = qa_save_reader_literal_number(&f0, 1);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "edition");
    static const char *const ch1[] = {"classic", "rerelease"};
    (*out).edition = qa_save_reader_choice_string(&f1, ch1, 2);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "actors");
    (*out).actors_count = qa_save_reader_list_count(&f2);
    (*out).actors = (qa_hand_grenades_actor *)calloc((*out).actors_count != 0 ? (*out).actors_count : 1, sizeof(qa_hand_grenades_actor));
    if ((*out).actors == NULL) qa_save_reader_fail(&f2, "out of memory");
    for (size_t i2 = 0; i2 < (*out).actors_count; i2++) {
      qa_save_reader it3 = item(&f2, i2);
      read_hand_grenades_actor(&it3, &(*out).actors[i2]);
      qa_save_reader_destroy(&it3);
    }
    qa_save_reader_destroy(&f2); }
}
static qa_save_value *build_hand_grenades_checkpoint(const qa_persist_hand_grenades_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "version", qa_save_value_number((*in).version))) goto fail;
  if (!set_own(obj, "edition", str_value((*in).edition))) goto fail;
  { qa_save_value *arr4 = qa_save_value_array();
    if (arr4 == NULL) goto fail;
    for (size_t i5 = 0; i5 < (*in).actors_count; i5++) {
      if (!push_own(arr4, build_hand_grenades_actor(&(*in).actors[i5]))) { qa_save_value_free(arr4); goto fail; }
    }
    if (!set_own(obj, "actors", arr4)) goto fail; }
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}




qa_persist_hand_grenades_checkpoint qa_read_hand_grenades_checkpoint(const qa_save_reader *reader) {
  qa_persist_hand_grenades_checkpoint out;
  read_hand_grenades_checkpoint(reader, &out);
  return out;
}
uint8_t *qa_encode_hand_grenades_checkpoint(const qa_persist_hand_grenades_checkpoint *checkpoint, size_t *out_length) {
  qa_save_value *value = build_hand_grenades_checkpoint(checkpoint);
  if (value == NULL) return NULL;
  uint8_t *bytes = qa_encode_checkpoint_value(value, out_length);
  qa_save_value_free(value);
  return bytes;
}
bool qa_decode_hand_grenades_checkpoint(const uint8_t *bytes, size_t length, qa_persist_hand_grenades_checkpoint *out) {
  qa_save_value *root = qa_decode_checkpoint_value(bytes, length);
  if (root == NULL) return false;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, root, "q2-hand-grenades")) { qa_save_value_free(root); return false; }
  read_hand_grenades_checkpoint(&reader, out);
  qa_save_reader_destroy(&reader);
  out->root = root;
  return true;
}

