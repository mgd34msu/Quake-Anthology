/* Port of src/persistence/q2-monsters.ts. GPL-2.0-or-later. */
#include "qa/persistence/q2_monsters.h"

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

static void read_q2_fly_pathing(const qa_save_reader *reader, qa_q2_fly_pathing *out);
static qa_save_value *build_q2_fly_pathing(const qa_q2_fly_pathing *in);
static void read_q2_alternate_fly_state(const qa_save_reader *reader, qa_q2_alternate_fly_state *out);
static qa_save_value *build_q2_alternate_fly_state(const qa_q2_alternate_fly_state *in);
static void read_q2_monster_sound_target(const qa_save_reader *reader, qa_q2_monster_sound_target *out);
static qa_save_value *build_q2_monster_sound_target(const qa_q2_monster_sound_target *in);
static void read_q2_monster_state_checkpoint(const qa_save_reader *reader, qa_q2_monster_state_checkpoint *out);
static qa_save_value *build_q2_monster_state_checkpoint(const qa_q2_monster_state_checkpoint *in);
static void read_q2_saved_sighting(const qa_save_reader *reader, qa_q2_saved_sighting *out);
static qa_save_value *build_q2_saved_sighting(const qa_q2_saved_sighting *in);
static void read_q2_saved_noise(const qa_save_reader *reader, qa_q2_saved_noise *out);
static qa_save_value *build_q2_saved_noise(const qa_q2_saved_noise *in);
static void read_q2_alerted_entry(const qa_save_reader *reader, qa_q2_alerted_entry *out);
static qa_save_value *build_q2_alerted_entry(const qa_q2_alerted_entry *in);
static void read_q2_noise_entry(const qa_save_reader *reader, qa_q2_noise_entry *out);
static qa_save_value *build_q2_noise_entry(const qa_q2_noise_entry *in);
static void read_q2_trail_point(const qa_save_reader *reader, qa_q2_trail_point *out);
static qa_save_value *build_q2_trail_point(const qa_q2_trail_point *in);
static void read_q2_trail_entry(const qa_save_reader *reader, qa_q2_trail_entry *out);
static qa_save_value *build_q2_trail_entry(const qa_q2_trail_entry *in);
static void read_q2_player_origin_entry(const qa_save_reader *reader, qa_q2_player_origin_entry *out);
static qa_save_value *build_q2_player_origin_entry(const qa_q2_player_origin_entry *in);
static void read_q2_monster_perception_checkpoint(const qa_save_reader *reader, qa_q2_monster_perception_checkpoint *out);
static qa_save_value *build_q2_monster_perception_checkpoint(const qa_q2_monster_perception_checkpoint *in);
static void read_q2_monster_reaction(const qa_save_reader *reader, qa_q2_monster_reaction *out);
static qa_save_value *build_q2_monster_reaction(const qa_q2_monster_reaction *in);
static void read_q2_monster_damage_checkpoint(const qa_save_reader *reader, qa_q2_monster_damage_checkpoint *out);
static qa_save_value *build_q2_monster_damage_checkpoint(const qa_q2_monster_damage_checkpoint *in);
static void read_q2_monster_actor(const qa_save_reader *reader, qa_q2_monster_actor *out);
static qa_save_value *build_q2_monster_actor(const qa_q2_monster_actor *in);
static void read_q2_monsters_checkpoint(const qa_save_reader *reader, qa_q2_monsters_checkpoint *out);
static qa_save_value *build_q2_monsters_checkpoint(const qa_q2_monsters_checkpoint *in);

