/* Port of src/persistence/q2-missionpacks.ts. GPL-2.0-or-later. */
#include "qa/persistence/q2_missionpacks.h"

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

static void read_q2_mission_pack_powerups(const qa_save_reader *reader, qa_q2_mission_pack_powerups *out);
static qa_save_value *build_q2_mission_pack_powerups(const qa_q2_mission_pack_powerups *in);
static void read_q2_mission_pack_power_entry(const qa_save_reader *reader, qa_q2_mission_pack_power_entry *out);
static qa_save_value *build_q2_mission_pack_power_entry(const qa_q2_mission_pack_power_entry *in);
static void read_q2_mission_pack_items_checkpoint(const qa_save_reader *reader, qa_q2_mission_pack_items_checkpoint *out);
static qa_save_value *build_q2_mission_pack_items_checkpoint(const qa_q2_mission_pack_items_checkpoint *in);
static void read_q2_rogue_hint_node(const qa_save_reader *reader, qa_q2_rogue_hint_node *out);
static qa_save_value *build_q2_rogue_hint_node(const qa_q2_rogue_hint_node *in);
static void read_q2_rogue_hint_monster(const qa_save_reader *reader, qa_q2_rogue_hint_monster *out);
static qa_save_value *build_q2_rogue_hint_monster(const qa_q2_rogue_hint_monster *in);
static void read_q2_rogue_hints_checkpoint(const qa_save_reader *reader, qa_q2_rogue_hints_checkpoint *out);
static qa_save_value *build_q2_rogue_hints_checkpoint(const qa_q2_rogue_hints_checkpoint *in);
static void read_q2_mission_pack_monster_state(const qa_save_reader *reader, qa_q2_mission_pack_monster_state *out);
static qa_save_value *build_q2_mission_pack_monster_state(const qa_q2_mission_pack_monster_state *in);
static void read_q2_mission_pack_monster_entry(const qa_save_reader *reader, qa_q2_mission_pack_monster_entry *out);
static qa_save_value *build_q2_mission_pack_monster_entry(const qa_q2_mission_pack_monster_entry *in);
static void read_q2_mission_pack_monsters_checkpoint(const qa_save_reader *reader, qa_q2_mission_pack_monsters_checkpoint *out);
static qa_save_value *build_q2_mission_pack_monsters_checkpoint(const qa_q2_mission_pack_monsters_checkpoint *in);
static void read_q2_rogue_entities_checkpoint(const qa_save_reader *reader, qa_q2_rogue_entities_checkpoint *out);
static qa_save_value *build_q2_rogue_entities_checkpoint(const qa_q2_rogue_entities_checkpoint *in);
static void read_q2_tag_checkpoint(const qa_save_reader *reader, qa_q2_tag_checkpoint *out);
static qa_save_value *build_q2_tag_checkpoint(const qa_q2_tag_checkpoint *in);
static void read_q2_death_ball_checkpoint(const qa_save_reader *reader, qa_q2_death_ball_checkpoint *out);
static qa_save_value *build_q2_death_ball_checkpoint(const qa_q2_death_ball_checkpoint *in);

