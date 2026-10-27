/* Port of src/persistence/q2-base-entities.ts. GPL-2.0-or-later. */
#include "qa/persistence/q2_base_entities.h"

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

static void read_q2_platform_state(const qa_save_reader *reader, qa_q2_platform_state *out);
static qa_save_value *build_q2_platform_state(const qa_q2_platform_state *in);
static void read_q2_platform_entry(const qa_save_reader *reader, qa_q2_platform_entry *out);
static qa_save_value *build_q2_platform_entry(const qa_q2_platform_entry *in);
static void read_q2_secret_state(const qa_save_reader *reader, qa_q2_secret_state *out);
static qa_save_value *build_q2_secret_state(const qa_q2_secret_state *in);
static void read_q2_secret_entry(const qa_save_reader *reader, qa_q2_secret_entry *out);
static qa_save_value *build_q2_secret_entry(const qa_q2_secret_entry *in);
static void read_q2_base_movers(const qa_save_reader *reader, qa_q2_base_movers *out);
static qa_save_value *build_q2_base_movers(const qa_q2_base_movers *in);
static void read_q2_scenery_animation(const qa_save_reader *reader, qa_q2_scenery_animation *out);
static qa_save_value *build_q2_scenery_animation(const qa_q2_scenery_animation *in);
static void read_q2_scenery_clock(const qa_save_reader *reader, qa_q2_scenery_clock *out);
static qa_save_value *build_q2_scenery_clock(const qa_q2_scenery_clock *in);
static void read_q2_scenery(const qa_save_reader *reader, qa_q2_scenery *out);
static qa_save_value *build_q2_scenery(const qa_q2_scenery *in);
static void read_q2_turret_breach_state(const qa_save_reader *reader, qa_q2_turret_breach_state *out);
static qa_save_value *build_q2_turret_breach_state(const qa_q2_turret_breach_state *in);
static void read_q2_turret_breach_entry(const qa_save_reader *reader, qa_q2_turret_breach_entry *out);
static qa_save_value *build_q2_turret_breach_entry(const qa_q2_turret_breach_entry *in);
static void read_q2_turret_driver_entry(const qa_save_reader *reader, qa_q2_turret_driver_entry *out);
static qa_save_value *build_q2_turret_driver_entry(const qa_q2_turret_driver_entry *in);
static void read_q2_turrets(const qa_save_reader *reader, qa_q2_turrets *out);
static qa_save_value *build_q2_turrets(const qa_q2_turrets *in);
static void read_q2_wind_time_entry(const qa_save_reader *reader, qa_q2_wind_time_entry *out);
static qa_save_value *build_q2_wind_time_entry(const qa_q2_wind_time_entry *in);
static void read_q2_base_entities_checkpoint(const qa_save_reader *reader, qa_q2_base_entities_checkpoint *out);
static qa_save_value *build_q2_base_entities_checkpoint(const qa_q2_base_entities_checkpoint *in);

