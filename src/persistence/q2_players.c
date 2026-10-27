/* Port of src/persistence/q2-players.ts. GPL-2.0-or-later. */
#include "qa/persistence/q2_players.h"

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


static const char *choice_field(const qa_save_reader *reader, const char *name,
                                const char *const *choices, size_t count) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_save_reader_choice_string(&f, choices, count);
  qa_save_reader_destroy(&f);
  return value;
}
static double number_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  double value = qa_save_reader_number(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static const char *string_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_save_reader_string(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static bool bool_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  bool value = qa_save_reader_boolean(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static const char *ns_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_namespaced(&f);
  qa_save_reader_destroy(&f);
  return value;
}
/* Local copies of save-image.ts readRegularArmor/readPoweredProtection/
 * readArmor/readInventoryEntry (save-image not yet ported). */
static qa_regular_armor_state read_regular_armor(const qa_save_reader *reader) {
  static const char *const kinds[] = {"none", "q1", "q2", "q3", "source"};
  qa_regular_armor_state armor;
  memset(&armor, 0, sizeof armor);
  const char *kind = choice_field(reader, "kind", kinds, 5);
  if (strcmp(kind, "none") == 0) { armor.kind = QA_REGULAR_ARMOR_NONE; return armor; }
  if (strcmp(kind, "source") == 0) {
    armor.kind = QA_REGULAR_ARMOR_SOURCE;
    armor.points = number_field(reader, "points");
    qa_save_reader item = field(reader, "item");
    armor.item = qa_save_reader_is_null(&item) ? NULL : qa_namespaced(&item);
    qa_save_reader_destroy(&item);
    return armor;
  }
  if (strcmp(kind, "q1") == 0) {
    armor.kind = QA_REGULAR_ARMOR_Q1;
    armor.points = number_field(reader, "points");
    armor.absorption = number_field(reader, "absorption");
    armor.item = ns_field(reader, "item");
    return armor;
  }
  if (strcmp(kind, "q3") == 0) {
    armor.kind = QA_REGULAR_ARMOR_Q3;
    armor.points = number_field(reader, "points");
    armor.protection = number_field(reader, "protection");
    return armor;
  }
  armor.kind = QA_REGULAR_ARMOR_Q2;
  armor.points = number_field(reader, "points");
  armor.normal_protection = number_field(reader, "normalProtection");
  armor.energy_protection = number_field(reader, "energyProtection");
  armor.item = ns_field(reader, "item");
  return armor;
}
static qa_powered_protection_state read_powered(const qa_save_reader *reader) {
  static const char *const kinds[] = {"none", "screen", "shield"};
  qa_powered_protection_state state;
  memset(&state, 0, sizeof state);
  state.kind = choice_field(reader, "kind", kinds, 3);
  if (strcmp(state.kind, "none") != 0) state.cells = number_field(reader, "cells");
  return state;
}
static qa_armor_state read_armor_value(const qa_save_reader *reader) {
  qa_armor_state armor;
  memset(&armor, 0, sizeof armor);
  qa_save_reader regular = field(reader, "regular");
  bool has_regular = !qa_save_reader_is_undefined(&regular);
  qa_save_reader_destroy(&regular);
  if (has_regular) {
    qa_save_reader r = field(reader, "regular");
    armor.regular = read_regular_armor(&r);
    qa_save_reader_destroy(&r);
    qa_save_reader p = field(reader, "powered");
    armor.powered = read_powered(&p);
    qa_save_reader_destroy(&p);
    return armor;
  }
  armor.regular = read_regular_armor(reader);
  if (armor.regular.kind == QA_REGULAR_ARMOR_Q2) {
    qa_save_reader p = field(reader, "powerArmor");
    armor.powered = read_powered(&p);
    qa_save_reader_destroy(&p);
  } else {
    armor.powered.kind = "none";
  }
  return armor;
}
static qa_save_value *build_regular_armor(const qa_regular_armor_state *in) {
  static const char *const names[] = {"none", "q1", "q2", "q3", "source"};
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  bool ok = set_own(obj, "kind", qa_save_value_string(names[in->kind]));
  if (ok && in->kind == QA_REGULAR_ARMOR_SOURCE)
    ok = set_own(obj, "points", qa_save_value_number(in->points)) &&
         set_own(obj, "item", in->item != NULL ? qa_save_value_string(in->item) : qa_save_value_null());
  if (ok && in->kind == QA_REGULAR_ARMOR_Q1)
    ok = set_own(obj, "points", qa_save_value_number(in->points)) &&
         set_own(obj, "absorption", qa_save_value_number(in->absorption)) &&
         set_own(obj, "item", qa_save_value_string(in->item != NULL ? in->item : ""));
  if (ok && in->kind == QA_REGULAR_ARMOR_Q3)
    ok = set_own(obj, "points", qa_save_value_number(in->points)) &&
         set_own(obj, "protection", qa_save_value_number(in->protection));
  if (ok && in->kind == QA_REGULAR_ARMOR_Q2)
    ok = set_own(obj, "points", qa_save_value_number(in->points)) &&
         set_own(obj, "normalProtection", qa_save_value_number(in->normal_protection)) &&
         set_own(obj, "energyProtection", qa_save_value_number(in->energy_protection)) &&
         set_own(obj, "item", qa_save_value_string(in->item != NULL ? in->item : ""));
  if (!ok) { qa_save_value_free(obj); return NULL; }
  return obj;
}
static qa_save_value *build_armor_value(const qa_armor_state *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  qa_save_value *powered = qa_save_value_object();
  bool ok = powered != NULL &&
      set_own(powered, "kind", qa_save_value_string(in->powered.kind != NULL ? in->powered.kind : "none"));
  if (ok && in->powered.kind != NULL && strcmp(in->powered.kind, "none") != 0)
    ok = set_own(powered, "cells", qa_save_value_number(in->powered.cells));
  ok = ok && set_own(obj, "regular", build_regular_armor(&in->regular)) &&
       set_own(obj, "powered", powered);
  if (!ok) { qa_save_value_free(obj); return NULL; }
  return obj;
}
static qa_inventory_entry read_inventory_entry_value(const qa_save_reader *reader) {
  static const char *const policy_kinds[] = {"stack", "source-counter"};
  static const char *const arithmetics[] = {"binary32", "binary64", "int32"};
  qa_inventory_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.item = ns_field(reader, "item");
  entry.count = number_field(reader, "count");
  entry.capacity = number_field(reader, "capacity");
  qa_save_reader policy = field(reader, "countPolicy");
  if (!qa_save_reader_is_undefined(&policy)) {
    entry.has_count_policy = true;
    entry.count_policy.kind = choice_field(&policy, "kind", policy_kinds, 2);
    if (strcmp(entry.count_policy.kind, "source-counter") == 0)
      entry.count_policy.arithmetic = choice_field(&policy, "arithmetic", arithmetics, 3);
  }
  qa_save_reader_destroy(&policy);
  return entry;
}
static qa_save_value *build_inventory_entry_value(const qa_inventory_entry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  bool ok = set_own(obj, "item", qa_save_value_string(in->item != NULL ? in->item : "")) &&
            set_own(obj, "count", qa_save_value_number(in->count)) &&
            set_own(obj, "capacity", qa_save_value_number(in->capacity));
  if (ok && in->has_count_policy) {
    qa_save_value *policy = qa_save_value_object();
    ok = policy != NULL &&
        set_own(policy, "kind", qa_save_value_string(in->count_policy.kind != NULL ? in->count_policy.kind : "stack"));
    if (ok && in->count_policy.kind != NULL && strcmp(in->count_policy.kind, "source-counter") == 0)
      ok = set_own(policy, "arithmetic", qa_save_value_string(in->count_policy.arithmetic != NULL ? in->count_policy.arithmetic : "binary64"));
    ok = ok && set_own(obj, "countPolicy", policy);
  }
  if (!ok) { qa_save_value_free(obj); return NULL; }
  return obj;
}
static qa_q2_player_intermission_checkpoint read_intermission_value(const qa_save_reader *reader) {
  static const char *const kinds[] = {"playing", "intermission"};
  qa_q2_player_intermission_checkpoint out;
  memset(&out, 0, sizeof out);
  out.kind = choice_field(reader, "kind", kinds, 2);
  if (strcmp(out.kind, "playing") == 0) return out;
  out.map = string_field(reader, "map");
  out.started = number_field(reader, "started");
  out.exit = bool_field(reader, "exit");
  qa_save_reader landmark = field(reader, "landmark");
  if (!qa_save_reader_is_null(&landmark)) {
    out.has_landmark = true;
    qa_save_reader player = field(&landmark, "player");
    out.landmark.player = read_actor(&player);
    qa_save_reader_destroy(&player);
    out.landmark.name = string_field(&landmark, "name");
    qa_save_reader origin = field(&landmark, "relativeOrigin");
    out.landmark.relative_origin = qa_read_vector(&origin);
    qa_save_reader_destroy(&origin);
    qa_save_reader velocity = field(&landmark, "relativeVelocity");
    out.landmark.relative_velocity = qa_read_vector(&velocity);
    qa_save_reader_destroy(&velocity);
    qa_save_reader angles = field(&landmark, "relativeViewAngles");
    out.landmark.relative_view_angles = qa_read_vector(&angles);
    qa_save_reader_destroy(&angles);
  }
  qa_save_reader_destroy(&landmark);
  return out;
}
static qa_save_value *build_intermission_value(const qa_q2_player_intermission_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  bool ok = set_own(obj, "kind", qa_save_value_string(in->kind != NULL ? in->kind : "playing"));
  if (ok && in->kind != NULL && strcmp(in->kind, "intermission") == 0) {
    ok = set_own(obj, "map", qa_save_value_string(in->map != NULL ? in->map : "")) &&
         set_own(obj, "started", qa_save_value_number(in->started)) &&
         set_own(obj, "exit", qa_save_value_boolean(in->exit));
    if (ok && in->has_landmark) {
      qa_save_value *landmark = qa_save_value_object();
      ok = landmark != NULL &&
          set_own(landmark, "player", actor_value(&in->landmark.player)) &&
          set_own(landmark, "name", qa_save_value_string(in->landmark.name != NULL ? in->landmark.name : "")) &&
          set_own(landmark, "relativeOrigin", vec_value(&in->landmark.relative_origin)) &&
          set_own(landmark, "relativeVelocity", vec_value(&in->landmark.relative_velocity)) &&
          set_own(landmark, "relativeViewAngles", vec_value(&in->landmark.relative_view_angles)) &&
          set_own(obj, "landmark", landmark);
    } else if (ok) {
      ok = set_own(obj, "landmark", qa_save_value_null());
    }
  }
  if (!ok) { qa_save_value_free(obj); return NULL; }
  return obj;
}

static void read_q2_player_carry(const qa_save_reader *reader, qa_q2_player_carry *out);
static qa_save_value *build_q2_player_carry(const qa_q2_player_carry *in);
static void read_q2_player_rules(const qa_save_reader *reader, qa_q2_player_rules *out);
static qa_save_value *build_q2_player_rules(const qa_q2_player_rules *in);
static void read_q2_player_state_checkpoint(const qa_save_reader *reader, qa_q2_player_state_checkpoint *out);
static qa_save_value *build_q2_player_state_checkpoint(const qa_q2_player_state_checkpoint *in);
static void read_q2_player_entry(const qa_save_reader *reader, qa_q2_player_entry *out);
static qa_save_value *build_q2_player_entry(const qa_q2_player_entry *in);
static void read_q2_players_checkpoint(const qa_save_reader *reader, qa_q2_players_checkpoint *out);
static qa_save_value *build_q2_players_checkpoint(const qa_q2_players_checkpoint *in);
static void read_q2_character_entity(const qa_save_reader *reader, qa_q2_character_entity *out);
static qa_save_value *build_q2_character_entity(const qa_q2_character_entity *in);
static void read_q2_character_checkpoint(const qa_save_reader *reader, qa_q2_character_checkpoint *out);
static qa_save_value *build_q2_character_checkpoint(const qa_q2_character_checkpoint *in);

static void read_q2_player_carry(const qa_save_reader *reader, qa_q2_player_carry *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "health");
    (*out).health = qa_save_reader_number(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "maximumHealth");
    (*out).maximum_health = qa_save_reader_number(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "armor");
    (*out).armor = read_armor_value(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "inventory");
    (*out).inventory_count = qa_save_reader_list_count(&f3);
    (*out).inventory = (qa_inventory_entry *)calloc((*out).inventory_count != 0 ? (*out).inventory_count : 1, sizeof(qa_inventory_entry));
    if ((*out).inventory == NULL) qa_save_reader_fail(&f3, "out of memory");
    for (size_t i1 = 0; i1 < (*out).inventory_count; i1++) {
      qa_save_reader it2 = item(&f3, i1);
      (*out).inventory[i1] = read_inventory_entry_value(&it2);
      qa_save_reader_destroy(&it2);
    }
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "weapon");
    if (qa_save_reader_is_null(&f4)) (*out).weapon = NULL; else {
      (*out).weapon = qa_save_reader_string(&f4);
    }
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "selectedItem");
    if (qa_save_reader_is_null(&f5)) (*out).selected_item = NULL; else {
      (*out).selected_item = qa_namespaced(&f5);
    }
    qa_save_reader_destroy(&f5); }
  { qa_save_reader f6 = field(reader, "score");
    (*out).score = qa_save_reader_number(&f6);
    qa_save_reader_destroy(&f6); }
  { qa_save_reader f7 = field(reader, "flags");
    (*out).flags = qa_save_reader_number(&f7);
    qa_save_reader_destroy(&f7); }
  { qa_save_reader f8 = field(reader, "powerCubes");
    (*out).power_cubes = qa_save_reader_number(&f8);
    qa_save_reader_destroy(&f8); }
}
static qa_save_value *build_q2_player_carry(const qa_q2_player_carry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "health", qa_save_value_number((*in).health))) goto fail;
  if (!set_own(obj, "maximumHealth", qa_save_value_number((*in).maximum_health))) goto fail;
  if (!set_own(obj, "armor", build_armor_value(&(*in).armor))) goto fail;
  { qa_save_value *arr5 = qa_save_value_array();
    if (arr5 == NULL) goto fail;
    for (size_t i6 = 0; i6 < (*in).inventory_count; i6++) {
      if (!push_own(arr5, build_inventory_entry_value(&(*in).inventory[i6]))) { qa_save_value_free(arr5); goto fail; }
    }
    if (!set_own(obj, "inventory", arr5)) goto fail; }
  if (!set_own(obj, "weapon", (*in).weapon != NULL ? str_value((*in).weapon) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "selectedItem", (*in).selected_item != NULL ? str_value((*in).selected_item) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "score", qa_save_value_number((*in).score))) goto fail;
  if (!set_own(obj, "flags", qa_save_value_number((*in).flags))) goto fail;
  if (!set_own(obj, "powerCubes", qa_save_value_number((*in).power_cubes))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_player_rules(const qa_save_reader *reader, qa_q2_player_rules *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "password");
    (*out).password = qa_save_reader_string(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "spectatorPassword");
    (*out).spectator_password = qa_save_reader_string(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "maxSpectators");
    (*out).max_spectators = qa_save_reader_number(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "cheats");
    (*out).cheats = qa_save_reader_boolean(&f3);
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "timeLimitMinutes");
    (*out).time_limit_minutes = qa_save_reader_number(&f4);
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "fragLimit");
    (*out).frag_limit = qa_save_reader_number(&f5);
    qa_save_reader_destroy(&f5); }
  { qa_save_reader f6 = field(reader, "mapList");
    (*out).map_list_count = qa_save_reader_list_count(&f6);
    (*out).map_list = (const char * *)calloc((*out).map_list_count != 0 ? (*out).map_list_count : 1, sizeof(const char *));
    if ((*out).map_list == NULL) qa_save_reader_fail(&f6, "out of memory");
    for (size_t i7 = 0; i7 < (*out).map_list_count; i7++) {
      qa_save_reader it8 = item(&f6, i7);
      (*out).map_list[i7] = qa_save_reader_string(&it8);
      qa_save_reader_destroy(&it8);
    }
    qa_save_reader_destroy(&f6); }
  { qa_save_reader f7 = field(reader, "mapListShuffle");
    (*out).map_list_shuffle = qa_save_reader_is_undefined(&f7) ? false : qa_save_reader_boolean(&f7);
    qa_save_reader_destroy(&f7); }
  { qa_save_reader f8 = field(reader, "nextMap");
    (*out).next_map = qa_save_reader_string(&f8);
    qa_save_reader_destroy(&f8); }
  { qa_save_reader f9 = field(reader, "spawnPoint");
    (*out).spawn_point = qa_save_reader_string(&f9);
    qa_save_reader_destroy(&f9); }
  { qa_save_reader f10 = field(reader, "floodMessages");
    (*out).flood_messages = qa_save_reader_number(&f10);
    qa_save_reader_destroy(&f10); }
  { qa_save_reader f11 = field(reader, "floodSeconds");
    (*out).flood_seconds = qa_save_reader_number(&f11);
    qa_save_reader_destroy(&f11); }
  { qa_save_reader f12 = field(reader, "floodWaitSeconds");
    (*out).flood_wait_seconds = qa_save_reader_number(&f12);
    qa_save_reader_destroy(&f12); }
  { qa_save_reader f13 = field(reader, "rollSpeed");
    (*out).roll_speed = qa_save_reader_number(&f13);
    qa_save_reader_destroy(&f13); }
  { qa_save_reader f14 = field(reader, "rollAngle");
    (*out).roll_angle = qa_save_reader_number(&f14);
    qa_save_reader_destroy(&f14); }
  { qa_save_reader f15 = field(reader, "runPitch");
    (*out).run_pitch = qa_save_reader_number(&f15);
    qa_save_reader_destroy(&f15); }
  { qa_save_reader f16 = field(reader, "runRoll");
    (*out).run_roll = qa_save_reader_number(&f16);
    qa_save_reader_destroy(&f16); }
  { qa_save_reader f17 = field(reader, "bobUp");
    (*out).bob_up = qa_save_reader_number(&f17);
    qa_save_reader_destroy(&f17); }
  { qa_save_reader f18 = field(reader, "bobPitch");
    (*out).bob_pitch = qa_save_reader_number(&f18);
    qa_save_reader_destroy(&f18); }
  { qa_save_reader f19 = field(reader, "bobRoll");
    (*out).bob_roll = qa_save_reader_number(&f19);
    qa_save_reader_destroy(&f19); }
  { qa_save_reader f20 = field(reader, "gunOffset");
    (*out).gun_offset = qa_read_vector(&f20);
    qa_save_reader_destroy(&f20); }
}
static qa_save_value *build_q2_player_rules(const qa_q2_player_rules *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "password", str_value((*in).password))) goto fail;
  if (!set_own(obj, "spectatorPassword", str_value((*in).spectator_password))) goto fail;
  if (!set_own(obj, "maxSpectators", qa_save_value_number((*in).max_spectators))) goto fail;
  if (!set_own(obj, "cheats", qa_save_value_boolean((*in).cheats))) goto fail;
  if (!set_own(obj, "timeLimitMinutes", qa_save_value_number((*in).time_limit_minutes))) goto fail;
  if (!set_own(obj, "fragLimit", qa_save_value_number((*in).frag_limit))) goto fail;
  { qa_save_value *arr9 = qa_save_value_array();
    if (arr9 == NULL) goto fail;
    for (size_t i10 = 0; i10 < (*in).map_list_count; i10++) {
      if (!push_own(arr9, str_value((*in).map_list[i10]))) { qa_save_value_free(arr9); goto fail; }
    }
    if (!set_own(obj, "mapList", arr9)) goto fail; }
  if (!set_own(obj, "mapListShuffle", qa_save_value_boolean((*in).map_list_shuffle))) goto fail;
  if (!set_own(obj, "nextMap", str_value((*in).next_map))) goto fail;
  if (!set_own(obj, "spawnPoint", str_value((*in).spawn_point))) goto fail;
  if (!set_own(obj, "floodMessages", qa_save_value_number((*in).flood_messages))) goto fail;
  if (!set_own(obj, "floodSeconds", qa_save_value_number((*in).flood_seconds))) goto fail;
  if (!set_own(obj, "floodWaitSeconds", qa_save_value_number((*in).flood_wait_seconds))) goto fail;
  if (!set_own(obj, "rollSpeed", qa_save_value_number((*in).roll_speed))) goto fail;
  if (!set_own(obj, "rollAngle", qa_save_value_number((*in).roll_angle))) goto fail;
  if (!set_own(obj, "runPitch", qa_save_value_number((*in).run_pitch))) goto fail;
  if (!set_own(obj, "runRoll", qa_save_value_number((*in).run_roll))) goto fail;
  if (!set_own(obj, "bobUp", qa_save_value_number((*in).bob_up))) goto fail;
  if (!set_own(obj, "bobPitch", qa_save_value_number((*in).bob_pitch))) goto fail;
  if (!set_own(obj, "bobRoll", qa_save_value_number((*in).bob_roll))) goto fail;
  if (!set_own(obj, "gunOffset", vec_value(&(*in).gun_offset))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_player_state_checkpoint(const qa_save_reader *reader, qa_q2_player_state_checkpoint *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "slot");
    (*out).slot = qa_save_reader_integer_min(&f0, 0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "enteredAt");
    (*out).entered_at = qa_save_reader_number(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "useQ2Weapons");
    (*out).use_q2_weapons = qa_save_reader_boolean(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "useQ2Inventory");
    (*out).use_q2_inventory = qa_save_reader_boolean(&f3);
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "spawnInventory");
    (*out).spawn_inventory_count = qa_save_reader_list_count(&f4);
    (*out).spawn_inventory = (qa_inventory_entry *)calloc((*out).spawn_inventory_count != 0 ? (*out).spawn_inventory_count : 1, sizeof(qa_inventory_entry));
    if ((*out).spawn_inventory == NULL) qa_save_reader_fail(&f4, "out of memory");
    for (size_t i11 = 0; i11 < (*out).spawn_inventory_count; i11++) {
      qa_save_reader it12 = item(&f4, i11);
      (*out).spawn_inventory[i11] = read_inventory_entry_value(&it12);
      qa_save_reader_destroy(&it12);
    }
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "userinfo");
    (*out).userinfo = qa_save_reader_string(&f5);
    qa_save_reader_destroy(&f5); }
  { qa_save_reader f6 = field(reader, "name");
    (*out).name = qa_save_reader_string(&f6);
    qa_save_reader_destroy(&f6); }
  { qa_save_reader f7 = field(reader, "skin");
    (*out).skin = qa_save_reader_string(&f7);
    qa_save_reader_destroy(&f7); }
  { qa_save_reader f8 = field(reader, "gender");
    static const char *const ch13[] = {"male", "female", "neutral"};
    (*out).gender = qa_save_reader_choice_string(&f8, ch13, 3);
    qa_save_reader_destroy(&f8); }
  { qa_save_reader f9 = field(reader, "fov");
    (*out).fov = qa_save_reader_number(&f9);
    qa_save_reader_destroy(&f9); }
  { qa_save_reader f10 = field(reader, "hand");
    static const char *const ch14[] = {"right", "left", "center"};
    (*out).hand = qa_save_reader_choice_string(&f10, ch14, 3);
    qa_save_reader_destroy(&f10); }
  { qa_save_reader f11 = field(reader, "spectator");
    (*out).spectator = qa_save_reader_boolean(&f11);
    qa_save_reader_destroy(&f11); }
  { qa_save_reader f12 = field(reader, "requestedSpectator");
    (*out).requested_spectator = qa_save_reader_boolean(&f12);
    qa_save_reader_destroy(&f12); }
  { qa_save_reader f13 = field(reader, "connected");
    (*out).connected = qa_save_reader_boolean(&f13);
    qa_save_reader_destroy(&f13); }
  { qa_save_reader f14 = field(reader, "dead");
    (*out).dead = qa_save_reader_boolean(&f14);
    qa_save_reader_destroy(&f14); }
  { qa_save_reader f15 = field(reader, "gibbed");
    (*out).gibbed = qa_save_reader_boolean(&f15);
    qa_save_reader_destroy(&f15); }
  { qa_save_reader f16 = field(reader, "noclip");
    (*out).noclip = qa_save_reader_boolean(&f16);
    qa_save_reader_destroy(&f16); }
  { qa_save_reader f17 = field(reader, "god");
    (*out).god = qa_save_reader_boolean(&f17);
    qa_save_reader_destroy(&f17); }
  { qa_save_reader f18 = field(reader, "notarget");
    (*out).notarget = qa_save_reader_boolean(&f18);
    qa_save_reader_destroy(&f18); }
  { qa_save_reader f19 = field(reader, "score");
    (*out).score = qa_save_reader_number(&f19);
    qa_save_reader_destroy(&f19); }
  { qa_save_reader f20 = field(reader, "ping");
    (*out).ping = qa_save_reader_number(&f20);
    qa_save_reader_destroy(&f20); }
  { qa_save_reader f21 = field(reader, "respawnTime");
    (*out).respawn_time = qa_save_reader_number(&f21);
    qa_save_reader_destroy(&f21); }
  { qa_save_reader f22 = field(reader, "airFinished");
    (*out).air_finished = qa_save_reader_number(&f22);
    qa_save_reader_destroy(&f22); }
  { qa_save_reader f23 = field(reader, "nextDrownTime");
    (*out).next_drown_time = qa_save_reader_number(&f23);
    qa_save_reader_destroy(&f23); }
  { qa_save_reader f24 = field(reader, "drownDamage");
    (*out).drown_damage = qa_save_reader_number(&f24);
    qa_save_reader_destroy(&f24); }
  { qa_save_reader f25 = field(reader, "oldWaterLevel");
    (*out).old_water_level = qa_save_reader_number(&f25);
    qa_save_reader_destroy(&f25); }
  { qa_save_reader f26 = field(reader, "breatherSound");
    (*out).breather_sound = qa_save_reader_number(&f26);
    qa_save_reader_destroy(&f26); }
  { qa_save_reader f27 = field(reader, "painDebounce");
    (*out).pain_debounce = qa_save_reader_number(&f27);
    qa_save_reader_destroy(&f27); }
  { qa_save_reader f28 = field(reader, "damageBlood");
    (*out).damage_blood = qa_save_reader_number(&f28);
    qa_save_reader_destroy(&f28); }
  { qa_save_reader f29 = field(reader, "damageArmor");
    (*out).damage_armor = qa_save_reader_number(&f29);
    qa_save_reader_destroy(&f29); }
  { qa_save_reader f30 = field(reader, "damagePowerArmor");
    (*out).damage_power_armor = qa_save_reader_number(&f30);
    qa_save_reader_destroy(&f30); }
  { qa_save_reader f31 = field(reader, "damageKnockback");
    (*out).damage_knockback = qa_save_reader_number(&f31);
    qa_save_reader_destroy(&f31); }
  { qa_save_reader f32 = field(reader, "damageFrom");
    (*out).damage_from = qa_read_vector(&f32);
    qa_save_reader_destroy(&f32); }
  { qa_save_reader f33 = field(reader, "damageBlend");
    (*out).damage_blend = qa_read_vector(&f33);
    qa_save_reader_destroy(&f33); }
  { qa_save_reader f34 = field(reader, "damageAlpha");
    (*out).damage_alpha = qa_save_reader_number(&f34);
    qa_save_reader_destroy(&f34); }
  { qa_save_reader f35 = field(reader, "bonusAlpha");
    (*out).bonus_alpha = qa_save_reader_number(&f35);
    qa_save_reader_destroy(&f35); }
  { qa_save_reader f36 = field(reader, "damagePitch");
    (*out).damage_pitch = qa_save_reader_number(&f36);
    qa_save_reader_destroy(&f36); }
  { qa_save_reader f37 = field(reader, "damageRoll");
    (*out).damage_roll = qa_save_reader_number(&f37);
    qa_save_reader_destroy(&f37); }
  { qa_save_reader f38 = field(reader, "damageTime");
    (*out).damage_time = qa_save_reader_number(&f38);
    qa_save_reader_destroy(&f38); }
  { qa_save_reader f39 = field(reader, "powerArmorTime");
    (*out).power_armor_time = qa_save_reader_number(&f39);
    qa_save_reader_destroy(&f39); }
  { qa_save_reader f40 = field(reader, "fallTime");
    (*out).fall_time = qa_save_reader_number(&f40);
    qa_save_reader_destroy(&f40); }
  { qa_save_reader f41 = field(reader, "fallValue");
    (*out).fall_value = qa_save_reader_number(&f41);
    qa_save_reader_destroy(&f41); }
  { qa_save_reader f42 = field(reader, "landmarkFreeFall");
    (*out).landmark_free_fall = qa_save_reader_boolean(&f42);
    qa_save_reader_destroy(&f42); }
  { qa_save_reader f43 = field(reader, "landmarkNoiseTime");
    (*out).landmark_noise_time = qa_save_reader_number(&f43);
    qa_save_reader_destroy(&f43); }
  { qa_save_reader f44 = field(reader, "oldVelocity");
    (*out).old_velocity = qa_read_vector(&f44);
    qa_save_reader_destroy(&f44); }
  { qa_save_reader f45 = field(reader, "oldViewAngles");
    (*out).old_view_angles = qa_read_vector(&f45);
    qa_save_reader_destroy(&f45); }
  { qa_save_reader f46 = field(reader, "killerYaw");
    (*out).killer_yaw = qa_save_reader_number(&f46);
    qa_save_reader_destroy(&f46); }
  { qa_save_reader f47 = field(reader, "buttons");
    (*out).buttons = qa_save_reader_number(&f47);
    qa_save_reader_destroy(&f47); }
  { qa_save_reader f48 = field(reader, "latchedButtons");
    (*out).latched_buttons = qa_save_reader_number(&f48);
    qa_save_reader_destroy(&f48); }
  { qa_save_reader f49 = field(reader, "weaponThunk");
    (*out).weapon_thunk = qa_save_reader_boolean(&f49);
    qa_save_reader_destroy(&f49); }
  { qa_save_reader f50 = field(reader, "bobTime");
    (*out).bob_time = qa_save_reader_number(&f50);
    qa_save_reader_destroy(&f50); }
  { qa_save_reader f51 = field(reader, "bobMove");
    (*out).bob_move = qa_save_reader_number(&f51);
    qa_save_reader_destroy(&f51); }
  { qa_save_reader f52 = field(reader, "event");
    (*out).event = qa_save_reader_string(&f52);
    qa_save_reader_destroy(&f52); }
  { qa_save_reader f53 = field(reader, "animationPriority");
    (*out).animation_priority = qa_save_reader_number(&f53);
    qa_save_reader_destroy(&f53); }
  { qa_save_reader f54 = field(reader, "animationEnd");
    (*out).animation_end = qa_save_reader_number(&f54);
    qa_save_reader_destroy(&f54); }
  { qa_save_reader f55 = field(reader, "animationDuck");
    (*out).animation_duck = qa_save_reader_boolean(&f55);
    qa_save_reader_destroy(&f55); }
  { qa_save_reader f56 = field(reader, "animationRun");
    (*out).animation_run = qa_save_reader_boolean(&f56);
    qa_save_reader_destroy(&f56); }
  { qa_save_reader f57 = field(reader, "loopSound");
    (*out).loop_sound = qa_save_reader_string(&f57);
    qa_save_reader_destroy(&f57); }
  { qa_save_reader f58 = field(reader, "selectedItem");
    if (qa_save_reader_is_null(&f58)) (*out).selected_item = NULL; else {
      (*out).selected_item = qa_namespaced(&f58);
    }
    qa_save_reader_destroy(&f58); }
  { qa_save_reader f59 = field(reader, "showScores");
    (*out).show_scores = qa_save_reader_boolean(&f59);
    qa_save_reader_destroy(&f59); }
  { qa_save_reader f60 = field(reader, "showInventory");
    (*out).show_inventory = qa_save_reader_boolean(&f60);
    qa_save_reader_destroy(&f60); }
  { qa_save_reader f61 = field(reader, "showHelp");
    (*out).show_help = qa_save_reader_boolean(&f61);
    qa_save_reader_destroy(&f61); }
  { qa_save_reader f62 = field(reader, "chaseTarget");
    if (!qa_save_reader_is_null(&f62)) { (*out).has_chase_target = true;
      (*out).chase_target = read_actor(&f62);
    }
    qa_save_reader_destroy(&f62); }
  { qa_save_reader f63 = field(reader, "coopRespawn");
    if (!qa_save_reader_is_null(&f63)) { (*out).has_coop_respawn = true;
      read_q2_player_carry(&f63, &(*out).coop_respawn);
    }
    qa_save_reader_destroy(&f63); }
  { qa_save_reader f64 = field(reader, "floodTimes");
    (*out).flood_times_count = qa_save_reader_list_count(&f64);
    (*out).flood_times = (double *)calloc((*out).flood_times_count != 0 ? (*out).flood_times_count : 1, sizeof(double));
    if ((*out).flood_times == NULL) qa_save_reader_fail(&f64, "out of memory");
    for (size_t i16 = 0; i16 < (*out).flood_times_count; i16++) {
      qa_save_reader it17 = item(&f64, i16);
      (*out).flood_times[i16] = qa_save_reader_number(&it17);
      qa_save_reader_destroy(&it17);
    }
    qa_save_reader_destroy(&f64); }
  { qa_save_reader f65 = field(reader, "floodLockUntil");
    (*out).flood_lock_until = qa_save_reader_number(&f65);
    qa_save_reader_destroy(&f65); }
}
static qa_save_value *build_q2_player_state_checkpoint(const qa_q2_player_state_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "slot", qa_save_value_number((*in).slot))) goto fail;
  if (!set_own(obj, "enteredAt", qa_save_value_number((*in).entered_at))) goto fail;
  if (!set_own(obj, "useQ2Weapons", qa_save_value_boolean((*in).use_q2_weapons))) goto fail;
  if (!set_own(obj, "useQ2Inventory", qa_save_value_boolean((*in).use_q2_inventory))) goto fail;
  { qa_save_value *arr18 = qa_save_value_array();
    if (arr18 == NULL) goto fail;
    for (size_t i19 = 0; i19 < (*in).spawn_inventory_count; i19++) {
      if (!push_own(arr18, build_inventory_entry_value(&(*in).spawn_inventory[i19]))) { qa_save_value_free(arr18); goto fail; }
    }
    if (!set_own(obj, "spawnInventory", arr18)) goto fail; }
  if (!set_own(obj, "userinfo", str_value((*in).userinfo))) goto fail;
  if (!set_own(obj, "name", str_value((*in).name))) goto fail;
  if (!set_own(obj, "skin", str_value((*in).skin))) goto fail;
  if (!set_own(obj, "gender", str_value((*in).gender))) goto fail;
  if (!set_own(obj, "fov", qa_save_value_number((*in).fov))) goto fail;
  if (!set_own(obj, "hand", str_value((*in).hand))) goto fail;
  if (!set_own(obj, "spectator", qa_save_value_boolean((*in).spectator))) goto fail;
  if (!set_own(obj, "requestedSpectator", qa_save_value_boolean((*in).requested_spectator))) goto fail;
  if (!set_own(obj, "connected", qa_save_value_boolean((*in).connected))) goto fail;
  if (!set_own(obj, "dead", qa_save_value_boolean((*in).dead))) goto fail;
  if (!set_own(obj, "gibbed", qa_save_value_boolean((*in).gibbed))) goto fail;
  if (!set_own(obj, "noclip", qa_save_value_boolean((*in).noclip))) goto fail;
  if (!set_own(obj, "god", qa_save_value_boolean((*in).god))) goto fail;
  if (!set_own(obj, "notarget", qa_save_value_boolean((*in).notarget))) goto fail;
  if (!set_own(obj, "score", qa_save_value_number((*in).score))) goto fail;
  if (!set_own(obj, "ping", qa_save_value_number((*in).ping))) goto fail;
  if (!set_own(obj, "respawnTime", qa_save_value_number((*in).respawn_time))) goto fail;
  if (!set_own(obj, "airFinished", qa_save_value_number((*in).air_finished))) goto fail;
  if (!set_own(obj, "nextDrownTime", qa_save_value_number((*in).next_drown_time))) goto fail;
  if (!set_own(obj, "drownDamage", qa_save_value_number((*in).drown_damage))) goto fail;
  if (!set_own(obj, "oldWaterLevel", qa_save_value_number((*in).old_water_level))) goto fail;
  if (!set_own(obj, "breatherSound", qa_save_value_number((*in).breather_sound))) goto fail;
  if (!set_own(obj, "painDebounce", qa_save_value_number((*in).pain_debounce))) goto fail;
  if (!set_own(obj, "damageBlood", qa_save_value_number((*in).damage_blood))) goto fail;
  if (!set_own(obj, "damageArmor", qa_save_value_number((*in).damage_armor))) goto fail;
  if (!set_own(obj, "damagePowerArmor", qa_save_value_number((*in).damage_power_armor))) goto fail;
  if (!set_own(obj, "damageKnockback", qa_save_value_number((*in).damage_knockback))) goto fail;
  if (!set_own(obj, "damageFrom", vec_value(&(*in).damage_from))) goto fail;
  if (!set_own(obj, "damageBlend", vec_value(&(*in).damage_blend))) goto fail;
  if (!set_own(obj, "damageAlpha", qa_save_value_number((*in).damage_alpha))) goto fail;
  if (!set_own(obj, "bonusAlpha", qa_save_value_number((*in).bonus_alpha))) goto fail;
  if (!set_own(obj, "damagePitch", qa_save_value_number((*in).damage_pitch))) goto fail;
  if (!set_own(obj, "damageRoll", qa_save_value_number((*in).damage_roll))) goto fail;
  if (!set_own(obj, "damageTime", qa_save_value_number((*in).damage_time))) goto fail;
  if (!set_own(obj, "powerArmorTime", qa_save_value_number((*in).power_armor_time))) goto fail;
  if (!set_own(obj, "fallTime", qa_save_value_number((*in).fall_time))) goto fail;
  if (!set_own(obj, "fallValue", qa_save_value_number((*in).fall_value))) goto fail;
  if (!set_own(obj, "landmarkFreeFall", qa_save_value_boolean((*in).landmark_free_fall))) goto fail;
  if (!set_own(obj, "landmarkNoiseTime", qa_save_value_number((*in).landmark_noise_time))) goto fail;
  if (!set_own(obj, "oldVelocity", vec_value(&(*in).old_velocity))) goto fail;
  if (!set_own(obj, "oldViewAngles", vec_value(&(*in).old_view_angles))) goto fail;
  if (!set_own(obj, "killerYaw", qa_save_value_number((*in).killer_yaw))) goto fail;
  if (!set_own(obj, "buttons", qa_save_value_number((*in).buttons))) goto fail;
  if (!set_own(obj, "latchedButtons", qa_save_value_number((*in).latched_buttons))) goto fail;
  if (!set_own(obj, "weaponThunk", qa_save_value_boolean((*in).weapon_thunk))) goto fail;
  if (!set_own(obj, "bobTime", qa_save_value_number((*in).bob_time))) goto fail;
  if (!set_own(obj, "bobMove", qa_save_value_number((*in).bob_move))) goto fail;
  if (!set_own(obj, "event", str_value((*in).event))) goto fail;
  if (!set_own(obj, "animationPriority", qa_save_value_number((*in).animation_priority))) goto fail;
  if (!set_own(obj, "animationEnd", qa_save_value_number((*in).animation_end))) goto fail;
  if (!set_own(obj, "animationDuck", qa_save_value_boolean((*in).animation_duck))) goto fail;
  if (!set_own(obj, "animationRun", qa_save_value_boolean((*in).animation_run))) goto fail;
  if (!set_own(obj, "loopSound", str_value((*in).loop_sound))) goto fail;
  if (!set_own(obj, "selectedItem", (*in).selected_item != NULL ? str_value((*in).selected_item) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "showScores", qa_save_value_boolean((*in).show_scores))) goto fail;
  if (!set_own(obj, "showInventory", qa_save_value_boolean((*in).show_inventory))) goto fail;
  if (!set_own(obj, "showHelp", qa_save_value_boolean((*in).show_help))) goto fail;
  if (!set_own(obj, "chaseTarget", (*in).has_chase_target ? actor_value(&(*in).chase_target) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "coopRespawn", (*in).has_coop_respawn ? build_q2_player_carry(&(*in).coop_respawn) : qa_save_value_null())) goto fail;
  { qa_save_value *arr20 = qa_save_value_array();
    if (arr20 == NULL) goto fail;
    for (size_t i21 = 0; i21 < (*in).flood_times_count; i21++) {
      if (!push_own(arr20, qa_save_value_number((*in).flood_times[i21]))) { qa_save_value_free(arr20); goto fail; }
    }
    if (!set_own(obj, "floodTimes", arr20)) goto fail; }
  if (!set_own(obj, "floodLockUntil", qa_save_value_number((*in).flood_lock_until))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_player_entry(const qa_save_reader *reader, qa_q2_player_entry *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "state");
    read_q2_player_state_checkpoint(&f1, &(*out).state);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_player_entry(const qa_q2_player_entry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "state", build_q2_player_state_checkpoint(&(*in).state))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_players_checkpoint(const qa_save_reader *reader, qa_q2_players_checkpoint *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "version");
    (*out).version = qa_save_reader_literal_number(&f0, 1);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "corpseIndex");
    (*out).corpse_index = qa_save_reader_integer_min(&f1, 0);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "deathAnimation");
    (*out).death_animation = qa_save_reader_number(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "painAnimation");
    (*out).pain_animation = qa_save_reader_number(&f3);
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "rules");
    read_q2_player_rules(&f4, &(*out).rules);
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "intermission");
    (*out).intermission = read_intermission_value(&f5);
    qa_save_reader_destroy(&f5); }
  { qa_save_reader f6 = field(reader, "players");
    (*out).players_count = qa_save_reader_list_count(&f6);
    (*out).players = (qa_q2_player_entry *)calloc((*out).players_count != 0 ? (*out).players_count : 1, sizeof(qa_q2_player_entry));
    if ((*out).players == NULL) qa_save_reader_fail(&f6, "out of memory");
    for (size_t i22 = 0; i22 < (*out).players_count; i22++) {
      qa_save_reader it23 = item(&f6, i22);
      read_q2_player_entry(&it23, &(*out).players[i22]);
      qa_save_reader_destroy(&it23);
    }
    qa_save_reader_destroy(&f6); }
}
static qa_save_value *build_q2_players_checkpoint(const qa_q2_players_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "version", qa_save_value_number((*in).version))) goto fail;
  if (!set_own(obj, "corpseIndex", qa_save_value_number((*in).corpse_index))) goto fail;
  if (!set_own(obj, "deathAnimation", qa_save_value_number((*in).death_animation))) goto fail;
  if (!set_own(obj, "painAnimation", qa_save_value_number((*in).pain_animation))) goto fail;
  if (!set_own(obj, "rules", build_q2_player_rules(&(*in).rules))) goto fail;
  if (!set_own(obj, "intermission", build_intermission_value(&(*in).intermission))) goto fail;
  { qa_save_value *arr24 = qa_save_value_array();
    if (arr24 == NULL) goto fail;
    for (size_t i25 = 0; i25 < (*in).players_count; i25++) {
      if (!push_own(arr24, build_q2_player_entry(&(*in).players[i25]))) { qa_save_value_free(arr24); goto fail; }
    }
    if (!set_own(obj, "players", arr24)) goto fail; }
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_character_entity(const qa_save_reader *reader, qa_q2_character_entity *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "model");
    (*out).model = qa_save_reader_string(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "model2");
    (*out).model2 = qa_save_reader_string(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "model3");
    (*out).model3 = qa_save_reader_string(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "model4");
    (*out).model4 = qa_save_reader_string(&f3);
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "skin");
    (*out).skin = qa_save_reader_number(&f4);
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "frame");
    (*out).frame = qa_save_reader_number(&f5);
    qa_save_reader_destroy(&f5); }
  { qa_save_reader f6 = field(reader, "oldFrame");
    (*out).old_frame = qa_save_reader_number(&f6);
    qa_save_reader_destroy(&f6); }
  { qa_save_reader f7 = field(reader, "scale");
    (*out).scale = qa_save_reader_number(&f7);
    qa_save_reader_destroy(&f7); }
  { qa_save_reader f8 = field(reader, "effects");
    (*out).effects = qa_save_reader_number(&f8);
    qa_save_reader_destroy(&f8); }
  { qa_save_reader f9 = field(reader, "renderFlags");
    (*out).render_flags = qa_save_reader_number(&f9);
    qa_save_reader_destroy(&f9); }
  { qa_save_reader f10 = field(reader, "flags");
    (*out).flags = qa_save_reader_number(&f10);
    qa_save_reader_destroy(&f10); }
  { qa_save_reader f11 = field(reader, "serverFlags");
    (*out).server_flags = qa_save_reader_number(&f11);
    qa_save_reader_destroy(&f11); }
  { qa_save_reader f12 = field(reader, "viewHeight");
    (*out).view_height = qa_save_reader_number(&f12);
    qa_save_reader_destroy(&f12); }
  { qa_save_reader f13 = field(reader, "maxHealth");
    (*out).max_health = qa_save_reader_number(&f13);
    qa_save_reader_destroy(&f13); }
  { qa_save_reader f14 = field(reader, "sound");
    (*out).sound = qa_save_reader_string(&f14);
    qa_save_reader_destroy(&f14); }
  { qa_save_reader f15 = field(reader, "visible");
    (*out).visible = qa_save_reader_boolean(&f15);
    qa_save_reader_destroy(&f15); }
}
static qa_save_value *build_q2_character_entity(const qa_q2_character_entity *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "model", str_value((*in).model))) goto fail;
  if (!set_own(obj, "model2", str_value((*in).model2))) goto fail;
  if (!set_own(obj, "model3", str_value((*in).model3))) goto fail;
  if (!set_own(obj, "model4", str_value((*in).model4))) goto fail;
  if (!set_own(obj, "skin", qa_save_value_number((*in).skin))) goto fail;
  if (!set_own(obj, "frame", qa_save_value_number((*in).frame))) goto fail;
  if (!set_own(obj, "oldFrame", qa_save_value_number((*in).old_frame))) goto fail;
  if (!set_own(obj, "scale", qa_save_value_number((*in).scale))) goto fail;
  if (!set_own(obj, "effects", qa_save_value_number((*in).effects))) goto fail;
  if (!set_own(obj, "renderFlags", qa_save_value_number((*in).render_flags))) goto fail;
  if (!set_own(obj, "flags", qa_save_value_number((*in).flags))) goto fail;
  if (!set_own(obj, "serverFlags", qa_save_value_number((*in).server_flags))) goto fail;
  if (!set_own(obj, "viewHeight", qa_save_value_number((*in).view_height))) goto fail;
  if (!set_own(obj, "maxHealth", qa_save_value_number((*in).max_health))) goto fail;
  if (!set_own(obj, "sound", str_value((*in).sound))) goto fail;
  if (!set_own(obj, "visible", qa_save_value_boolean((*in).visible))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_character_checkpoint(const qa_save_reader *reader, qa_q2_character_checkpoint *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "version");
    (*out).version = qa_save_reader_literal_number(&f0, 1);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "painIndex");
    (*out).pain_index = qa_save_reader_number(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "deathIndex");
    (*out).death_index = qa_save_reader_number(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "state");
    read_q2_player_state_checkpoint(&f3, &(*out).state);
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "rules");
    read_q2_player_rules(&f4, &(*out).rules);
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "lastAttack");
    if (!qa_save_reader_is_null(&f5)) { (*out).has_last_attack = true;
      (*out).last_attack = qa_read_q2_attack_checkpoint(&f5);
    }
    qa_save_reader_destroy(&f5); }
  { qa_save_reader f6 = field(reader, "entity");
    read_q2_character_entity(&f6, &(*out).entity);
    qa_save_reader_destroy(&f6); }
}
static qa_save_value *build_q2_character_checkpoint(const qa_q2_character_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "version", qa_save_value_number((*in).version))) goto fail;
  if (!set_own(obj, "painIndex", qa_save_value_number((*in).pain_index))) goto fail;
  if (!set_own(obj, "deathIndex", qa_save_value_number((*in).death_index))) goto fail;
  if (!set_own(obj, "state", build_q2_player_state_checkpoint(&(*in).state))) goto fail;
  if (!set_own(obj, "rules", build_q2_player_rules(&(*in).rules))) goto fail;
  if (!set_own(obj, "lastAttack", (*in).has_last_attack ? qa_q2_attack_checkpoint_value(&(*in).last_attack) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "entity", build_q2_character_entity(&(*in).entity))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}