static void read_q2_mission_pack_powerups(const qa_save_reader *reader, qa_q2_mission_pack_powerups *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "quadFireUntil");
    (*out).quad_fire_until = qa_save_reader_number(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "doubleUntil");
    (*out).double_until = qa_save_reader_number(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "irUntil");
    (*out).ir_until = qa_save_reader_number(&f2);
    qa_save_reader_destroy(&f2); }
}
static qa_save_value *build_q2_mission_pack_powerups(const qa_q2_mission_pack_powerups *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "quadFireUntil", qa_save_value_number((*in).quad_fire_until))) goto fail;
  if (!set_own(obj, "doubleUntil", qa_save_value_number((*in).double_until))) goto fail;
  if (!set_own(obj, "irUntil", qa_save_value_number((*in).ir_until))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_mission_pack_power_entry(const qa_save_reader *reader, qa_q2_mission_pack_power_entry *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "state");
    read_q2_mission_pack_powerups(&f1, &(*out).state);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_mission_pack_power_entry(const qa_q2_mission_pack_power_entry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "state", build_q2_mission_pack_powerups(&(*in).state))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_mission_pack_items_checkpoint(const qa_save_reader *reader, qa_q2_mission_pack_items_checkpoint *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "powers");
    (*out).powers_count = qa_save_reader_list_count(&f0);
    (*out).powers = (qa_q2_mission_pack_power_entry *)calloc((*out).powers_count != 0 ? (*out).powers_count : 1, sizeof(qa_q2_mission_pack_power_entry));
    if ((*out).powers == NULL) qa_save_reader_fail(&f0, "out of memory");
    for (size_t i1 = 0; i1 < (*out).powers_count; i1++) {
      qa_save_reader it2 = item(&f0, i1);
      read_q2_mission_pack_power_entry(&it2, &(*out).powers[i1]);
      qa_save_reader_destroy(&it2);
    }
    qa_save_reader_destroy(&f0); }
}
static qa_save_value *build_q2_mission_pack_items_checkpoint(const qa_q2_mission_pack_items_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  { qa_save_value *arr3 = qa_save_value_array();
    if (arr3 == NULL) goto fail;
    for (size_t i4 = 0; i4 < (*in).powers_count; i4++) {
      if (!push_own(arr3, build_q2_mission_pack_power_entry(&(*in).powers[i4]))) { qa_save_value_free(arr3); goto fail; }
    }
    if (!set_own(obj, "powers", arr3)) goto fail; }
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_rogue_hint_node(const qa_save_reader *reader, qa_q2_rogue_hint_node *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "chain");
    (*out).chain = qa_save_reader_integer_min(&f1, -1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "next");
    if (!qa_save_reader_is_null(&f2)) { (*out).has_next = true;
      (*out).next = read_actor(&f2);
    }
    qa_save_reader_destroy(&f2); }
}
static qa_save_value *build_q2_rogue_hint_node(const qa_q2_rogue_hint_node *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "chain", qa_save_value_number((*in).chain))) goto fail;
  if (!set_own(obj, "next", (*in).has_next ? actor_value(&(*in).next) : qa_save_value_null())) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_rogue_hint_monster(const qa_save_reader *reader, qa_q2_rogue_hint_monster *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "goal");
    if (!qa_save_reader_is_null(&f1)) { (*out).has_goal = true;
      (*out).goal = read_actor(&f1);
    }
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "lastTime");
    (*out).last_time = qa_save_reader_finite(&f2);
    qa_save_reader_destroy(&f2); }
}
static qa_save_value *build_q2_rogue_hint_monster(const qa_q2_rogue_hint_monster *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "goal", (*in).has_goal ? actor_value(&(*in).goal) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "lastTime", qa_save_value_number((*in).last_time))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_rogue_hints_checkpoint(const qa_save_reader *reader, qa_q2_rogue_hints_checkpoint *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "version");
    (*out).version = qa_save_reader_literal_number(&f0, 1);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "present");
    (*out).present = qa_save_reader_boolean(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "starts");
    (*out).starts_count = qa_save_reader_list_count(&f2);
    (*out).starts = (qa_saved_actor_id *)calloc((*out).starts_count != 0 ? (*out).starts_count : 1, sizeof(qa_saved_actor_id));
    if ((*out).starts == NULL) qa_save_reader_fail(&f2, "out of memory");
    for (size_t i5 = 0; i5 < (*out).starts_count; i5++) {
      qa_save_reader it6 = item(&f2, i5);
      (*out).starts[i5] = read_actor(&it6);
      qa_save_reader_destroy(&it6);
    }
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "nodes");
    (*out).nodes_count = qa_save_reader_list_count(&f3);
    (*out).nodes = (qa_q2_rogue_hint_node *)calloc((*out).nodes_count != 0 ? (*out).nodes_count : 1, sizeof(qa_q2_rogue_hint_node));
    if ((*out).nodes == NULL) qa_save_reader_fail(&f3, "out of memory");
    for (size_t i7 = 0; i7 < (*out).nodes_count; i7++) {
      qa_save_reader it8 = item(&f3, i7);
      read_q2_rogue_hint_node(&it8, &(*out).nodes[i7]);
      qa_save_reader_destroy(&it8);
    }
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "monsters");
    (*out).monsters_count = qa_save_reader_list_count(&f4);
    (*out).monsters = (qa_q2_rogue_hint_monster *)calloc((*out).monsters_count != 0 ? (*out).monsters_count : 1, sizeof(qa_q2_rogue_hint_monster));
    if ((*out).monsters == NULL) qa_save_reader_fail(&f4, "out of memory");
    for (size_t i9 = 0; i9 < (*out).monsters_count; i9++) {
      qa_save_reader it10 = item(&f4, i9);
      read_q2_rogue_hint_monster(&it10, &(*out).monsters[i9]);
      qa_save_reader_destroy(&it10);
    }
    qa_save_reader_destroy(&f4); }
  if (out->starts_count > 100) qa_save_reader_fail(reader, "invalid hint chain index");
  for (size_t i = 0; i < out->nodes_count; i++) if (out->nodes[i].chain >= (double)out->starts_count) qa_save_reader_fail(reader, "invalid hint chain index");
}
static qa_save_value *build_q2_rogue_hints_checkpoint(const qa_q2_rogue_hints_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "version", qa_save_value_number((*in).version))) goto fail;
  if (!set_own(obj, "present", qa_save_value_boolean((*in).present))) goto fail;
  { qa_save_value *arr11 = qa_save_value_array();
    if (arr11 == NULL) goto fail;
    for (size_t i12 = 0; i12 < (*in).starts_count; i12++) {
      if (!push_own(arr11, actor_value(&(*in).starts[i12]))) { qa_save_value_free(arr11); goto fail; }
    }
    if (!set_own(obj, "starts", arr11)) goto fail; }
  { qa_save_value *arr13 = qa_save_value_array();
    if (arr13 == NULL) goto fail;
    for (size_t i14 = 0; i14 < (*in).nodes_count; i14++) {
      if (!push_own(arr13, build_q2_rogue_hint_node(&(*in).nodes[i14]))) { qa_save_value_free(arr13); goto fail; }
    }
    if (!set_own(obj, "nodes", arr13)) goto fail; }
  { qa_save_value *arr15 = qa_save_value_array();
    if (arr15 == NULL) goto fail;
    for (size_t i16 = 0; i16 < (*in).monsters_count; i16++) {
      if (!push_own(arr15, build_q2_rogue_hint_monster(&(*in).monsters[i16]))) { qa_save_value_free(arr15); goto fail; }
    }
    if (!set_own(obj, "monsters", arr15)) goto fail; }
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_mission_pack_monster_state(const qa_save_reader *reader, qa_q2_mission_pack_monster_state *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "blocked");
    (*out).blocked = qa_save_reader_boolean(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "turretOrientation");
    (*out).turret_orientation = qa_save_reader_number(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "healer");
    if (!qa_save_reader_is_null(&f2)) { (*out).has_healer = true;
      (*out).healer = read_actor(&f2);
    }
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "badMedic1");
    if (!qa_save_reader_is_null(&f3)) { (*out).has_bad_medic1 = true;
      (*out).bad_medic1 = read_actor(&f3);
    }
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "badMedic2");
    if (!qa_save_reader_is_null(&f4)) { (*out).has_bad_medic2 = true;
      (*out).bad_medic2 = read_actor(&f4);
    }
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "medicTries");
    (*out).medic_tries = qa_save_reader_number(&f5);
    qa_save_reader_destroy(&f5); }
  { qa_save_reader f6 = field(reader, "chosenReinforcements");
    (*out).chosen_reinforcements_count = qa_save_reader_list_count(&f6);
    (*out).chosen_reinforcements = (double *)calloc((*out).chosen_reinforcements_count != 0 ? (*out).chosen_reinforcements_count : 1, sizeof(double));
    if ((*out).chosen_reinforcements == NULL) qa_save_reader_fail(&f6, "out of memory");
    for (size_t i17 = 0; i17 < (*out).chosen_reinforcements_count; i17++) {
      qa_save_reader it18 = item(&f6, i17);
      double v19 = qa_save_reader_integer_min(&it18, 0);
      if (v19 >= 255) qa_save_reader_fail(&it18, "expected a reinforcement index below 255");
      (*out).chosen_reinforcements[i17] = v19;
      qa_save_reader_destroy(&it18);
    }
    qa_save_reader_destroy(&f6); }
  { qa_save_reader f7 = field(reader, "reactToDamageTime");
    (*out).react_to_damage_time = qa_save_reader_finite(&f7);
    qa_save_reader_destroy(&f7); }
  { qa_save_reader f8 = field(reader, "summonStrength");
    (*out).summon_strength = qa_save_reader_number(&f8);
    qa_save_reader_destroy(&f8); }
  { qa_save_reader f9 = field(reader, "lastPlayerEnemy");
    if (!qa_save_reader_is_null(&f9)) { (*out).has_last_player_enemy = true;
      (*out).last_player_enemy = read_actor(&f9);
    }
    qa_save_reader_destroy(&f9); }
  { qa_save_reader f10 = field(reader, "badArea");
    if (!qa_save_reader_is_null(&f10)) { (*out).has_bad_area = true;
      (*out).bad_area = read_actor(&f10);
    }
    qa_save_reader_destroy(&f10); }
  { qa_save_reader f11 = field(reader, "goodGuy");
    (*out).good_guy = qa_save_reader_boolean(&f11);
    qa_save_reader_destroy(&f11); }
  { qa_save_reader f12 = field(reader, "widowQuadUntil");
    (*out).widow_quad_until = qa_save_reader_number(&f12);
    qa_save_reader_destroy(&f12); }
  { qa_save_reader f13 = field(reader, "widowDoubleUntil");
    (*out).widow_double_until = qa_save_reader_number(&f13);
    qa_save_reader_destroy(&f13); }
  { qa_save_reader f14 = field(reader, "widowInvulnerableUntil");
    (*out).widow_invulnerable_until = qa_save_reader_number(&f14);
    qa_save_reader_destroy(&f14); }
}
static qa_save_value *build_q2_mission_pack_monster_state(const qa_q2_mission_pack_monster_state *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "blocked", qa_save_value_boolean((*in).blocked))) goto fail;
  if (!set_own(obj, "turretOrientation", qa_save_value_number((*in).turret_orientation))) goto fail;
  if (!set_own(obj, "healer", (*in).has_healer ? actor_value(&(*in).healer) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "badMedic1", (*in).has_bad_medic1 ? actor_value(&(*in).bad_medic1) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "badMedic2", (*in).has_bad_medic2 ? actor_value(&(*in).bad_medic2) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "medicTries", qa_save_value_number((*in).medic_tries))) goto fail;
  { qa_save_value *arr20 = qa_save_value_array();
    if (arr20 == NULL) goto fail;
    for (size_t i21 = 0; i21 < (*in).chosen_reinforcements_count; i21++) {
      if (!push_own(arr20, qa_save_value_number((*in).chosen_reinforcements[i21]))) { qa_save_value_free(arr20); goto fail; }
    }
    if (!set_own(obj, "chosenReinforcements", arr20)) goto fail; }
  if (!set_own(obj, "reactToDamageTime", qa_save_value_number((*in).react_to_damage_time))) goto fail;
  if (!set_own(obj, "summonStrength", qa_save_value_number((*in).summon_strength))) goto fail;
  if (!set_own(obj, "lastPlayerEnemy", (*in).has_last_player_enemy ? actor_value(&(*in).last_player_enemy) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "badArea", (*in).has_bad_area ? actor_value(&(*in).bad_area) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "goodGuy", qa_save_value_boolean((*in).good_guy))) goto fail;
  if (!set_own(obj, "widowQuadUntil", qa_save_value_number((*in).widow_quad_until))) goto fail;
  if (!set_own(obj, "widowDoubleUntil", qa_save_value_number((*in).widow_double_until))) goto fail;
  if (!set_own(obj, "widowInvulnerableUntil", qa_save_value_number((*in).widow_invulnerable_until))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_mission_pack_monster_entry(const qa_save_reader *reader, qa_q2_mission_pack_monster_entry *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "state");
    read_q2_mission_pack_monster_state(&f1, &(*out).state);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_mission_pack_monster_entry(const qa_q2_mission_pack_monster_entry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "state", build_q2_mission_pack_monster_state(&(*in).state))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_mission_pack_monsters_checkpoint(const qa_save_reader *reader, qa_q2_mission_pack_monsters_checkpoint *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "version");
    (*out).version = qa_save_reader_literal_number(&f0, 1);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "flyerNextMove");
    static const char *const ch22[] = {"none", "run"};
    (*out).flyer_next_move = qa_save_reader_choice_string(&f1, ch22, 2);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "hints");
    if (!qa_save_reader_is_null(&f2)) { (*out).has_hints = true;
      read_q2_rogue_hints_checkpoint(&f2, &(*out).hints);
    }
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "widowShotsFired");
    (*out).widow_shots_fired = qa_save_reader_number(&f3);
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "widowDamageMultiplier");
    static const double ch23[] = {1, 2, 4};
    (*out).widow_damage_multiplier = qa_save_reader_choice_number(&f4, ch23, 3);
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "actors");
    (*out).actors_count = qa_save_reader_list_count(&f5);
    (*out).actors = (qa_q2_mission_pack_monster_entry *)calloc((*out).actors_count != 0 ? (*out).actors_count : 1, sizeof(qa_q2_mission_pack_monster_entry));
    if ((*out).actors == NULL) qa_save_reader_fail(&f5, "out of memory");
    for (size_t i24 = 0; i24 < (*out).actors_count; i24++) {
      qa_save_reader it25 = item(&f5, i24);
      read_q2_mission_pack_monster_entry(&it25, &(*out).actors[i24]);
      qa_save_reader_destroy(&it25);
    }
    qa_save_reader_destroy(&f5); }
}
static qa_save_value *build_q2_mission_pack_monsters_checkpoint(const qa_q2_mission_pack_monsters_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "version", qa_save_value_number((*in).version))) goto fail;
  if (!set_own(obj, "flyerNextMove", str_value((*in).flyer_next_move))) goto fail;
  if (!set_own(obj, "hints", (*in).has_hints ? build_q2_rogue_hints_checkpoint(&(*in).hints) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "widowShotsFired", qa_save_value_number((*in).widow_shots_fired))) goto fail;
  if (!set_own(obj, "widowDamageMultiplier", qa_save_value_number((*in).widow_damage_multiplier))) goto fail;
  { qa_save_value *arr26 = qa_save_value_array();
    if (arr26 == NULL) goto fail;
    for (size_t i27 = 0; i27 < (*in).actors_count; i27++) {
      if (!push_own(arr26, build_q2_mission_pack_monster_entry(&(*in).actors[i27]))) { qa_save_value_free(arr26); goto fail; }
    }
    if (!set_own(obj, "actors", arr26)) goto fail; }
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_rogue_entities_checkpoint(const qa_save_reader *reader, qa_q2_rogue_entities_checkpoint *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "steamId");
    (*out).steam_id = qa_save_reader_number(&f0);
    qa_save_reader_destroy(&f0); }
}
static qa_save_value *build_q2_rogue_entities_checkpoint(const qa_q2_rogue_entities_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "steamId", qa_save_value_number((*in).steam_id))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_tag_checkpoint(const qa_save_reader *reader, qa_q2_tag_checkpoint *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "token");
    if (!qa_save_reader_is_null(&f0)) { (*out).has_token = true;
      (*out).token = read_actor(&f0);
    }
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "owner");
    if (!qa_save_reader_is_null(&f1)) { (*out).has_owner = true;
      (*out).owner = read_actor(&f1);
    }
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "count");
    (*out).count = qa_save_reader_number(&f2);
    qa_save_reader_destroy(&f2); }
}
static qa_save_value *build_q2_tag_checkpoint(const qa_q2_tag_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "token", (*in).has_token ? actor_value(&(*in).token) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "owner", (*in).has_owner ? actor_value(&(*in).owner) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "count", qa_save_value_number((*in).count))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_death_ball_checkpoint(const qa_save_reader *reader, qa_q2_death_ball_checkpoint *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "ball");
    if (!qa_save_reader_is_null(&f0)) { (*out).has_ball = true;
      (*out).ball = read_actor(&f0);
    }
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "starts");
    (*out).starts = qa_save_reader_number(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "team1Score");
    (*out).team1_score = qa_save_reader_number(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "team2Score");
    (*out).team2_score = qa_save_reader_number(&f3);
    qa_save_reader_destroy(&f3); }
}
static qa_save_value *build_q2_death_ball_checkpoint(const qa_q2_death_ball_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "ball", (*in).has_ball ? actor_value(&(*in).ball) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "starts", qa_save_value_number((*in).starts))) goto fail;
  if (!set_own(obj, "team1Score", qa_save_value_number((*in).team1_score))) goto fail;
  if (!set_own(obj, "team2Score", qa_save_value_number((*in).team2_score))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}