static void read_q2_platform_state(const qa_save_reader *reader, qa_q2_platform_state *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "top");
    (*out).top = qa_read_vector(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "bottom");
    (*out).bottom = qa_read_vector(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "phase");
    static const char *const ch1[] = {"top", "bottom", "up", "down"};
    (*out).phase = qa_save_reader_choice_string(&f2, ch1, 4);
    qa_save_reader_destroy(&f2); }
}
static qa_save_value *build_q2_platform_state(const qa_q2_platform_state *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "top", vec_value(&(*in).top))) goto fail;
  if (!set_own(obj, "bottom", vec_value(&(*in).bottom))) goto fail;
  if (!set_own(obj, "phase", str_value((*in).phase))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_platform_entry(const qa_save_reader *reader, qa_q2_platform_entry *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "state");
    read_q2_platform_state(&f1, &(*out).state);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_platform_entry(const qa_q2_platform_entry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "state", build_q2_platform_state(&(*in).state))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_secret_state(const qa_save_reader *reader, qa_q2_secret_state *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "first");
    (*out).first = qa_read_vector(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "second");
    (*out).second = qa_read_vector(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "home");
    (*out).home = qa_read_vector(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "shootable");
    (*out).shootable = qa_save_reader_boolean(&f3);
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "blockedTime");
    (*out).blocked_time = qa_save_reader_number(&f4);
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "messageTime");
    (*out).message_time = qa_save_reader_number(&f5);
    qa_save_reader_destroy(&f5); }
}
static qa_save_value *build_q2_secret_state(const qa_q2_secret_state *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "first", vec_value(&(*in).first))) goto fail;
  if (!set_own(obj, "second", vec_value(&(*in).second))) goto fail;
  if (!set_own(obj, "home", vec_value(&(*in).home))) goto fail;
  if (!set_own(obj, "shootable", qa_save_value_boolean((*in).shootable))) goto fail;
  if (!set_own(obj, "blockedTime", qa_save_value_number((*in).blocked_time))) goto fail;
  if (!set_own(obj, "messageTime", qa_save_value_number((*in).message_time))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_secret_entry(const qa_save_reader *reader, qa_q2_secret_entry *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "state");
    read_q2_secret_state(&f1, &(*out).state);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_secret_entry(const qa_q2_secret_entry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "state", build_q2_secret_state(&(*in).state))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_base_movers(const qa_save_reader *reader, qa_q2_base_movers *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "platforms");
    (*out).platforms_count = qa_save_reader_list_count(&f0);
    (*out).platforms = (qa_q2_platform_entry *)calloc((*out).platforms_count != 0 ? (*out).platforms_count : 1, sizeof(qa_q2_platform_entry));
    if ((*out).platforms == NULL) qa_save_reader_fail(&f0, "out of memory");
    for (size_t i2 = 0; i2 < (*out).platforms_count; i2++) {
      qa_save_reader it3 = item(&f0, i2);
      read_q2_platform_entry(&it3, &(*out).platforms[i2]);
      qa_save_reader_destroy(&it3);
    }
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "secrets");
    (*out).secrets_count = qa_save_reader_list_count(&f1);
    (*out).secrets = (qa_q2_secret_entry *)calloc((*out).secrets_count != 0 ? (*out).secrets_count : 1, sizeof(qa_q2_secret_entry));
    if ((*out).secrets == NULL) qa_save_reader_fail(&f1, "out of memory");
    for (size_t i4 = 0; i4 < (*out).secrets_count; i4++) {
      qa_save_reader it5 = item(&f1, i4);
      read_q2_secret_entry(&it5, &(*out).secrets[i4]);
      qa_save_reader_destroy(&it5);
    }
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "linear");
    (*out).linear = qa_read_q2_linear_motion_checkpoint(&f2);
    qa_save_reader_destroy(&f2); }
}
static qa_save_value *build_q2_base_movers(const qa_q2_base_movers *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  { qa_save_value *arr6 = qa_save_value_array();
    if (arr6 == NULL) goto fail;
    for (size_t i7 = 0; i7 < (*in).platforms_count; i7++) {
      if (!push_own(arr6, build_q2_platform_entry(&(*in).platforms[i7]))) { qa_save_value_free(arr6); goto fail; }
    }
    if (!set_own(obj, "platforms", arr6)) goto fail; }
  { qa_save_value *arr8 = qa_save_value_array();
    if (arr8 == NULL) goto fail;
    for (size_t i9 = 0; i9 < (*in).secrets_count; i9++) {
      if (!push_own(arr8, build_q2_secret_entry(&(*in).secrets[i9]))) { qa_save_value_free(arr8); goto fail; }
    }
    if (!set_own(obj, "secrets", arr8)) goto fail; }
  if (!set_own(obj, "linear", qa_q2_linear_motion_checkpoint_value(&(*in).linear))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_scenery_animation(const qa_save_reader *reader, qa_q2_scenery_animation *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "first");
    (*out).first = qa_save_reader_integer_min(&f1, 0);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "end");
    (*out).end = qa_save_reader_integer_min(&f2, 1);
    qa_save_reader_destroy(&f2); }
}
static qa_save_value *build_q2_scenery_animation(const qa_q2_scenery_animation *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "first", qa_save_value_number((*in).first))) goto fail;
  if (!set_own(obj, "end", qa_save_value_number((*in).end))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_scenery_clock(const qa_save_reader *reader, qa_q2_scenery_clock *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "value");
    (*out).value = qa_save_reader_number(&f1);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_scenery_clock(const qa_q2_scenery_clock *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "value", qa_save_value_number((*in).value))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_scenery(const qa_save_reader *reader, qa_q2_scenery *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "animations");
    (*out).animations_count = qa_save_reader_list_count(&f0);
    (*out).animations = (qa_q2_scenery_animation *)calloc((*out).animations_count != 0 ? (*out).animations_count : 1, sizeof(qa_q2_scenery_animation));
    if ((*out).animations == NULL) qa_save_reader_fail(&f0, "out of memory");
    for (size_t i10 = 0; i10 < (*out).animations_count; i10++) {
      qa_save_reader it11 = item(&f0, i10);
      read_q2_scenery_animation(&it11, &(*out).animations[i10]);
      qa_save_reader_destroy(&it11);
    }
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "clocks");
    (*out).clocks_count = qa_save_reader_list_count(&f1);
    (*out).clocks = (qa_q2_scenery_clock *)calloc((*out).clocks_count != 0 ? (*out).clocks_count : 1, sizeof(qa_q2_scenery_clock));
    if ((*out).clocks == NULL) qa_save_reader_fail(&f1, "out of memory");
    for (size_t i12 = 0; i12 < (*out).clocks_count; i12++) {
      qa_save_reader it13 = item(&f1, i12);
      read_q2_scenery_clock(&it13, &(*out).clocks[i12]);
      qa_save_reader_destroy(&it13);
    }
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_scenery(const qa_q2_scenery *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  { qa_save_value *arr14 = qa_save_value_array();
    if (arr14 == NULL) goto fail;
    for (size_t i15 = 0; i15 < (*in).animations_count; i15++) {
      if (!push_own(arr14, build_q2_scenery_animation(&(*in).animations[i15]))) { qa_save_value_free(arr14); goto fail; }
    }
    if (!set_own(obj, "animations", arr14)) goto fail; }
  { qa_save_value *arr16 = qa_save_value_array();
    if (arr16 == NULL) goto fail;
    for (size_t i17 = 0; i17 < (*in).clocks_count; i17++) {
      if (!push_own(arr16, build_q2_scenery_clock(&(*in).clocks[i17]))) { qa_save_value_free(arr16); goto fail; }
    }
    if (!set_own(obj, "clocks", arr16)) goto fail; }
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_turret_breach_state(const qa_save_reader *reader, qa_q2_turret_breach_state *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "goal");
    (*out).goal = qa_read_vector(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "muzzle");
    (*out).muzzle = qa_read_vector(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "pitchMax");
    (*out).pitch_max = qa_save_reader_number(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "pitchMin");
    (*out).pitch_min = qa_save_reader_number(&f3);
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "yawMin");
    (*out).yaw_min = qa_save_reader_number(&f4);
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "yawMax");
    (*out).yaw_max = qa_save_reader_number(&f5);
    qa_save_reader_destroy(&f5); }
}
static qa_save_value *build_q2_turret_breach_state(const qa_q2_turret_breach_state *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "goal", vec_value(&(*in).goal))) goto fail;
  if (!set_own(obj, "muzzle", vec_value(&(*in).muzzle))) goto fail;
  if (!set_own(obj, "pitchMax", qa_save_value_number((*in).pitch_max))) goto fail;
  if (!set_own(obj, "pitchMin", qa_save_value_number((*in).pitch_min))) goto fail;
  if (!set_own(obj, "yawMin", qa_save_value_number((*in).yaw_min))) goto fail;
  if (!set_own(obj, "yawMax", qa_save_value_number((*in).yaw_max))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_turret_breach_entry(const qa_save_reader *reader, qa_q2_turret_breach_entry *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "state");
    read_q2_turret_breach_state(&f1, &(*out).state);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_turret_breach_entry(const qa_q2_turret_breach_entry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "state", build_q2_turret_breach_state(&(*in).state))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_turret_driver_entry(const qa_save_reader *reader, qa_q2_turret_driver_entry *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "breach");
    if (!qa_save_reader_is_null(&f1)) { (*out).has_breach = true;
      (*out).breach = read_actor(&f1);
    }
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "radius");
    (*out).radius = qa_save_reader_number(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "yawOffset");
    (*out).yaw_offset = qa_save_reader_number(&f3);
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "height");
    (*out).height = qa_save_reader_number(&f4);
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "monsterDie");
    (*out).monster_die = qa_save_reader_string(&f5);
    qa_save_reader_destroy(&f5); }
}
static qa_save_value *build_q2_turret_driver_entry(const qa_q2_turret_driver_entry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "breach", (*in).has_breach ? actor_value(&(*in).breach) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "radius", qa_save_value_number((*in).radius))) goto fail;
  if (!set_own(obj, "yawOffset", qa_save_value_number((*in).yaw_offset))) goto fail;
  if (!set_own(obj, "height", qa_save_value_number((*in).height))) goto fail;
  if (!set_own(obj, "monsterDie", str_value((*in).monster_die))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_turrets(const qa_save_reader *reader, qa_q2_turrets *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "breaches");
    (*out).breaches_count = qa_save_reader_list_count(&f0);
    (*out).breaches = (qa_q2_turret_breach_entry *)calloc((*out).breaches_count != 0 ? (*out).breaches_count : 1, sizeof(qa_q2_turret_breach_entry));
    if ((*out).breaches == NULL) qa_save_reader_fail(&f0, "out of memory");
    for (size_t i18 = 0; i18 < (*out).breaches_count; i18++) {
      qa_save_reader it19 = item(&f0, i18);
      read_q2_turret_breach_entry(&it19, &(*out).breaches[i18]);
      qa_save_reader_destroy(&it19);
    }
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "drivers");
    (*out).drivers_count = qa_save_reader_list_count(&f1);
    (*out).drivers = (qa_q2_turret_driver_entry *)calloc((*out).drivers_count != 0 ? (*out).drivers_count : 1, sizeof(qa_q2_turret_driver_entry));
    if ((*out).drivers == NULL) qa_save_reader_fail(&f1, "out of memory");
    for (size_t i20 = 0; i20 < (*out).drivers_count; i20++) {
      qa_save_reader it21 = item(&f1, i20);
      read_q2_turret_driver_entry(&it21, &(*out).drivers[i20]);
      qa_save_reader_destroy(&it21);
    }
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_turrets(const qa_q2_turrets *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  { qa_save_value *arr22 = qa_save_value_array();
    if (arr22 == NULL) goto fail;
    for (size_t i23 = 0; i23 < (*in).breaches_count; i23++) {
      if (!push_own(arr22, build_q2_turret_breach_entry(&(*in).breaches[i23]))) { qa_save_value_free(arr22); goto fail; }
    }
    if (!set_own(obj, "breaches", arr22)) goto fail; }
  { qa_save_value *arr24 = qa_save_value_array();
    if (arr24 == NULL) goto fail;
    for (size_t i25 = 0; i25 < (*in).drivers_count; i25++) {
      if (!push_own(arr24, build_q2_turret_driver_entry(&(*in).drivers[i25]))) { qa_save_value_free(arr24); goto fail; }
    }
    if (!set_own(obj, "drivers", arr24)) goto fail; }
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_wind_time_entry(const qa_save_reader *reader, qa_q2_wind_time_entry *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "until");
    (*out).until = qa_save_reader_number(&f1);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_wind_time_entry(const qa_q2_wind_time_entry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "until", qa_save_value_number((*in).until))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_base_entities_checkpoint(const qa_save_reader *reader, qa_q2_base_entities_checkpoint *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "version");
    (*out).version = qa_save_reader_literal_number(&f0, 1);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "movers");
    read_q2_base_movers(&f1, &(*out).movers);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "scenery");
    read_q2_scenery(&f2, &(*out).scenery);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "turrets");
    read_q2_turrets(&f3, &(*out).turrets);
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "windTimes");
    (*out).wind_times_count = qa_save_reader_list_count(&f4);
    (*out).wind_times = (qa_q2_wind_time_entry *)calloc((*out).wind_times_count != 0 ? (*out).wind_times_count : 1, sizeof(qa_q2_wind_time_entry));
    if ((*out).wind_times == NULL) qa_save_reader_fail(&f4, "out of memory");
    for (size_t i26 = 0; i26 < (*out).wind_times_count; i26++) {
      qa_save_reader it27 = item(&f4, i26);
      read_q2_wind_time_entry(&it27, &(*out).wind_times[i26]);
      qa_save_reader_destroy(&it27);
    }
    qa_save_reader_destroy(&f4); }
}
static qa_save_value *build_q2_base_entities_checkpoint(const qa_q2_base_entities_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "version", qa_save_value_number((*in).version))) goto fail;
  if (!set_own(obj, "movers", build_q2_base_movers(&(*in).movers))) goto fail;
  if (!set_own(obj, "scenery", build_q2_scenery(&(*in).scenery))) goto fail;
  if (!set_own(obj, "turrets", build_q2_turrets(&(*in).turrets))) goto fail;
  { qa_save_value *arr28 = qa_save_value_array();
    if (arr28 == NULL) goto fail;
    for (size_t i29 = 0; i29 < (*in).wind_times_count; i29++) {
      if (!push_own(arr28, build_q2_wind_time_entry(&(*in).wind_times[i29]))) { qa_save_value_free(arr28); goto fail; }
    }
    if (!set_own(obj, "windTimes", arr28)) goto fail; }
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}














qa_q2_base_entities_checkpoint qa_read_q2_base_entities_checkpoint(const qa_save_reader *reader) {
  qa_q2_base_entities_checkpoint out;
  read_q2_base_entities_checkpoint(reader, &out);
  return out;
}
uint8_t *qa_encode_q2_base_entities_checkpoint(const qa_q2_base_entities_checkpoint *checkpoint, size_t *out_length) {
  qa_save_value *value = build_q2_base_entities_checkpoint(checkpoint);
  if (value == NULL) return NULL;
  uint8_t *bytes = qa_encode_checkpoint_value(value, out_length);
  qa_save_value_free(value);
  return bytes;
}
bool qa_decode_q2_base_entities_checkpoint(const uint8_t *bytes, size_t length, qa_q2_base_entities_checkpoint *out) {
  qa_save_value *root = qa_decode_checkpoint_value(bytes, length);
  if (root == NULL) return false;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, root, "q2-base-entities")) { qa_save_value_free(root); return false; }
  read_q2_base_entities_checkpoint(&reader, out);
  qa_save_reader_destroy(&reader);
  out->root = root;
  return true;
}