qa_q2_player_state_checkpoint qa_read_q2_player_state_checkpoint(const qa_save_reader *reader) {
  qa_q2_player_state_checkpoint out;
  read_q2_player_state_checkpoint(reader, &out);
  return out;
}


qa_q2_players_checkpoint qa_read_q2_players_checkpoint(const qa_save_reader *reader) {
  qa_q2_players_checkpoint out;
  read_q2_players_checkpoint(reader, &out);
  return out;
}
uint8_t *qa_encode_q2_players_checkpoint(const qa_q2_players_checkpoint *checkpoint, size_t *out_length) {
  qa_save_value *value = build_q2_players_checkpoint(checkpoint);
  if (value == NULL) return NULL;
  uint8_t *bytes = qa_encode_checkpoint_value(value, out_length);
  qa_save_value_free(value);
  return bytes;
}
bool qa_decode_q2_players_checkpoint(const uint8_t *bytes, size_t length, qa_q2_players_checkpoint *out) {
  qa_save_value *root = qa_decode_checkpoint_value(bytes, length);
  if (root == NULL) return false;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, root, "q2-players")) { qa_save_value_free(root); return false; }
  read_q2_players_checkpoint(&reader, out);
  qa_save_reader_destroy(&reader);
  out->root = root;
  return true;
}


qa_q2_character_checkpoint qa_read_q2_character_checkpoint(const qa_save_reader *reader) {
  qa_q2_character_checkpoint out;
  read_q2_character_checkpoint(reader, &out);
  return out;
}
uint8_t *qa_encode_q2_character_checkpoint(const qa_q2_character_checkpoint *checkpoint, size_t *out_length) {
  qa_save_value *value = build_q2_character_checkpoint(checkpoint);
  if (value == NULL) return NULL;
  uint8_t *bytes = qa_encode_checkpoint_value(value, out_length);
  qa_save_value_free(value);
  return bytes;
}
bool qa_decode_q2_character_checkpoint(const uint8_t *bytes, size_t length, qa_q2_character_checkpoint *out) {
  qa_save_value *root = qa_decode_checkpoint_value(bytes, length);
  if (root == NULL) return false;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, root, "q2-character")) { qa_save_value_free(root); return false; }
  read_q2_character_checkpoint(&reader, out);
  qa_save_reader_destroy(&reader);
  out->root = root;
  return true;
}