qa_q2_mission_pack_items_checkpoint qa_read_q2_mission_pack_items_checkpoint(const qa_save_reader *reader) {
  qa_q2_mission_pack_items_checkpoint out;
  read_q2_mission_pack_items_checkpoint(reader, &out);
  return out;
}
uint8_t *qa_encode_q2_mission_pack_items_checkpoint(const qa_q2_mission_pack_items_checkpoint *checkpoint, size_t *out_length) {
  qa_save_value *value = build_q2_mission_pack_items_checkpoint(checkpoint);
  if (value == NULL) return NULL;
  uint8_t *bytes = qa_encode_checkpoint_value(value, out_length);
  qa_save_value_free(value);
  return bytes;
}
bool qa_decode_q2_mission_pack_items_checkpoint(const uint8_t *bytes, size_t length, qa_q2_mission_pack_items_checkpoint *out) {
  qa_save_value *root = qa_decode_checkpoint_value(bytes, length);
  if (root == NULL) return false;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, root, "q2-missionpack-items")) { qa_save_value_free(root); return false; }
  read_q2_mission_pack_items_checkpoint(&reader, out);
  qa_save_reader_destroy(&reader);
  out->root = root;
  return true;
}



qa_q2_rogue_hints_checkpoint qa_read_q2_rogue_hints_checkpoint(const qa_save_reader *reader) {
  qa_q2_rogue_hints_checkpoint out;
  read_q2_rogue_hints_checkpoint(reader, &out);
  return out;
}
qa_save_value *qa_q2_rogue_hints_checkpoint_value(const qa_q2_rogue_hints_checkpoint *checkpoint) {
  return build_q2_rogue_hints_checkpoint(checkpoint);
}



