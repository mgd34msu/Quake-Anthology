/* Port of src/persistence/q2-movers.ts. GPL-2.0-or-later. */
#include "qa/persistence/q2_movers.h"

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

static void read_q2_linear_curve(const qa_save_reader *reader, qa_q2_linear_curve *out);
static qa_save_value *build_q2_linear_curve(const qa_q2_linear_curve *in);
static void read_q2_linear_motion_state(const qa_save_reader *reader, qa_q2_linear_motion_state *out);
static qa_save_value *build_q2_linear_motion_state(const qa_q2_linear_motion_state *in);
static void read_q2_linear_motion_entry(const qa_save_reader *reader, qa_persist_q2_linear_motion_entry *out);
static qa_save_value *build_q2_linear_motion_entry(const qa_persist_q2_linear_motion_entry *in);
static void read_q2_linear_motion_checkpoint(const qa_save_reader *reader, qa_persist_q2_linear_motion_checkpoint *out);
static qa_save_value *build_q2_linear_motion_checkpoint(const qa_persist_q2_linear_motion_checkpoint *in);
static void read_q2_angular_motion_entry(const qa_save_reader *reader, qa_persist_q2_angular_motion_entry *out);
static qa_save_value *build_q2_angular_motion_entry(const qa_persist_q2_angular_motion_entry *in);
static void read_q2_angular_motion_checkpoint(const qa_save_reader *reader, qa_persist_q2_angular_motion_checkpoint *out);
static qa_save_value *build_q2_angular_motion_checkpoint(const qa_persist_q2_angular_motion_checkpoint *in);
static void read_q2_door_state(const qa_save_reader *reader, qa_q2_door_state *out);
static qa_save_value *build_q2_door_state(const qa_q2_door_state *in);
static void read_q2_door_entry(const qa_save_reader *reader, qa_q2_door_entry *out);
static qa_save_value *build_q2_door_entry(const qa_q2_door_entry *in);
static void read_q2_train_entry(const qa_save_reader *reader, qa_q2_train_entry *out);
static qa_save_value *build_q2_train_entry(const qa_q2_train_entry *in);
static void read_q2_movers_checkpoint(const qa_save_reader *reader, qa_persist_q2_movers_checkpoint *out);
static qa_save_value *build_q2_movers_checkpoint(const qa_persist_q2_movers_checkpoint *in);