static void read_q2_fly_pathing(const qa_save_reader *reader, qa_q2_fly_pathing *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "firstMovePoint");
    (*out).first_move_point = qa_read_vector(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "secondMovePoint");
    (*out).second_move_point = qa_read_vector(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "traversalPending");
    (*out).traversal_pending = qa_save_reader_boolean(&f2);
    qa_save_reader_destroy(&f2); }
}
static qa_save_value *build_q2_fly_pathing(const qa_q2_fly_pathing *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "firstMovePoint", vec_value(&(*in).first_move_point))) goto fail;
  if (!set_own(obj, "secondMovePoint", vec_value(&(*in).second_move_point))) goto fail;
  if (!set_own(obj, "traversalPending", qa_save_value_boolean((*in).traversal_pending))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_alternate_fly_state(const qa_save_reader *reader, qa_q2_alternate_fly_state *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "alternateFly");
    (*out).alternate_fly = qa_save_reader_boolean(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "flyMinDistance");
    (*out).fly_min_distance = qa_save_reader_number(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "flyMaxDistance");
    (*out).fly_max_distance = qa_save_reader_number(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "flyAcceleration");
    (*out).fly_acceleration = qa_save_reader_number(&f3);
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "flySpeed");
    (*out).fly_speed = qa_save_reader_number(&f4);
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "flyIdealPosition");
    (*out).fly_ideal_position = qa_read_vector(&f5);
    qa_save_reader_destroy(&f5); }
  { qa_save_reader f6 = field(reader, "flyPositionTime");
    (*out).fly_position_time = qa_save_reader_number(&f6);
    qa_save_reader_destroy(&f6); }
  { qa_save_reader f7 = field(reader, "flyBuzzard");
    (*out).fly_buzzard = qa_save_reader_boolean(&f7);
    qa_save_reader_destroy(&f7); }
  { qa_save_reader f8 = field(reader, "flyAbove");
    (*out).fly_above = qa_save_reader_boolean(&f8);
    qa_save_reader_destroy(&f8); }
  { qa_save_reader f9 = field(reader, "flyPinned");
    (*out).fly_pinned = qa_save_reader_boolean(&f9);
    qa_save_reader_destroy(&f9); }
  { qa_save_reader f10 = field(reader, "flyThrusters");
    (*out).fly_thrusters = qa_save_reader_boolean(&f10);
    qa_save_reader_destroy(&f10); }
  { qa_save_reader f11 = field(reader, "flyRecoveryTime");
    (*out).fly_recovery_time = qa_save_reader_number(&f11);
    qa_save_reader_destroy(&f11); }
  { qa_save_reader f12 = field(reader, "flyRecoveryDirection");
    (*out).fly_recovery_direction = qa_read_vector(&f12);
    qa_save_reader_destroy(&f12); }
  { qa_save_reader f13 = field(reader, "hintPath");
    (*out).hint_path = qa_save_reader_boolean(&f13);
    qa_save_reader_destroy(&f13); }
  { qa_save_reader f14 = field(reader, "pathing");
    if (!qa_save_reader_is_null(&f14)) { (*out).has_pathing = true;
      read_q2_fly_pathing(&f14, &(*out).pathing);
    }
    qa_save_reader_destroy(&f14); }
}
static qa_save_value *build_q2_alternate_fly_state(const qa_q2_alternate_fly_state *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "alternateFly", qa_save_value_boolean((*in).alternate_fly))) goto fail;
  if (!set_own(obj, "flyMinDistance", qa_save_value_number((*in).fly_min_distance))) goto fail;
  if (!set_own(obj, "flyMaxDistance", qa_save_value_number((*in).fly_max_distance))) goto fail;
  if (!set_own(obj, "flyAcceleration", qa_save_value_number((*in).fly_acceleration))) goto fail;
  if (!set_own(obj, "flySpeed", qa_save_value_number((*in).fly_speed))) goto fail;
  if (!set_own(obj, "flyIdealPosition", vec_value(&(*in).fly_ideal_position))) goto fail;
  if (!set_own(obj, "flyPositionTime", qa_save_value_number((*in).fly_position_time))) goto fail;
  if (!set_own(obj, "flyBuzzard", qa_save_value_boolean((*in).fly_buzzard))) goto fail;
  if (!set_own(obj, "flyAbove", qa_save_value_boolean((*in).fly_above))) goto fail;
  if (!set_own(obj, "flyPinned", qa_save_value_boolean((*in).fly_pinned))) goto fail;
  if (!set_own(obj, "flyThrusters", qa_save_value_boolean((*in).fly_thrusters))) goto fail;
  if (!set_own(obj, "flyRecoveryTime", qa_save_value_number((*in).fly_recovery_time))) goto fail;
  if (!set_own(obj, "flyRecoveryDirection", vec_value(&(*in).fly_recovery_direction))) goto fail;
  if (!set_own(obj, "hintPath", qa_save_value_boolean((*in).hint_path))) goto fail;
  if (!set_own(obj, "pathing", (*in).has_pathing ? build_q2_fly_pathing(&(*in).pathing) : qa_save_value_null())) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_monster_sound_target(const qa_save_reader *reader, qa_q2_monster_sound_target *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "owner");
    (*out).owner = read_actor(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "origin");
    (*out).origin = qa_read_vector(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "time");
    (*out).time = qa_save_reader_number(&f3);
    qa_save_reader_destroy(&f3); }
}
static qa_save_value *build_q2_monster_sound_target(const qa_q2_monster_sound_target *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "owner", actor_value(&(*in).owner))) goto fail;
  if (!set_own(obj, "origin", vec_value(&(*in).origin))) goto fail;
  if (!set_own(obj, "time", qa_save_value_number((*in).time))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_monster_state_checkpoint(const qa_save_reader *reader, qa_q2_monster_state_checkpoint *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "initialPowerArmorType");
    static const char *const ch1[] = {"none", "screen", "shield"};
    (*out).initial_power_armor_type = qa_save_reader_choice_string(&f0, ch1, 3);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "maxPowerArmorPower");
    (*out).max_power_armor_power = qa_save_reader_finite(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "baseHealth");
    (*out).base_health = qa_save_reader_finite(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "healthScaling");
    (*out).health_scaling = qa_save_reader_integer_min(&f3, 1);
    qa_save_reader_destroy(&f3); }
  read_q2_alternate_fly_state(reader, &(*out).fly);
  { qa_save_reader f5 = field(reader, "kind");
    (*out).kind = qa_save_reader_string(&f5);
    qa_save_reader_destroy(&f5); }
  { qa_save_reader f6 = field(reader, "weapon");
    static const char *const ch2[] = {"blaster", "shotgun", "machinegun"};
    (*out).weapon = qa_save_reader_choice_string(&f6, ch2, 3);
    qa_save_reader_destroy(&f6); }
  { qa_save_reader f7 = field(reader, "locomotion");
    static const char *const ch3[] = {"walk", "fly", "swim", "stationary"};
    (*out).locomotion = qa_save_reader_choice_string(&f7, ch3, 4);
    qa_save_reader_destroy(&f7); }
  { qa_save_reader f8 = field(reader, "hasMelee");
    (*out).has_melee = qa_save_reader_boolean(&f8);
    qa_save_reader_destroy(&f8); }
  { qa_save_reader f9 = field(reader, "hasRangedAttack");
    (*out).has_ranged_attack = qa_save_reader_boolean(&f9);
    qa_save_reader_destroy(&f9); }
  { qa_save_reader f10 = field(reader, "hasIdle");
    (*out).has_idle = qa_save_reader_boolean(&f10);
    qa_save_reader_destroy(&f10); }
  { qa_save_reader f11 = field(reader, "hasSearch");
    (*out).has_search = qa_save_reader_boolean(&f11);
    qa_save_reader_destroy(&f11); }
  { qa_save_reader f12 = field(reader, "blindFire");
    (*out).blind_fire = qa_save_reader_boolean(&f12);
    qa_save_reader_destroy(&f12); }
  { qa_save_reader f13 = field(reader, "goodGuy");
    (*out).good_guy = qa_save_reader_boolean(&f13);
    qa_save_reader_destroy(&f13); }
  { qa_save_reader f14 = field(reader, "targetAnger");
    (*out).target_anger = qa_save_reader_boolean(&f14);
    qa_save_reader_destroy(&f14); }
  { qa_save_reader f15 = field(reader, "ignoreShots");
    (*out).ignore_shots = qa_save_reader_boolean(&f15);
    qa_save_reader_destroy(&f15); }
  { qa_save_reader f16 = field(reader, "doNotCount");
    (*out).do_not_count = qa_save_reader_boolean(&f16);
    qa_save_reader_destroy(&f16); }
  { qa_save_reader f17 = field(reader, "spawnedBy");
    static const char *const ch4[] = {"none", "carrier", "medic", "widow"};
    (*out).spawned_by = qa_save_reader_choice_string(&f17, ch4, 4);
    qa_save_reader_destroy(&f17); }
  { qa_save_reader f18 = field(reader, "commander");
    if (!qa_save_reader_is_null(&f18)) { (*out).has_commander = true;
      (*out).commander = read_actor(&f18);
    }
    qa_save_reader_destroy(&f18); }
  { qa_save_reader f19 = field(reader, "monsterSlots");
    (*out).monster_slots = qa_save_reader_number(&f19);
    qa_save_reader_destroy(&f19); }
  { qa_save_reader f20 = field(reader, "monsterUsed");
    (*out).monster_used = qa_save_reader_number(&f20);
    qa_save_reader_destroy(&f20); }
  { qa_save_reader f21 = field(reader, "brutal");
    (*out).brutal = qa_save_reader_boolean(&f21);
    qa_save_reader_destroy(&f21); }
  { qa_save_reader f22 = field(reader, "medic");
    (*out).medic = qa_save_reader_boolean(&f22);
    qa_save_reader_destroy(&f22); }
  { qa_save_reader f23 = field(reader, "resurrecting");
    (*out).resurrecting = qa_save_reader_boolean(&f23);
    qa_save_reader_destroy(&f23); }
  { qa_save_reader f24 = field(reader, "move");
    (*out).move = qa_save_reader_string(&f24);
    qa_save_reader_destroy(&f24); }
  { qa_save_reader f25 = field(reader, "nextMove");
    if (qa_save_reader_is_null(&f25)) (*out).next_move = NULL; else {
      (*out).next_move = qa_save_reader_string(&f25);
    }
    qa_save_reader_destroy(&f25); }
  { qa_save_reader f26 = field(reader, "nextFrame");
    (*out).next_frame = qa_save_reader_number(&f26);
    qa_save_reader_destroy(&f26); }
  { qa_save_reader f27 = field(reader, "nextMoveTime");
    (*out).next_move_time = qa_save_reader_number(&f27);
    qa_save_reader_destroy(&f27); }
  { qa_save_reader f28 = field(reader, "scale");
    (*out).scale = qa_save_reader_number(&f28);
    qa_save_reader_destroy(&f28); }
  { qa_save_reader f29 = field(reader, "gibHealth");
    (*out).gib_health = qa_save_reader_number(&f29);
    qa_save_reader_destroy(&f29); }
  { qa_save_reader f30 = field(reader, "canTakeDamage");
    (*out).can_take_damage = qa_save_reader_boolean(&f30);
    qa_save_reader_destroy(&f30); }
  { qa_save_reader f31 = field(reader, "dead");
    (*out).dead = qa_save_reader_boolean(&f31);
    qa_save_reader_destroy(&f31); }
  { qa_save_reader f32 = field(reader, "corpse");
    (*out).corpse = qa_save_reader_boolean(&f32);
    qa_save_reader_destroy(&f32); }
  { qa_save_reader f33 = field(reader, "gibbed");
    (*out).gibbed = qa_save_reader_boolean(&f33);
    qa_save_reader_destroy(&f33); }
  { qa_save_reader f34 = field(reader, "standGround");
    (*out).stand_ground = qa_save_reader_boolean(&f34);
    qa_save_reader_destroy(&f34); }
  { qa_save_reader f35 = field(reader, "temporaryStandGround");
    (*out).temporary_stand_ground = qa_save_reader_boolean(&f35);
    qa_save_reader_destroy(&f35); }
  { qa_save_reader f36 = field(reader, "holdFrame");
    (*out).hold_frame = qa_save_reader_boolean(&f36);
    qa_save_reader_destroy(&f36); }
  { qa_save_reader f37 = field(reader, "ducked");
    (*out).ducked = qa_save_reader_boolean(&f37);
    qa_save_reader_destroy(&f37); }
  { qa_save_reader f38 = field(reader, "dodging");
    (*out).dodging = qa_save_reader_boolean(&f38);
    qa_save_reader_destroy(&f38); }
  { qa_save_reader f39 = field(reader, "charging");
    (*out).charging = qa_save_reader_boolean(&f39);
    qa_save_reader_destroy(&f39); }
  { qa_save_reader f40 = field(reader, "manualSteering");
    (*out).manual_steering = qa_save_reader_boolean(&f40);
    qa_save_reader_destroy(&f40); }
  { qa_save_reader f41 = field(reader, "combatPoint");
    (*out).combat_point = qa_save_reader_boolean(&f41);
    qa_save_reader_destroy(&f41); }
  { qa_save_reader f42 = field(reader, "attackState");
    static const char *const ch6[] = {"straight", "sliding", "melee", "missile", "blind"};
    (*out).attack_state = qa_save_reader_choice_string(&f42, ch6, 5);
    qa_save_reader_destroy(&f42); }
  { qa_save_reader f43 = field(reader, "lefty");
    (*out).lefty = qa_save_reader_boolean(&f43);
    qa_save_reader_destroy(&f43); }
  { qa_save_reader f44 = field(reader, "idealYaw");
    (*out).ideal_yaw = qa_save_reader_number(&f44);
    qa_save_reader_destroy(&f44); }
  { qa_save_reader f45 = field(reader, "yawSpeed");
    (*out).yaw_speed = qa_save_reader_number(&f45);
    qa_save_reader_destroy(&f45); }
  { qa_save_reader f46 = field(reader, "pauseTime");
    (*out).pause_time = qa_save_reader_number(&f46);
    qa_save_reader_destroy(&f46); }
  { qa_save_reader f47 = field(reader, "idleTime");
    (*out).idle_time = qa_save_reader_number(&f47);
    qa_save_reader_destroy(&f47); }
  { qa_save_reader f48 = field(reader, "painTime");
    (*out).pain_time = qa_save_reader_number(&f48);
    qa_save_reader_destroy(&f48); }
  { qa_save_reader f49 = field(reader, "fireWait");
    (*out).fire_wait = qa_save_reader_number(&f49);
    qa_save_reader_destroy(&f49); }
  { qa_save_reader f50 = field(reader, "duckWait");
    (*out).duck_wait = qa_save_reader_number(&f50);
    qa_save_reader_destroy(&f50); }
  { qa_save_reader f51 = field(reader, "nextDuckTime");
    (*out).next_duck_time = qa_save_reader_number(&f51);
    qa_save_reader_destroy(&f51); }
  { qa_save_reader f52 = field(reader, "dodgeTime");
    (*out).dodge_time = qa_save_reader_number(&f52);
    qa_save_reader_destroy(&f52); }
  { qa_save_reader f53 = field(reader, "attackFinished");
    (*out).attack_finished = qa_save_reader_number(&f53);
    qa_save_reader_destroy(&f53); }
  { qa_save_reader f54 = field(reader, "checkAttackTime");
    (*out).check_attack_time = qa_save_reader_number(&f54);
    qa_save_reader_destroy(&f54); }
  { qa_save_reader f55 = field(reader, "strafeTime");
    (*out).strafe_time = qa_save_reader_number(&f55);
    qa_save_reader_destroy(&f55); }
  { qa_save_reader f56 = field(reader, "hadVisibility");
    (*out).had_visibility = qa_save_reader_boolean(&f56);
    qa_save_reader_destroy(&f56); }
  { qa_save_reader f57 = field(reader, "closeSightTripped");
    (*out).close_sight_tripped = qa_save_reader_boolean(&f57);
    qa_save_reader_destroy(&f57); }
  { qa_save_reader f58 = field(reader, "meleeTime");
    (*out).melee_time = qa_save_reader_number(&f58);
    qa_save_reader_destroy(&f58); }
  { qa_save_reader f59 = field(reader, "searchTime");
    (*out).search_time = qa_save_reader_number(&f59);
    qa_save_reader_destroy(&f59); }
  { qa_save_reader f60 = field(reader, "trailTime");
    (*out).trail_time = qa_save_reader_number(&f60);
    qa_save_reader_destroy(&f60); }
  { qa_save_reader f61 = field(reader, "showHostile");
    (*out).show_hostile = qa_save_reader_number(&f61);
    qa_save_reader_destroy(&f61); }
  { qa_save_reader f62 = field(reader, "lastSighting");
    (*out).last_sighting = qa_read_vector(&f62);
    qa_save_reader_destroy(&f62); }
  { qa_save_reader f63 = field(reader, "savedGoal");
    if (!qa_save_reader_is_null(&f63)) { (*out).has_saved_goal = true;
      (*out).saved_goal = qa_read_vector(&f63);
    }
    qa_save_reader_destroy(&f63); }
  { qa_save_reader f64 = field(reader, "lostSight");
    (*out).lost_sight = qa_save_reader_boolean(&f64);
    qa_save_reader_destroy(&f64); }
  { qa_save_reader f65 = field(reader, "pursueNext");
    (*out).pursue_next = qa_save_reader_boolean(&f65);
    qa_save_reader_destroy(&f65); }
  { qa_save_reader f66 = field(reader, "pursueTemporary");
    (*out).pursue_temporary = qa_save_reader_boolean(&f66);
    qa_save_reader_destroy(&f66); }
  { qa_save_reader f67 = field(reader, "pursuitLastSeen");
    (*out).pursuit_last_seen = qa_save_reader_boolean(&f67);
    qa_save_reader_destroy(&f67); }
  { qa_save_reader f68 = field(reader, "blindFireTarget");
    (*out).blind_fire_target = qa_read_vector(&f68);
    qa_save_reader_destroy(&f68); }
  { qa_save_reader f69 = field(reader, "blindFireDelay");
    (*out).blind_fire_delay = qa_save_reader_number(&f69);
    qa_save_reader_destroy(&f69); }
  { qa_save_reader f70 = field(reader, "soundTarget");
    if (!qa_save_reader_is_null(&f70)) { (*out).has_sound_target = true;
      read_q2_monster_sound_target(&f70, &(*out).sound_target);
    }
    qa_save_reader_destroy(&f70); }
  { qa_save_reader f71 = field(reader, "oldEnemy");
    if (!qa_save_reader_is_null(&f71)) { (*out).has_old_enemy = true;
      (*out).old_enemy = read_actor(&f71);
    }
    qa_save_reader_destroy(&f71); }
  { qa_save_reader f72 = field(reader, "moveTarget");
    if (!qa_save_reader_is_null(&f72)) { (*out).has_move_target = true;
      (*out).move_target = read_actor(&f72);
    }
    qa_save_reader_destroy(&f72); }
  { qa_save_reader f73 = field(reader, "combatTarget");
    (*out).combat_target = qa_save_reader_string(&f73);
    qa_save_reader_destroy(&f73); }
  { qa_save_reader f74 = field(reader, "cocked");
    (*out).cocked = qa_save_reader_boolean(&f74);
    qa_save_reader_destroy(&f74); }
  { qa_save_reader f75 = field(reader, "forceRefire");
    (*out).force_refire = qa_save_reader_boolean(&f75);
    qa_save_reader_destroy(&f75); }
  { qa_save_reader f76 = field(reader, "normalHeight");
    (*out).normal_height = qa_save_reader_number(&f76);
    qa_save_reader_destroy(&f76); }
  { qa_save_reader f77 = field(reader, "airFinished");
    (*out).air_finished = qa_save_reader_number(&f77);
    qa_save_reader_destroy(&f77); }
  { qa_save_reader f78 = field(reader, "environmentalDamageTime");
    (*out).environmental_damage_time = qa_save_reader_number(&f78);
    qa_save_reader_destroy(&f78); }
  { qa_save_reader f79 = field(reader, "waterLevel");
    static const double ch7[] = {0, 1, 2, 3};
    (*out).water_level = qa_save_reader_choice_number(&f79, ch7, 4);
    qa_save_reader_destroy(&f79); }
  { qa_save_reader f80 = field(reader, "waterType");
    (*out).water_type = qa_save_reader_number(&f80);
    qa_save_reader_destroy(&f80); }
  { qa_save_reader f81 = field(reader, "lastLinkCount");
    (*out).last_link_count = qa_save_reader_number(&f81);
    qa_save_reader_destroy(&f81); }
  { qa_save_reader f82 = field(reader, "jumpTime");
    (*out).jump_time = qa_save_reader_number(&f82);
    qa_save_reader_destroy(&f82); }
  { qa_save_reader f83 = field(reader, "fliesTime");
    if (!qa_save_reader_is_null(&f83)) { (*out).has_flies_time = true;
      (*out).flies_time = qa_save_reader_number(&f83);
    }
    qa_save_reader_destroy(&f83); }
}
static qa_save_value *build_q2_monster_state_checkpoint(const qa_q2_monster_state_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "initialPowerArmorType", str_value((*in).initial_power_armor_type))) goto fail;
  if (!set_own(obj, "maxPowerArmorPower", qa_save_value_number((*in).max_power_armor_power))) goto fail;
  if (!set_own(obj, "baseHealth", qa_save_value_number((*in).base_health))) goto fail;
  if (!set_own(obj, "healthScaling", qa_save_value_number((*in).health_scaling))) goto fail;
  { qa_save_value *flat8 = build_q2_alternate_fly_state(&(*in).fly);
    if (flat8 == NULL || !merge_into(obj, flat8)) { qa_save_value_free(flat8); goto fail; } qa_save_value_free(flat8); }
  if (!set_own(obj, "kind", str_value((*in).kind))) goto fail;
  if (!set_own(obj, "weapon", str_value((*in).weapon))) goto fail;
  if (!set_own(obj, "locomotion", str_value((*in).locomotion))) goto fail;
  if (!set_own(obj, "hasMelee", qa_save_value_boolean((*in).has_melee))) goto fail;
  if (!set_own(obj, "hasRangedAttack", qa_save_value_boolean((*in).has_ranged_attack))) goto fail;
  if (!set_own(obj, "hasIdle", qa_save_value_boolean((*in).has_idle))) goto fail;
  if (!set_own(obj, "hasSearch", qa_save_value_boolean((*in).has_search))) goto fail;
  if (!set_own(obj, "blindFire", qa_save_value_boolean((*in).blind_fire))) goto fail;
  if (!set_own(obj, "goodGuy", qa_save_value_boolean((*in).good_guy))) goto fail;
  if (!set_own(obj, "targetAnger", qa_save_value_boolean((*in).target_anger))) goto fail;
  if (!set_own(obj, "ignoreShots", qa_save_value_boolean((*in).ignore_shots))) goto fail;
  if (!set_own(obj, "doNotCount", qa_save_value_boolean((*in).do_not_count))) goto fail;
  if (!set_own(obj, "spawnedBy", str_value((*in).spawned_by))) goto fail;
  if (!set_own(obj, "commander", (*in).has_commander ? actor_value(&(*in).commander) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "monsterSlots", qa_save_value_number((*in).monster_slots))) goto fail;
  if (!set_own(obj, "monsterUsed", qa_save_value_number((*in).monster_used))) goto fail;
  if (!set_own(obj, "brutal", qa_save_value_boolean((*in).brutal))) goto fail;
  if (!set_own(obj, "medic", qa_save_value_boolean((*in).medic))) goto fail;
  if (!set_own(obj, "resurrecting", qa_save_value_boolean((*in).resurrecting))) goto fail;
  if (!set_own(obj, "move", str_value((*in).move))) goto fail;
  if (!set_own(obj, "nextMove", (*in).next_move != NULL ? str_value((*in).next_move) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "nextFrame", qa_save_value_number((*in).next_frame))) goto fail;
  if (!set_own(obj, "nextMoveTime", qa_save_value_number((*in).next_move_time))) goto fail;
  if (!set_own(obj, "scale", qa_save_value_number((*in).scale))) goto fail;
  if (!set_own(obj, "gibHealth", qa_save_value_number((*in).gib_health))) goto fail;
  if (!set_own(obj, "canTakeDamage", qa_save_value_boolean((*in).can_take_damage))) goto fail;
  if (!set_own(obj, "dead", qa_save_value_boolean((*in).dead))) goto fail;
  if (!set_own(obj, "corpse", qa_save_value_boolean((*in).corpse))) goto fail;
  if (!set_own(obj, "gibbed", qa_save_value_boolean((*in).gibbed))) goto fail;
  if (!set_own(obj, "standGround", qa_save_value_boolean((*in).stand_ground))) goto fail;
  if (!set_own(obj, "temporaryStandGround", qa_save_value_boolean((*in).temporary_stand_ground))) goto fail;
  if (!set_own(obj, "holdFrame", qa_save_value_boolean((*in).hold_frame))) goto fail;
  if (!set_own(obj, "ducked", qa_save_value_boolean((*in).ducked))) goto fail;
  if (!set_own(obj, "dodging", qa_save_value_boolean((*in).dodging))) goto fail;
  if (!set_own(obj, "charging", qa_save_value_boolean((*in).charging))) goto fail;
  if (!set_own(obj, "manualSteering", qa_save_value_boolean((*in).manual_steering))) goto fail;
  if (!set_own(obj, "combatPoint", qa_save_value_boolean((*in).combat_point))) goto fail;
  if (!set_own(obj, "attackState", str_value((*in).attack_state))) goto fail;
  if (!set_own(obj, "lefty", qa_save_value_boolean((*in).lefty))) goto fail;
  if (!set_own(obj, "idealYaw", qa_save_value_number((*in).ideal_yaw))) goto fail;
  if (!set_own(obj, "yawSpeed", qa_save_value_number((*in).yaw_speed))) goto fail;
  if (!set_own(obj, "pauseTime", qa_save_value_number((*in).pause_time))) goto fail;
  if (!set_own(obj, "idleTime", qa_save_value_number((*in).idle_time))) goto fail;
  if (!set_own(obj, "painTime", qa_save_value_number((*in).pain_time))) goto fail;
  if (!set_own(obj, "fireWait", qa_save_value_number((*in).fire_wait))) goto fail;
  if (!set_own(obj, "duckWait", qa_save_value_number((*in).duck_wait))) goto fail;
  if (!set_own(obj, "nextDuckTime", qa_save_value_number((*in).next_duck_time))) goto fail;
  if (!set_own(obj, "dodgeTime", qa_save_value_number((*in).dodge_time))) goto fail;
  if (!set_own(obj, "attackFinished", qa_save_value_number((*in).attack_finished))) goto fail;
  if (!set_own(obj, "checkAttackTime", qa_save_value_number((*in).check_attack_time))) goto fail;
  if (!set_own(obj, "strafeTime", qa_save_value_number((*in).strafe_time))) goto fail;
  if (!set_own(obj, "hadVisibility", qa_save_value_boolean((*in).had_visibility))) goto fail;
  if (!set_own(obj, "closeSightTripped", qa_save_value_boolean((*in).close_sight_tripped))) goto fail;
  if (!set_own(obj, "meleeTime", qa_save_value_number((*in).melee_time))) goto fail;
  if (!set_own(obj, "searchTime", qa_save_value_number((*in).search_time))) goto fail;
  if (!set_own(obj, "trailTime", qa_save_value_number((*in).trail_time))) goto fail;
  if (!set_own(obj, "showHostile", qa_save_value_number((*in).show_hostile))) goto fail;
  if (!set_own(obj, "lastSighting", vec_value(&(*in).last_sighting))) goto fail;
  if (!set_own(obj, "savedGoal", (*in).has_saved_goal ? vec_value(&(*in).saved_goal) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "lostSight", qa_save_value_boolean((*in).lost_sight))) goto fail;
  if (!set_own(obj, "pursueNext", qa_save_value_boolean((*in).pursue_next))) goto fail;
  if (!set_own(obj, "pursueTemporary", qa_save_value_boolean((*in).pursue_temporary))) goto fail;
  if (!set_own(obj, "pursuitLastSeen", qa_save_value_boolean((*in).pursuit_last_seen))) goto fail;
  if (!set_own(obj, "blindFireTarget", vec_value(&(*in).blind_fire_target))) goto fail;
  if (!set_own(obj, "blindFireDelay", qa_save_value_number((*in).blind_fire_delay))) goto fail;
  if (!set_own(obj, "soundTarget", (*in).has_sound_target ? build_q2_monster_sound_target(&(*in).sound_target) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "oldEnemy", (*in).has_old_enemy ? actor_value(&(*in).old_enemy) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "moveTarget", (*in).has_move_target ? actor_value(&(*in).move_target) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "combatTarget", str_value((*in).combat_target))) goto fail;
  if (!set_own(obj, "cocked", qa_save_value_boolean((*in).cocked))) goto fail;
  if (!set_own(obj, "forceRefire", qa_save_value_boolean((*in).force_refire))) goto fail;
  if (!set_own(obj, "normalHeight", qa_save_value_number((*in).normal_height))) goto fail;
  if (!set_own(obj, "airFinished", qa_save_value_number((*in).air_finished))) goto fail;
  if (!set_own(obj, "environmentalDamageTime", qa_save_value_number((*in).environmental_damage_time))) goto fail;
  if (!set_own(obj, "waterLevel", qa_save_value_number((*in).water_level))) goto fail;
  if (!set_own(obj, "waterType", qa_save_value_number((*in).water_type))) goto fail;
  if (!set_own(obj, "lastLinkCount", qa_save_value_number((*in).last_link_count))) goto fail;
  if (!set_own(obj, "jumpTime", qa_save_value_number((*in).jump_time))) goto fail;
  if (!set_own(obj, "fliesTime", (*in).has_flies_time ? qa_save_value_number((*in).flies_time) : qa_save_value_null())) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_saved_sighting(const qa_save_reader *reader, qa_q2_saved_sighting *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "time");
    (*out).time = qa_save_reader_number(&f1);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_saved_sighting(const qa_q2_saved_sighting *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "time", qa_save_value_number((*in).time))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_saved_noise(const qa_save_reader *reader, qa_q2_saved_noise *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "time");
    (*out).time = qa_save_reader_number(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "owner");
    (*out).owner = read_actor(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "origin");
    (*out).origin = qa_read_vector(&f3);
    qa_save_reader_destroy(&f3); }
}
static qa_save_value *build_q2_saved_noise(const qa_q2_saved_noise *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "time", qa_save_value_number((*in).time))) goto fail;
  if (!set_own(obj, "owner", actor_value(&(*in).owner))) goto fail;
  if (!set_own(obj, "origin", vec_value(&(*in).origin))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_alerted_entry(const qa_save_reader *reader, qa_q2_alerted_entry *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "sighting");
    read_q2_saved_sighting(&f1, &(*out).sighting);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_alerted_entry(const qa_q2_alerted_entry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "sighting", build_q2_saved_sighting(&(*in).sighting))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_noise_entry(const qa_save_reader *reader, qa_q2_noise_entry *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "primary");
    (*out).primary = read_actor(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "secondary");
    (*out).secondary = read_actor(&f2);
    qa_save_reader_destroy(&f2); }
}
static qa_save_value *build_q2_noise_entry(const qa_q2_noise_entry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "primary", actor_value(&(*in).primary))) goto fail;
  if (!set_own(obj, "secondary", actor_value(&(*in).secondary))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_trail_point(const qa_save_reader *reader, qa_q2_trail_point *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "origin");
    (*out).origin = qa_read_vector(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "time");
    (*out).time = qa_save_reader_number(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "yaw");
    (*out).yaw = qa_save_reader_number(&f2);
    qa_save_reader_destroy(&f2); }
}
static qa_save_value *build_q2_trail_point(const qa_q2_trail_point *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "origin", vec_value(&(*in).origin))) goto fail;
  if (!set_own(obj, "time", qa_save_value_number((*in).time))) goto fail;
  if (!set_own(obj, "yaw", qa_save_value_number((*in).yaw))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_trail_entry(const qa_save_reader *reader, qa_q2_trail_entry *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "points");
    (*out).points_count = qa_save_reader_list_count(&f1);
    (*out).points = (qa_q2_trail_point *)calloc((*out).points_count != 0 ? (*out).points_count : 1, sizeof(qa_q2_trail_point));
    if ((*out).points == NULL) qa_save_reader_fail(&f1, "out of memory");
    for (size_t i9 = 0; i9 < (*out).points_count; i9++) {
      qa_save_reader it10 = item(&f1, i9);
      read_q2_trail_point(&it10, &(*out).points[i9]);
      qa_save_reader_destroy(&it10);
    }
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_trail_entry(const qa_q2_trail_entry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  { qa_save_value *arr11 = qa_save_value_array();
    if (arr11 == NULL) goto fail;
    for (size_t i12 = 0; i12 < (*in).points_count; i12++) {
      if (!push_own(arr11, build_q2_trail_point(&(*in).points[i12]))) { qa_save_value_free(arr11); goto fail; }
    }
    if (!set_own(obj, "points", arr11)) goto fail; }
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_player_origin_entry(const qa_save_reader *reader, qa_q2_player_origin_entry *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "origin");
    (*out).origin = qa_read_vector(&f1);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_player_origin_entry(const qa_q2_player_origin_entry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "origin", vec_value(&(*in).origin))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_monster_perception_checkpoint(const qa_save_reader *reader, qa_q2_monster_perception_checkpoint *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "sightClient");
    if (!qa_save_reader_is_null(&f0)) { (*out).has_sight_client = true;
      (*out).sight_client = read_actor(&f0);
    }
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "sight");
    if (!qa_save_reader_is_null(&f1)) { (*out).has_sight = true;
      read_q2_saved_sighting(&f1, &(*out).sight);
    }
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "alerted");
    (*out).alerted_count = qa_save_reader_list_count(&f2);
    (*out).alerted = (qa_q2_alerted_entry *)calloc((*out).alerted_count != 0 ? (*out).alerted_count : 1, sizeof(qa_q2_alerted_entry));
    if ((*out).alerted == NULL) qa_save_reader_fail(&f2, "out of memory");
    for (size_t i13 = 0; i13 < (*out).alerted_count; i13++) {
      qa_save_reader it14 = item(&f2, i13);
      read_q2_alerted_entry(&it14, &(*out).alerted[i13]);
      qa_save_reader_destroy(&it14);
    }
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "primary");
    if (!qa_save_reader_is_null(&f3)) { (*out).has_primary = true;
      read_q2_saved_noise(&f3, &(*out).primary);
    }
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "secondary");
    if (!qa_save_reader_is_null(&f4)) { (*out).has_secondary = true;
      read_q2_saved_noise(&f4, &(*out).secondary);
    }
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "noises");
    (*out).noises_count = qa_save_reader_list_count(&f5);
    (*out).noises = (qa_q2_noise_entry *)calloc((*out).noises_count != 0 ? (*out).noises_count : 1, sizeof(qa_q2_noise_entry));
    if ((*out).noises == NULL) qa_save_reader_fail(&f5, "out of memory");
    for (size_t i15 = 0; i15 < (*out).noises_count; i15++) {
      qa_save_reader it16 = item(&f5, i15);
      read_q2_noise_entry(&it16, &(*out).noises[i15]);
      qa_save_reader_destroy(&it16);
    }
    qa_save_reader_destroy(&f5); }
  { qa_save_reader f6 = field(reader, "trails");
    (*out).trails_count = qa_save_reader_list_count(&f6);
    (*out).trails = (qa_q2_trail_entry *)calloc((*out).trails_count != 0 ? (*out).trails_count : 1, sizeof(qa_q2_trail_entry));
    if ((*out).trails == NULL) qa_save_reader_fail(&f6, "out of memory");
    for (size_t i17 = 0; i17 < (*out).trails_count; i17++) {
      qa_save_reader it18 = item(&f6, i17);
      read_q2_trail_entry(&it18, &(*out).trails[i17]);
      qa_save_reader_destroy(&it18);
    }
    qa_save_reader_destroy(&f6); }
  { qa_save_reader f7 = field(reader, "playerOrigins");
    (*out).player_origins_count = qa_save_reader_list_count(&f7);
    (*out).player_origins = (qa_q2_player_origin_entry *)calloc((*out).player_origins_count != 0 ? (*out).player_origins_count : 1, sizeof(qa_q2_player_origin_entry));
    if ((*out).player_origins == NULL) qa_save_reader_fail(&f7, "out of memory");
    for (size_t i19 = 0; i19 < (*out).player_origins_count; i19++) {
      qa_save_reader it20 = item(&f7, i19);
      read_q2_player_origin_entry(&it20, &(*out).player_origins[i19]);
      qa_save_reader_destroy(&it20);
    }
    qa_save_reader_destroy(&f7); }
  { qa_save_reader f8 = field(reader, "hostile");
    (*out).hostile_count = qa_save_reader_list_count(&f8);
    (*out).hostile = (qa_q2_saved_sighting *)calloc((*out).hostile_count != 0 ? (*out).hostile_count : 1, sizeof(qa_q2_saved_sighting));
    if ((*out).hostile == NULL) qa_save_reader_fail(&f8, "out of memory");
    for (size_t i21 = 0; i21 < (*out).hostile_count; i21++) {
      qa_save_reader it22 = item(&f8, i21);
      read_q2_saved_sighting(&it22, &(*out).hostile[i21]);
      qa_save_reader_destroy(&it22);
    }
    qa_save_reader_destroy(&f8); }
  { qa_save_reader f9 = field(reader, "lastFrame");
    if (!qa_save_reader_is_null(&f9)) { (*out).has_last_frame = true;
      (*out).last_frame = qa_save_reader_number(&f9);
    }
    qa_save_reader_destroy(&f9); }
}
static qa_save_value *build_q2_monster_perception_checkpoint(const qa_q2_monster_perception_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "sightClient", (*in).has_sight_client ? actor_value(&(*in).sight_client) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "sight", (*in).has_sight ? build_q2_saved_sighting(&(*in).sight) : qa_save_value_null())) goto fail;
  { qa_save_value *arr23 = qa_save_value_array();
    if (arr23 == NULL) goto fail;
    for (size_t i24 = 0; i24 < (*in).alerted_count; i24++) {
      if (!push_own(arr23, build_q2_alerted_entry(&(*in).alerted[i24]))) { qa_save_value_free(arr23); goto fail; }
    }
    if (!set_own(obj, "alerted", arr23)) goto fail; }
  if (!set_own(obj, "primary", (*in).has_primary ? build_q2_saved_noise(&(*in).primary) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "secondary", (*in).has_secondary ? build_q2_saved_noise(&(*in).secondary) : qa_save_value_null())) goto fail;
  { qa_save_value *arr25 = qa_save_value_array();
    if (arr25 == NULL) goto fail;
    for (size_t i26 = 0; i26 < (*in).noises_count; i26++) {
      if (!push_own(arr25, build_q2_noise_entry(&(*in).noises[i26]))) { qa_save_value_free(arr25); goto fail; }
    }
    if (!set_own(obj, "noises", arr25)) goto fail; }
  { qa_save_value *arr27 = qa_save_value_array();
    if (arr27 == NULL) goto fail;
    for (size_t i28 = 0; i28 < (*in).trails_count; i28++) {
      if (!push_own(arr27, build_q2_trail_entry(&(*in).trails[i28]))) { qa_save_value_free(arr27); goto fail; }
    }
    if (!set_own(obj, "trails", arr27)) goto fail; }
  { qa_save_value *arr29 = qa_save_value_array();
    if (arr29 == NULL) goto fail;
    for (size_t i30 = 0; i30 < (*in).player_origins_count; i30++) {
      if (!push_own(arr29, build_q2_player_origin_entry(&(*in).player_origins[i30]))) { qa_save_value_free(arr29); goto fail; }
    }
    if (!set_own(obj, "playerOrigins", arr29)) goto fail; }
  { qa_save_value *arr31 = qa_save_value_array();
    if (arr31 == NULL) goto fail;
    for (size_t i32 = 0; i32 < (*in).hostile_count; i32++) {
      if (!push_own(arr31, build_q2_saved_sighting(&(*in).hostile[i32]))) { qa_save_value_free(arr31); goto fail; }
    }
    if (!set_own(obj, "hostile", arr31)) goto fail; }
  if (!set_own(obj, "lastFrame", (*in).has_last_frame ? qa_save_value_number((*in).last_frame) : qa_save_value_null())) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_monster_reaction(const qa_save_reader *reader, qa_q2_monster_reaction *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "damage");
    (*out).damage = qa_save_reader_number(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "kick");
    (*out).kick = qa_save_reader_number(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "point");
    (*out).point = qa_read_vector(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "attacker");
    if (!qa_save_reader_is_null(&f3)) { (*out).has_attacker = true;
      (*out).attacker = read_actor(&f3);
    }
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "inflictor");
    if (!qa_save_reader_is_null(&f4)) { (*out).has_inflictor = true;
      (*out).inflictor = read_actor(&f4);
    }
    qa_save_reader_destroy(&f4); }
}
static qa_save_value *build_q2_monster_reaction(const qa_q2_monster_reaction *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "damage", qa_save_value_number((*in).damage))) goto fail;
  if (!set_own(obj, "kick", qa_save_value_number((*in).kick))) goto fail;
  if (!set_own(obj, "point", vec_value(&(*in).point))) goto fail;
  if (!set_own(obj, "attacker", (*in).has_attacker ? actor_value(&(*in).attacker) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "inflictor", (*in).has_inflictor ? actor_value(&(*in).inflictor) : qa_save_value_null())) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_monster_damage_checkpoint(const qa_save_reader *reader, qa_q2_monster_damage_checkpoint *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "reaction");
    read_q2_monster_reaction(&f0, &(*out).reaction);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "attack");
    if (!qa_save_reader_is_null(&f1)) { (*out).has_attack = true;
      (*out).attack = qa_read_q2_attack_checkpoint(&f1);
    }
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_monster_damage_checkpoint(const qa_q2_monster_damage_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "reaction", build_q2_monster_reaction(&(*in).reaction))) goto fail;
  if (!set_own(obj, "attack", (*in).has_attack ? qa_q2_attack_checkpoint_value(&(*in).attack) : qa_save_value_null())) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_monster_actor(const qa_save_reader *reader, qa_q2_monster_actor *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "definition");
    (*out).definition = qa_save_reader_string(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "state");
    read_q2_monster_state_checkpoint(&f2, &(*out).state);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "pendingDamage");
    if (!qa_save_reader_is_null(&f3)) { (*out).has_pending_damage = true;
      read_q2_monster_damage_checkpoint(&f3, &(*out).pending_damage);
    }
    qa_save_reader_destroy(&f3); }
}
static qa_save_value *build_q2_monster_actor(const qa_q2_monster_actor *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "definition", str_value((*in).definition))) goto fail;
  if (!set_own(obj, "state", build_q2_monster_state_checkpoint(&(*in).state))) goto fail;
  if (!set_own(obj, "pendingDamage", (*in).has_pending_damage ? build_q2_monster_damage_checkpoint(&(*in).pending_damage) : qa_save_value_null())) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_monsters_checkpoint(const qa_save_reader *reader, qa_q2_monsters_checkpoint *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "version");
    (*out).version = qa_save_reader_literal_number(&f0, 1);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "actors");
    (*out).actors_count = qa_save_reader_list_count(&f1);
    (*out).actors = (qa_q2_monster_actor *)calloc((*out).actors_count != 0 ? (*out).actors_count : 1, sizeof(qa_q2_monster_actor));
    if ((*out).actors == NULL) qa_save_reader_fail(&f1, "out of memory");
    for (size_t i33 = 0; i33 < (*out).actors_count; i33++) {
      qa_save_reader it34 = item(&f1, i33);
      read_q2_monster_actor(&it34, &(*out).actors[i33]);
      qa_save_reader_destroy(&it34);
    }
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "perception");
    read_q2_monster_perception_checkpoint(&f2, &(*out).perception);
    qa_save_reader_destroy(&f2); }
}
static qa_save_value *build_q2_monsters_checkpoint(const qa_q2_monsters_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "version", qa_save_value_number((*in).version))) goto fail;
  { qa_save_value *arr35 = qa_save_value_array();
    if (arr35 == NULL) goto fail;
    for (size_t i36 = 0; i36 < (*in).actors_count; i36++) {
      if (!push_own(arr35, build_q2_monster_actor(&(*in).actors[i36]))) { qa_save_value_free(arr35); goto fail; }
    }
    if (!set_own(obj, "actors", arr35)) goto fail; }
  if (!set_own(obj, "perception", build_q2_monster_perception_checkpoint(&(*in).perception))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}
















qa_q2_monsters_checkpoint qa_read_q2_monsters_checkpoint(const qa_save_reader *reader) {
  qa_q2_monsters_checkpoint out;
  read_q2_monsters_checkpoint(reader, &out);
  return out;
}
uint8_t *qa_encode_q2_monsters_checkpoint(const qa_q2_monsters_checkpoint *checkpoint, size_t *out_length) {
  qa_save_value *value = build_q2_monsters_checkpoint(checkpoint);
  if (value == NULL) return NULL;
  uint8_t *bytes = qa_encode_checkpoint_value(value, out_length);
  qa_save_value_free(value);
  return bytes;
}
bool qa_decode_q2_monsters_checkpoint(const uint8_t *bytes, size_t length, qa_q2_monsters_checkpoint *out) {
  qa_save_value *root = qa_decode_checkpoint_value(bytes, length);
  if (root == NULL) return false;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, root, "q2-monsters")) { qa_save_value_free(root); return false; }
  read_q2_monsters_checkpoint(&reader, out);
  qa_save_reader_destroy(&reader);
  out->root = root;
  return true;
}