qa_q2_mission_pack_monsters_checkpoint qa_read_q2_mission_pack_monsters_checkpoint(const qa_save_reader *reader) {
  qa_q2_mission_pack_monsters_checkpoint out;
  read_q2_mission_pack_monsters_checkpoint(reader, &out);
  return out;
}
uint8_t *qa_encode_q2_mission_pack_monsters_checkpoint(const qa_q2_mission_pack_monsters_checkpoint *checkpoint, size_t *out_length) {
  qa_save_value *value = build_q2_mission_pack_monsters_checkpoint(checkpoint);
  if (value == NULL) return NULL;
  uint8_t *bytes = qa_encode_checkpoint_value(value, out_length);
  qa_save_value_free(value);
  return bytes;
}
bool qa_decode_q2_mission_pack_monsters_checkpoint(const uint8_t *bytes, size_t length, qa_q2_mission_pack_monsters_checkpoint *out) {
  qa_save_value *root = qa_decode_checkpoint_value(bytes, length);
  if (root == NULL) return false;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, root, "q2-missionpack-monsters")) { qa_save_value_free(root); return false; }
  read_q2_mission_pack_monsters_checkpoint(&reader, out);
  qa_save_reader_destroy(&reader);
  out->root = root;
  return true;
}

qa_q2_rogue_entities_checkpoint qa_read_q2_rogue_entities_checkpoint(const qa_save_reader *reader) {
  qa_q2_rogue_entities_checkpoint out;
  read_q2_rogue_entities_checkpoint(reader, &out);
  return out;
}
uint8_t *qa_encode_q2_rogue_entities_checkpoint(const qa_q2_rogue_entities_checkpoint *checkpoint, size_t *out_length) {
  qa_save_value *value = build_q2_rogue_entities_checkpoint(checkpoint);
  if (value == NULL) return NULL;
  uint8_t *bytes = qa_encode_checkpoint_value(value, out_length);
  qa_save_value_free(value);
  return bytes;
}
bool qa_decode_q2_rogue_entities_checkpoint(const uint8_t *bytes, size_t length, qa_q2_rogue_entities_checkpoint *out) {
  qa_save_value *root = qa_decode_checkpoint_value(bytes, length);
  if (root == NULL) return false;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, root, "q2-rogue-entities")) { qa_save_value_free(root); return false; }
  read_q2_rogue_entities_checkpoint(&reader, out);
  qa_save_reader_destroy(&reader);
  out->root = root;
  return true;
}