static void read_q2_linear_curve(const qa_save_reader *reader, qa_q2_linear_curve *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "positions");
    (*out).positions_count = qa_save_reader_list_count(&f0);
    (*out).positions = (double *)calloc((*out).positions_count != 0 ? (*out).positions_count : 1, sizeof(double));
    if ((*out).positions == NULL) qa_save_reader_fail(&f0, "out of memory");
    for (size_t i1 = 0; i1 < (*out).positions_count; i1++) {
      qa_save_reader it2 = item(&f0, i1);
      (*out).positions[i1] = qa_save_reader_number(&it2);
      qa_save_reader_destroy(&it2);
    }
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "frame");
    (*out).frame = qa_save_reader_number(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "subframe");
    (*out).subframe = qa_save_reader_number(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "subframes");
    (*out).subframes = qa_save_reader_number(&f3);
    qa_save_reader_destroy(&f3); }
}
static qa_save_value *build_q2_linear_curve(const qa_q2_linear_curve *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  { qa_save_value *arr3 = qa_save_value_array();
    if (arr3 == NULL) goto fail;
    for (size_t i4 = 0; i4 < (*in).positions_count; i4++) {
      if (!push_own(arr3, qa_save_value_number((*in).positions[i4]))) { qa_save_value_free(arr3); goto fail; }
    }
    if (!set_own(obj, "positions", arr3)) goto fail; }
  if (!set_own(obj, "frame", qa_save_value_number((*in).frame))) goto fail;
  if (!set_own(obj, "subframe", qa_save_value_number((*in).subframe))) goto fail;
  if (!set_own(obj, "subframes", qa_save_value_number((*in).subframes))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_linear_motion_state(const qa_save_reader *reader, qa_q2_linear_motion_state *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "direction");
    (*out).direction = qa_read_vector(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "destination");
    (*out).destination = qa_read_vector(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "reference");
    (*out).reference = qa_read_vector(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "remaining");
    (*out).remaining = qa_save_reader_number(&f3);
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "currentSpeed");
    (*out).current_speed = qa_save_reader_number(&f4);
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "moveSpeed");
    (*out).move_speed = qa_save_reader_number(&f5);
    qa_save_reader_destroy(&f5); }
  { qa_save_reader f6 = field(reader, "nextSpeed");
    (*out).next_speed = qa_save_reader_number(&f6);
    qa_save_reader_destroy(&f6); }
  { qa_save_reader f7 = field(reader, "decelDistance");
    (*out).decel_distance = qa_save_reader_number(&f7);
    qa_save_reader_destroy(&f7); }
  { qa_save_reader f8 = field(reader, "done");
    (*out).done = qa_save_reader_string(&f8);
    qa_save_reader_destroy(&f8); }
  { qa_save_reader f9 = field(reader, "curve");
    if (!qa_save_reader_is_null(&f9)) { (*out).has_curve = true;
      read_q2_linear_curve(&f9, &(*out).curve);
    }
    qa_save_reader_destroy(&f9); }
}
static qa_save_value *build_q2_linear_motion_state(const qa_q2_linear_motion_state *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "direction", vec_value(&(*in).direction))) goto fail;
  if (!set_own(obj, "destination", vec_value(&(*in).destination))) goto fail;
  if (!set_own(obj, "reference", vec_value(&(*in).reference))) goto fail;
  if (!set_own(obj, "remaining", qa_save_value_number((*in).remaining))) goto fail;
  if (!set_own(obj, "currentSpeed", qa_save_value_number((*in).current_speed))) goto fail;
  if (!set_own(obj, "moveSpeed", qa_save_value_number((*in).move_speed))) goto fail;
  if (!set_own(obj, "nextSpeed", qa_save_value_number((*in).next_speed))) goto fail;
  if (!set_own(obj, "decelDistance", qa_save_value_number((*in).decel_distance))) goto fail;
  if (!set_own(obj, "done", str_value((*in).done))) goto fail;
  if (!set_own(obj, "curve", (*in).has_curve ? build_q2_linear_curve(&(*in).curve) : qa_save_value_null())) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_linear_motion_entry(const qa_save_reader *reader, qa_persist_q2_linear_motion_entry *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "state");
    read_q2_linear_motion_state(&f1, &(*out).state);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_linear_motion_entry(const qa_persist_q2_linear_motion_entry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "state", build_q2_linear_motion_state(&(*in).state))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_linear_motion_checkpoint(const qa_save_reader *reader, qa_persist_q2_linear_motion_checkpoint *out) {
  memset(out, 0, sizeof *out);
  out->count = qa_save_reader_list_count(reader);
  out->entries = (qa_persist_q2_linear_motion_entry *)calloc(out->count != 0 ? out->count : 1, sizeof(qa_persist_q2_linear_motion_entry));
  if (out->entries == NULL) qa_save_reader_fail(reader, "out of memory");
  for (size_t i5 = 0; i5 < out->count; i5++) {
    qa_save_reader it6 = item(reader, i5);
    read_q2_linear_motion_entry(&it6, &out->entries[i5]);
    qa_save_reader_destroy(&it6);
  }
}
static qa_save_value *build_q2_linear_motion_checkpoint(const qa_persist_q2_linear_motion_checkpoint *in) {
  qa_save_value *obj = NULL; (void)obj;
  { qa_save_value *arr7 = qa_save_value_array();
    if (arr7 == NULL) goto fail;
    for (size_t i8 = 0; i8 < in->count; i8++) {
      if (!push_own(arr7, build_q2_linear_motion_entry(&in->entries[i8]))) { qa_save_value_free(arr7); goto fail; }
    }
    return arr7; }
fail: return NULL;
}

static void read_q2_angular_motion_entry(const qa_save_reader *reader, qa_persist_q2_angular_motion_entry *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "destination");
    (*out).destination = qa_read_vector(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "speed");
    (*out).speed = qa_save_reader_number(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "done");
    (*out).done = qa_save_reader_string(&f3);
    qa_save_reader_destroy(&f3); }
}
static qa_save_value *build_q2_angular_motion_entry(const qa_persist_q2_angular_motion_entry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "destination", vec_value(&(*in).destination))) goto fail;
  if (!set_own(obj, "speed", qa_save_value_number((*in).speed))) goto fail;
  if (!set_own(obj, "done", str_value((*in).done))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_angular_motion_checkpoint(const qa_save_reader *reader, qa_persist_q2_angular_motion_checkpoint *out) {
  memset(out, 0, sizeof *out);
  out->count = qa_save_reader_list_count(reader);
  out->entries = (qa_persist_q2_angular_motion_entry *)calloc(out->count != 0 ? out->count : 1, sizeof(qa_persist_q2_angular_motion_entry));
  if (out->entries == NULL) qa_save_reader_fail(reader, "out of memory");
  for (size_t i9 = 0; i9 < out->count; i9++) {
    qa_save_reader it10 = item(reader, i9);
    read_q2_angular_motion_entry(&it10, &out->entries[i9]);
    qa_save_reader_destroy(&it10);
  }
}
static qa_save_value *build_q2_angular_motion_checkpoint(const qa_persist_q2_angular_motion_checkpoint *in) {
  qa_save_value *obj = NULL; (void)obj;
  { qa_save_value *arr11 = qa_save_value_array();
    if (arr11 == NULL) goto fail;
    for (size_t i12 = 0; i12 < in->count; i12++) {
      if (!push_own(arr11, build_q2_angular_motion_entry(&in->entries[i12]))) { qa_save_value_free(arr11); goto fail; }
    }
    return arr11; }
fail: return NULL;
}

static void read_q2_door_state(const qa_save_reader *reader, qa_q2_door_state *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "start");
    (*out).start = qa_read_vector(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "end");
    (*out).end = qa_read_vector(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "distance");
    (*out).distance = qa_save_reader_number(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "button");
    (*out).button = qa_save_reader_boolean(&f3);
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "angular");
    (*out).angular = qa_save_reader_boolean(&f4);
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "water");
    (*out).water = qa_save_reader_boolean(&f5);
    qa_save_reader_destroy(&f5); }
  { qa_save_reader f6 = field(reader, "safeDirection");
    (*out).safe_direction = qa_read_vector(&f6);
    qa_save_reader_destroy(&f6); }
  { qa_save_reader f7 = field(reader, "waterDivisor");
    (*out).water_divisor = qa_save_reader_number(&f7);
    qa_save_reader_destroy(&f7); }
  { qa_save_reader f8 = field(reader, "reversed");
    (*out).reversed = qa_save_reader_boolean(&f8);
    qa_save_reader_destroy(&f8); }
  { qa_save_reader f9 = field(reader, "activated");
    (*out).activated = qa_save_reader_boolean(&f9);
    qa_save_reader_destroy(&f9); }
  { qa_save_reader f10 = field(reader, "phase");
    static const char *const ch13[] = {"bottom", "up", "top", "down"};
    (*out).phase = qa_save_reader_choice_string(&f10, ch13, 4);
    qa_save_reader_destroy(&f10); }
  { qa_save_reader f11 = field(reader, "debounce");
    (*out).debounce = qa_save_reader_number(&f11);
    qa_save_reader_destroy(&f11); }
}
static qa_save_value *build_q2_door_state(const qa_q2_door_state *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "start", vec_value(&(*in).start))) goto fail;
  if (!set_own(obj, "end", vec_value(&(*in).end))) goto fail;
  if (!set_own(obj, "distance", qa_save_value_number((*in).distance))) goto fail;
  if (!set_own(obj, "button", qa_save_value_boolean((*in).button))) goto fail;
  if (!set_own(obj, "angular", qa_save_value_boolean((*in).angular))) goto fail;
  if (!set_own(obj, "water", qa_save_value_boolean((*in).water))) goto fail;
  if (!set_own(obj, "safeDirection", vec_value(&(*in).safe_direction))) goto fail;
  if (!set_own(obj, "waterDivisor", qa_save_value_number((*in).water_divisor))) goto fail;
  if (!set_own(obj, "reversed", qa_save_value_boolean((*in).reversed))) goto fail;
  if (!set_own(obj, "activated", qa_save_value_boolean((*in).activated))) goto fail;
  if (!set_own(obj, "phase", str_value((*in).phase))) goto fail;
  if (!set_own(obj, "debounce", qa_save_value_number((*in).debounce))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_door_entry(const qa_save_reader *reader, qa_q2_door_entry *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "master");
    (*out).master = read_actor(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "team");
    (*out).team_count = qa_save_reader_list_count(&f2);
    (*out).team = (qa_saved_actor_id *)calloc((*out).team_count != 0 ? (*out).team_count : 1, sizeof(qa_saved_actor_id));
    if ((*out).team == NULL) qa_save_reader_fail(&f2, "out of memory");
    for (size_t i14 = 0; i14 < (*out).team_count; i14++) {
      qa_save_reader it15 = item(&f2, i14);
      (*out).team[i14] = read_actor(&it15);
      qa_save_reader_destroy(&it15);
    }
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "state");
    read_q2_door_state(&f3, &(*out).state);
    qa_save_reader_destroy(&f3); }
}
static qa_save_value *build_q2_door_entry(const qa_q2_door_entry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "master", actor_value(&(*in).master))) goto fail;
  { qa_save_value *arr16 = qa_save_value_array();
    if (arr16 == NULL) goto fail;
    for (size_t i17 = 0; i17 < (*in).team_count; i17++) {
      if (!push_own(arr16, actor_value(&(*in).team[i17]))) { qa_save_value_free(arr16); goto fail; }
    }
    if (!set_own(obj, "team", arr16)) goto fail; }
  if (!set_own(obj, "state", build_q2_door_state(&(*in).state))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_train_entry(const qa_save_reader *reader, qa_q2_train_entry *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "destination");
    if (!qa_save_reader_is_null(&f1)) { (*out).has_destination = true;
      (*out).destination = read_actor(&f1);
    }
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "debounce");
    (*out).debounce = qa_save_reader_number(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "ship");
    (*out).ship = qa_save_reader_boolean(&f3);
    qa_save_reader_destroy(&f3); }
}
static qa_save_value *build_q2_train_entry(const qa_q2_train_entry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "destination", (*in).has_destination ? actor_value(&(*in).destination) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "debounce", qa_save_value_number((*in).debounce))) goto fail;
  if (!set_own(obj, "ship", qa_save_value_boolean((*in).ship))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_movers_checkpoint(const qa_save_reader *reader, qa_persist_q2_movers_checkpoint *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "doors");
    (*out).doors_count = qa_save_reader_list_count(&f0);
    (*out).doors = (qa_q2_door_entry *)calloc((*out).doors_count != 0 ? (*out).doors_count : 1, sizeof(qa_q2_door_entry));
    if ((*out).doors == NULL) qa_save_reader_fail(&f0, "out of memory");
    for (size_t i18 = 0; i18 < (*out).doors_count; i18++) {
      qa_save_reader it19 = item(&f0, i18);
      read_q2_door_entry(&it19, &(*out).doors[i18]);
      qa_save_reader_destroy(&it19);
    }
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "trains");
    (*out).trains_count = qa_save_reader_list_count(&f1);
    (*out).trains = (qa_q2_train_entry *)calloc((*out).trains_count != 0 ? (*out).trains_count : 1, sizeof(qa_q2_train_entry));
    if ((*out).trains == NULL) qa_save_reader_fail(&f1, "out of memory");
    for (size_t i20 = 0; i20 < (*out).trains_count; i20++) {
      qa_save_reader it21 = item(&f1, i20);
      read_q2_train_entry(&it21, &(*out).trains[i20]);
      qa_save_reader_destroy(&it21);
    }
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "linear");
    read_q2_linear_motion_checkpoint(&f2, &(*out).linear);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "angular");
    read_q2_angular_motion_checkpoint(&f3, &(*out).angular);
    qa_save_reader_destroy(&f3); }
}
static qa_save_value *build_q2_movers_checkpoint(const qa_persist_q2_movers_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  { qa_save_value *arr22 = qa_save_value_array();
    if (arr22 == NULL) goto fail;
    for (size_t i23 = 0; i23 < (*in).doors_count; i23++) {
      if (!push_own(arr22, build_q2_door_entry(&(*in).doors[i23]))) { qa_save_value_free(arr22); goto fail; }
    }
    if (!set_own(obj, "doors", arr22)) goto fail; }
  { qa_save_value *arr24 = qa_save_value_array();
    if (arr24 == NULL) goto fail;
    for (size_t i25 = 0; i25 < (*in).trains_count; i25++) {
      if (!push_own(arr24, build_q2_train_entry(&(*in).trains[i25]))) { qa_save_value_free(arr24); goto fail; }
    }
    if (!set_own(obj, "trains", arr24)) goto fail; }
  if (!set_own(obj, "linear", build_q2_linear_motion_checkpoint(&(*in).linear))) goto fail;
  if (!set_own(obj, "angular", build_q2_angular_motion_checkpoint(&(*in).angular))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}




qa_persist_q2_linear_motion_checkpoint qa_read_q2_linear_motion_checkpoint(const qa_save_reader *reader) {
  qa_persist_q2_linear_motion_checkpoint out;
  read_q2_linear_motion_checkpoint(reader, &out);
  return out;
}
qa_save_value *qa_q2_linear_motion_checkpoint_value(const qa_persist_q2_linear_motion_checkpoint *checkpoint) {
  return build_q2_linear_motion_checkpoint(checkpoint);
}


qa_persist_q2_angular_motion_checkpoint qa_read_q2_angular_motion_checkpoint(const qa_save_reader *reader) {
  qa_persist_q2_angular_motion_checkpoint out;
  read_q2_angular_motion_checkpoint(reader, &out);
  return out;
}
qa_save_value *qa_q2_angular_motion_checkpoint_value(const qa_persist_q2_angular_motion_checkpoint *checkpoint) {
  return build_q2_angular_motion_checkpoint(checkpoint);
}




qa_persist_q2_movers_checkpoint qa_read_q2_movers_checkpoint(const qa_save_reader *reader) {
  qa_persist_q2_movers_checkpoint out;
  read_q2_movers_checkpoint(reader, &out);
  return out;
}
uint8_t *qa_encode_q2_movers_checkpoint(const qa_persist_q2_movers_checkpoint *checkpoint, size_t *out_length) {
  qa_save_value *value = build_q2_movers_checkpoint(checkpoint);
  if (value == NULL) return NULL;
  uint8_t *bytes = qa_encode_checkpoint_value(value, out_length);
  qa_save_value_free(value);
  return bytes;
}
bool qa_decode_q2_movers_checkpoint(const uint8_t *bytes, size_t length, qa_persist_q2_movers_checkpoint *out) {
  qa_save_value *root = qa_decode_checkpoint_value(bytes, length);
  if (root == NULL) return false;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, root, "q2-movers")) { qa_save_value_free(root); return false; }
  read_q2_movers_checkpoint(&reader, out);
  qa_save_reader_destroy(&reader);
  out->root = root;
  return true;
}