qa_q2_tag_checkpoint qa_read_q2_tag_checkpoint(const qa_save_reader *reader) {
  qa_q2_tag_checkpoint out;
  read_q2_tag_checkpoint(reader, &out);
  return out;
}
uint8_t *qa_encode_q2_tag_checkpoint(const qa_q2_tag_checkpoint *checkpoint, size_t *out_length) {
  qa_save_value *value = build_q2_tag_checkpoint(checkpoint);
  if (value == NULL) return NULL;
  uint8_t *bytes = qa_encode_checkpoint_value(value, out_length);
  qa_save_value_free(value);
  return bytes;
}
bool qa_decode_q2_tag_checkpoint(const uint8_t *bytes, size_t length, qa_q2_tag_checkpoint *out) {
  qa_save_value *root = qa_decode_checkpoint_value(bytes, length);
  if (root == NULL) return false;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, root, "q2-tag")) { qa_save_value_free(root); return false; }
  read_q2_tag_checkpoint(&reader, out);
  qa_save_reader_destroy(&reader);
  out->root = root;
  return true;
}

qa_q2_death_ball_checkpoint qa_read_q2_death_ball_checkpoint(const qa_save_reader *reader) {
  qa_q2_death_ball_checkpoint out;
  read_q2_death_ball_checkpoint(reader, &out);
  return out;
}
uint8_t *qa_encode_q2_death_ball_checkpoint(const qa_q2_death_ball_checkpoint *checkpoint, size_t *out_length) {
  qa_save_value *value = build_q2_death_ball_checkpoint(checkpoint);
  if (value == NULL) return NULL;
  uint8_t *bytes = qa_encode_checkpoint_value(value, out_length);
  qa_save_value_free(value);
  return bytes;
}
bool qa_decode_q2_death_ball_checkpoint(const uint8_t *bytes, size_t length, qa_q2_death_ball_checkpoint *out) {
  qa_save_value *root = qa_decode_checkpoint_value(bytes, length);
  if (root == NULL) return false;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, root, "q2-deathball")) { qa_save_value_free(root); return false; }
  read_q2_death_ball_checkpoint(&reader, out);
  qa_save_reader_destroy(&reader);
  out->root = root;
  return true;
}

