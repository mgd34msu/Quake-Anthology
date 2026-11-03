/* Port of src/persistence/q2-monsters.ts over the content checkpoint type
 * qa_q2m_monsters_checkpoint (content/q2/foundation/monsters/checkpoint.h).
 * Builders emit keys in the order the TS capture objects are created (the
 * MonsterState literal of Q2Monsters.admit minus the saved links, then the
 * links); readers follow the TS reader. GPL-2.0-or-later. */
#include "qa/persistence/q2_monsters.h"

#include <string.h>

#include "q2_codec_internal.h"

static const char *const POWER_ARMOR[] = {"none", "screen", "shield"};
static const char *const WEAPONS[] = {"blaster", "shotgun", "machinegun"};
static const char *const LOCOMOTION[] = {"walk", "fly", "swim", "stationary"};
static const char *const SPAWNED_BY[] = {"none", "carrier", "medic", "widow"};
static const char *const ATTACK_STATES[] = {"straight", "sliding", "melee", "missile", "blind"};

static size_t choice_index(const qa_save_reader *reader, const char *name, const char *const *choices, size_t count) {
  const char *value = q2c_choice(reader, name, choices, count);
  for (size_t i = 0; i < count; i++) if (value == choices[i]) return i;
  return 0;
}
static double finite_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = q2c_field(reader, name);
  double value = qa_save_reader_finite(&f);
  qa_save_reader_destroy(&f);
  return value;
}

/* ---- TS alternateFly / state readers ---- */
static void read_alternate_fly(const qa_save_reader *r, qa_q2m_alternate_fly_state *out) {
  out->alternate_fly = q2c_boolean(r, "alternateFly");
  out->fly_min_distance = q2c_number(r, "flyMinDistance");
  out->fly_max_distance = q2c_number(r, "flyMaxDistance");
  out->fly_acceleration = q2c_number(r, "flyAcceleration");
  out->fly_speed = q2c_number(r, "flySpeed");
  out->fly_ideal_position = q2c_vector(r, "flyIdealPosition");
  out->fly_position_time = q2c_number(r, "flyPositionTime");
  out->fly_buzzard = q2c_boolean(r, "flyBuzzard");
  out->fly_above = q2c_boolean(r, "flyAbove");
  out->fly_pinned = q2c_boolean(r, "flyPinned");
  out->fly_thrusters = q2c_boolean(r, "flyThrusters");
  out->fly_recovery_time = q2c_number(r, "flyRecoveryTime");
  out->fly_recovery_direction = q2c_vector(r, "flyRecoveryDirection");
  out->hint_path = q2c_boolean(r, "hintPath");
  qa_save_reader pathing = q2c_field(r, "pathing");
  out->has_pathing = !qa_save_reader_is_null(&pathing);
  if (out->has_pathing) {
    out->pathing.first_move_point = q2c_vector(&pathing, "firstMovePoint");
    out->pathing.second_move_point = q2c_vector(&pathing, "secondMovePoint");
    out->pathing.traversal_pending = q2c_boolean(&pathing, "traversalPending");
  }
  qa_save_reader_destroy(&pathing);
}

static void read_state(const qa_save_reader *r, qa_q2m_state_checkpoint *out) {
  static const double WATER_LEVELS[] = {0, 1, 2, 3};
  qa_q2m_state *s = &out->rest;
  s->initial_power_armor_type = (qa_q2m_power_armor)choice_index(r, "initialPowerArmorType", POWER_ARMOR, 3);
  s->max_power_armor_power = finite_field(r, "maxPowerArmorPower");
  s->base_health = finite_field(r, "baseHealth");
  s->health_scaling = q2c_integer(r, "healthScaling", 1);
  read_alternate_fly(r, &s->alt);
  s->kind = q2c_string(r, "kind");
  s->weapon = (qa_q2m_weapon_kind)choice_index(r, "weapon", WEAPONS, 3);
  s->locomotion = (qa_q2m_locomotion)choice_index(r, "locomotion", LOCOMOTION, 4);
  s->has_melee = q2c_boolean(r, "hasMelee");
  s->has_ranged_attack = q2c_boolean(r, "hasRangedAttack");
  s->has_idle = q2c_boolean(r, "hasIdle");
  s->has_search = q2c_boolean(r, "hasSearch");
  s->blind_fire = q2c_boolean(r, "blindFire");
  s->good_guy = q2c_boolean(r, "goodGuy");
  s->target_anger = q2c_boolean(r, "targetAnger");
  s->ignore_shots = q2c_boolean(r, "ignoreShots");
  s->do_not_count = q2c_boolean(r, "doNotCount");
  s->spawned_by = (qa_q2m_spawned_by)choice_index(r, "spawnedBy", SPAWNED_BY, 4);
  out->has_commander = q2c_nullable_actor(r, "commander", &out->commander);
  s->monster_slots = q2c_number(r, "monsterSlots");
  s->monster_used = q2c_number(r, "monsterUsed");
  s->brutal = q2c_boolean(r, "brutal");
  s->medic = q2c_boolean(r, "medic");
  s->resurrecting = q2c_boolean(r, "resurrecting");
  out->move = q2c_string(r, "move");
  out->next_move = q2c_is_null(r, "nextMove") ? NULL : q2c_string(r, "nextMove");
  s->next_frame = q2c_number(r, "nextFrame");
  s->next_move_time = q2c_number(r, "nextMoveTime");
  s->scale = q2c_number(r, "scale");
  s->gib_health = q2c_number(r, "gibHealth");
  s->can_take_damage = q2c_boolean(r, "canTakeDamage");
  s->dead = q2c_boolean(r, "dead");
  s->corpse = q2c_boolean(r, "corpse");
  s->gibbed = q2c_boolean(r, "gibbed");
  s->stand_ground = q2c_boolean(r, "standGround");
  s->temporary_stand_ground = q2c_boolean(r, "temporaryStandGround");
  s->hold_frame = q2c_boolean(r, "holdFrame");
  s->ducked = q2c_boolean(r, "ducked");
  s->dodging = q2c_boolean(r, "dodging");
  s->charging = q2c_boolean(r, "charging");
  s->manual_steering = q2c_boolean(r, "manualSteering");
  s->combat_point = q2c_boolean(r, "combatPoint");
  s->attack_state = (qa_q2m_attack_state)choice_index(r, "attackState", ATTACK_STATES, 5);
  s->lefty = q2c_boolean(r, "lefty");
  s->ideal_yaw = q2c_number(r, "idealYaw");
  s->yaw_speed = q2c_number(r, "yawSpeed");
  s->pause_time = q2c_number(r, "pauseTime");
  s->idle_time = q2c_number(r, "idleTime");
  s->pain_time = q2c_number(r, "painTime");
  s->fire_wait = q2c_number(r, "fireWait");
  s->duck_wait = q2c_number(r, "duckWait");
  s->next_duck_time = q2c_number(r, "nextDuckTime");
  s->dodge_time = q2c_number(r, "dodgeTime");
  s->attack_finished = q2c_number(r, "attackFinished");
  s->check_attack_time = q2c_number(r, "checkAttackTime");
  s->strafe_time = q2c_number(r, "strafeTime");
  s->had_visibility = q2c_boolean(r, "hadVisibility");
  s->close_sight_tripped = q2c_boolean(r, "closeSightTripped");
  /* Timers added after the first save revision read as expired from older saves; the flags read as false. */
  s->bad_move_time = q2c_present(r, "badMoveTime") ? q2c_number(r, "badMoveTime") : 0;
  s->reached_hold_combat = q2c_true(r, "reachedHoldCombat");
  s->third_eye = q2c_true(r, "thirdEye");
  s->bump_time = q2c_present(r, "bumpTime") ? q2c_number(r, "bumpTime") : 0;
  s->no_step = q2c_true(r, "noStep");
  s->random_change_time = q2c_present(r, "randomChangeTime") ? q2c_number(r, "randomChangeTime") : 0;
  s->melee_time = q2c_number(r, "meleeTime");
  s->search_time = q2c_number(r, "searchTime");
  s->trail_time = q2c_number(r, "trailTime");
  s->show_hostile = q2c_number(r, "showHostile");
  s->last_sighting = q2c_vector(r, "lastSighting");
  s->has_saved_goal = !q2c_is_null(r, "savedGoal");
  if (s->has_saved_goal) s->saved_goal = q2c_vector(r, "savedGoal");
  s->lost_sight = q2c_boolean(r, "lostSight");
  s->pursue_next = q2c_boolean(r, "pursueNext");
  s->pursue_temporary = q2c_boolean(r, "pursueTemporary");
  s->pursuit_last_seen = q2c_boolean(r, "pursuitLastSeen");
  s->blind_fire_target = q2c_vector(r, "blindFireTarget");
  s->blind_fire_delay = q2c_number(r, "blindFireDelay");
  qa_save_reader sound = q2c_field(r, "soundTarget");
  out->has_sound_target = !qa_save_reader_is_null(&sound);
  if (out->has_sound_target) {
    out->sound_target.actor = q2c_actor(&sound, "actor");
    out->sound_target.owner = q2c_actor(&sound, "owner");
    out->sound_target.origin = q2c_vector(&sound, "origin");
    out->sound_target.time = q2c_number(&sound, "time");
  }
  qa_save_reader_destroy(&sound);
  out->has_old_enemy = q2c_nullable_actor(r, "oldEnemy", &out->old_enemy);
  out->has_move_target = q2c_nullable_actor(r, "moveTarget", &out->move_target);
  s->combat_target = q2c_string(r, "combatTarget");
  s->cocked = q2c_boolean(r, "cocked");
  s->force_refire = q2c_boolean(r, "forceRefire");
  s->normal_height = q2c_number(r, "normalHeight");
  s->air_finished = q2c_number(r, "airFinished");
  s->environmental_damage_time = q2c_number(r, "environmentalDamageTime");
  qa_save_reader water = q2c_field(r, "waterLevel");
  s->water_level = qa_save_reader_choice_number(&water, WATER_LEVELS, 4);
  qa_save_reader_destroy(&water);
  s->water_type = q2c_number(r, "waterType");
  s->last_link_count = q2c_number(r, "lastLinkCount");
  s->jump_time = q2c_number(r, "jumpTime");
  s->has_flies_time = !q2c_is_null(r, "fliesTime");
  if (s->has_flies_time) s->flies_time = q2c_number(r, "fliesTime");
}

/* TS Q2Monsters.capture() state: the admit() literal (createAlternateFlyState
 * spread first) minus move/nextMove/soundTarget/oldEnemy/moveTarget/commander,
 * then those six. */
static qa_save_value *state_value(const qa_q2m_state_checkpoint *in) {
  const qa_q2m_state *s = &in->rest;
  const qa_q2m_alternate_fly_state *a = &s->alt;
  q2c_object o = q2c_object_new();
  q2c_set(&o, "alternateFly", qa_save_value_boolean(a->alternate_fly));
  q2c_set(&o, "flyMinDistance", qa_save_value_number(a->fly_min_distance));
  q2c_set(&o, "flyMaxDistance", qa_save_value_number(a->fly_max_distance));
  q2c_set(&o, "flyAcceleration", qa_save_value_number(a->fly_acceleration));
  q2c_set(&o, "flySpeed", qa_save_value_number(a->fly_speed));
  q2c_set(&o, "flyIdealPosition", q2c_vector_value(a->fly_ideal_position));
  q2c_set(&o, "flyPositionTime", qa_save_value_number(a->fly_position_time));
  q2c_set(&o, "flyBuzzard", qa_save_value_boolean(a->fly_buzzard));
  q2c_set(&o, "flyAbove", qa_save_value_boolean(a->fly_above));
  q2c_set(&o, "flyPinned", qa_save_value_boolean(a->fly_pinned));
  q2c_set(&o, "flyThrusters", qa_save_value_boolean(a->fly_thrusters));
  q2c_set(&o, "flyRecoveryTime", qa_save_value_number(a->fly_recovery_time));
  q2c_set(&o, "flyRecoveryDirection", q2c_vector_value(a->fly_recovery_direction));
  q2c_set(&o, "hintPath", qa_save_value_boolean(a->hint_path));
  if (a->has_pathing) {
    q2c_object pathing = q2c_object_new();
    q2c_set(&pathing, "firstMovePoint", q2c_vector_value(a->pathing.first_move_point));
    q2c_set(&pathing, "secondMovePoint", q2c_vector_value(a->pathing.second_move_point));
    q2c_set(&pathing, "traversalPending", qa_save_value_boolean(a->pathing.traversal_pending));
    q2c_set(&o, "pathing", q2c_done(&pathing));
  } else {
    q2c_set(&o, "pathing", qa_save_value_null());
  }
  q2c_set(&o, "initialPowerArmorType", qa_save_value_string(POWER_ARMOR[s->initial_power_armor_type]));
  q2c_set(&o, "maxPowerArmorPower", qa_save_value_number(s->max_power_armor_power));
  q2c_set(&o, "baseHealth", qa_save_value_number(s->base_health));
  q2c_set(&o, "healthScaling", qa_save_value_number(s->health_scaling));
  q2c_set(&o, "kind", q2c_string_value(s->kind));
  q2c_set(&o, "weapon", qa_save_value_string(WEAPONS[s->weapon]));
  q2c_set(&o, "locomotion", qa_save_value_string(LOCOMOTION[s->locomotion]));
  q2c_set(&o, "hasMelee", qa_save_value_boolean(s->has_melee));
  q2c_set(&o, "hasRangedAttack", qa_save_value_boolean(s->has_ranged_attack));
  q2c_set(&o, "hasIdle", qa_save_value_boolean(s->has_idle));
  q2c_set(&o, "hasSearch", qa_save_value_boolean(s->has_search));
  q2c_set(&o, "blindFire", qa_save_value_boolean(s->blind_fire));
  q2c_set(&o, "goodGuy", qa_save_value_boolean(s->good_guy));
  q2c_set(&o, "targetAnger", qa_save_value_boolean(s->target_anger));
  q2c_set(&o, "brutal", qa_save_value_boolean(s->brutal));
  q2c_set(&o, "medic", qa_save_value_boolean(s->medic));
  q2c_set(&o, "resurrecting", qa_save_value_boolean(s->resurrecting));
  q2c_set(&o, "ignoreShots", qa_save_value_boolean(s->ignore_shots));
  q2c_set(&o, "doNotCount", qa_save_value_boolean(s->do_not_count));
  q2c_set(&o, "spawnedBy", qa_save_value_string(SPAWNED_BY[s->spawned_by]));
  q2c_set(&o, "monsterSlots", qa_save_value_number(s->monster_slots));
  q2c_set(&o, "monsterUsed", qa_save_value_number(s->monster_used));
  q2c_set(&o, "nextFrame", qa_save_value_number(s->next_frame));
  q2c_set(&o, "nextMoveTime", qa_save_value_number(s->next_move_time));
  q2c_set(&o, "scale", qa_save_value_number(s->scale));
  q2c_set(&o, "gibHealth", qa_save_value_number(s->gib_health));
  q2c_set(&o, "canTakeDamage", qa_save_value_boolean(s->can_take_damage));
  q2c_set(&o, "dead", qa_save_value_boolean(s->dead));
  q2c_set(&o, "corpse", qa_save_value_boolean(s->corpse));
  q2c_set(&o, "gibbed", qa_save_value_boolean(s->gibbed));
  q2c_set(&o, "standGround", qa_save_value_boolean(s->stand_ground));
  q2c_set(&o, "temporaryStandGround", qa_save_value_boolean(s->temporary_stand_ground));
  q2c_set(&o, "holdFrame", qa_save_value_boolean(s->hold_frame));
  q2c_set(&o, "ducked", qa_save_value_boolean(s->ducked));
  q2c_set(&o, "dodging", qa_save_value_boolean(s->dodging));
  q2c_set(&o, "charging", qa_save_value_boolean(s->charging));
  q2c_set(&o, "manualSteering", qa_save_value_boolean(s->manual_steering));
  q2c_set(&o, "combatPoint", qa_save_value_boolean(s->combat_point));
  q2c_set(&o, "attackState", qa_save_value_string(ATTACK_STATES[s->attack_state]));
  q2c_set(&o, "lefty", qa_save_value_boolean(s->lefty));
  q2c_set(&o, "idealYaw", qa_save_value_number(s->ideal_yaw));
  q2c_set(&o, "yawSpeed", qa_save_value_number(s->yaw_speed));
  q2c_set(&o, "pauseTime", qa_save_value_number(s->pause_time));
  q2c_set(&o, "idleTime", qa_save_value_number(s->idle_time));
  q2c_set(&o, "painTime", qa_save_value_number(s->pain_time));
  q2c_set(&o, "fireWait", qa_save_value_number(s->fire_wait));
  q2c_set(&o, "duckWait", qa_save_value_number(s->duck_wait));
  q2c_set(&o, "nextDuckTime", qa_save_value_number(s->next_duck_time));
  q2c_set(&o, "dodgeTime", qa_save_value_number(s->dodge_time));
  q2c_set(&o, "attackFinished", qa_save_value_number(s->attack_finished));
  q2c_set(&o, "checkAttackTime", qa_save_value_number(s->check_attack_time));
  q2c_set(&o, "strafeTime", qa_save_value_number(s->strafe_time));
  q2c_set(&o, "hadVisibility", qa_save_value_boolean(s->had_visibility));
  q2c_set(&o, "closeSightTripped", qa_save_value_boolean(s->close_sight_tripped));
  q2c_set(&o, "badMoveTime", qa_save_value_number(s->bad_move_time));
  q2c_set(&o, "reachedHoldCombat", qa_save_value_boolean(s->reached_hold_combat));
  q2c_set(&o, "thirdEye", qa_save_value_boolean(s->third_eye));
  q2c_set(&o, "bumpTime", qa_save_value_number(s->bump_time));
  q2c_set(&o, "noStep", qa_save_value_boolean(s->no_step));
  q2c_set(&o, "randomChangeTime", qa_save_value_number(s->random_change_time));
  q2c_set(&o, "meleeTime", qa_save_value_number(s->melee_time));
  q2c_set(&o, "searchTime", qa_save_value_number(s->search_time));
  q2c_set(&o, "trailTime", qa_save_value_number(s->trail_time));
  q2c_set(&o, "showHostile", qa_save_value_number(s->show_hostile));
  q2c_set(&o, "lastSighting", q2c_vector_value(s->last_sighting));
  q2c_set(&o, "savedGoal", s->has_saved_goal ? q2c_vector_value(s->saved_goal) : qa_save_value_null());
  q2c_set(&o, "lostSight", qa_save_value_boolean(s->lost_sight));
  q2c_set(&o, "pursueNext", qa_save_value_boolean(s->pursue_next));
  q2c_set(&o, "pursueTemporary", qa_save_value_boolean(s->pursue_temporary));
  q2c_set(&o, "pursuitLastSeen", qa_save_value_boolean(s->pursuit_last_seen));
  q2c_set(&o, "blindFireTarget", q2c_vector_value(s->blind_fire_target));
  q2c_set(&o, "blindFireDelay", qa_save_value_number(s->blind_fire_delay));
  q2c_set(&o, "combatTarget", q2c_string_value(s->combat_target));
  q2c_set(&o, "cocked", qa_save_value_boolean(s->cocked));
  q2c_set(&o, "forceRefire", qa_save_value_boolean(s->force_refire));
  q2c_set(&o, "normalHeight", qa_save_value_number(s->normal_height));
  q2c_set(&o, "waterLevel", qa_save_value_number(s->water_level));
  q2c_set(&o, "waterType", qa_save_value_number(s->water_type));
  q2c_set(&o, "lastLinkCount", qa_save_value_number(s->last_link_count));
  q2c_set(&o, "airFinished", qa_save_value_number(s->air_finished));
  q2c_set(&o, "environmentalDamageTime", qa_save_value_number(s->environmental_damage_time));
  q2c_set(&o, "jumpTime", qa_save_value_number(s->jump_time));
  q2c_set(&o, "fliesTime", s->has_flies_time ? qa_save_value_number(s->flies_time) : qa_save_value_null());
  q2c_set(&o, "move", q2c_string_value(in->move));
  q2c_set(&o, "nextMove", in->next_move != NULL ? qa_save_value_string(in->next_move) : qa_save_value_null());
  if (in->has_sound_target) {
    q2c_object sound = q2c_object_new();
    q2c_set(&sound, "actor", q2c_actor_value(in->sound_target.actor));
    q2c_set(&sound, "owner", q2c_actor_value(in->sound_target.owner));
    q2c_set(&sound, "origin", q2c_vector_value(in->sound_target.origin));
    q2c_set(&sound, "time", qa_save_value_number(in->sound_target.time));
    q2c_set(&o, "soundTarget", q2c_done(&sound));
  } else {
    q2c_set(&o, "soundTarget", qa_save_value_null());
  }
  q2c_set(&o, "oldEnemy", q2c_nullable_actor_value(in->has_old_enemy, in->old_enemy));
  q2c_set(&o, "moveTarget", q2c_nullable_actor_value(in->has_move_target, in->move_target));
  q2c_set(&o, "commander", q2c_nullable_actor_value(in->has_commander, in->commander));
  return q2c_done(&o);
}

/* ---- perception (TS MonsterPerception.capture()) ---- */
static qa_q2m_saved_sighting read_sighting(const qa_save_reader *r) {
  qa_q2m_saved_sighting out;
  out.actor = q2c_actor(r, "actor");
  out.time = q2c_number(r, "time");
  return out;
}
static qa_q2m_saved_noise read_noise(const qa_save_reader *r) {
  qa_q2m_saved_noise out;
  out.actor = q2c_actor(r, "actor");
  out.time = q2c_number(r, "time");
  out.owner = q2c_actor(r, "owner");
  out.origin = q2c_vector(r, "origin");
  return out;
}
static qa_q2m_saved_noise_result read_nullable_noise(const qa_save_reader *r, const char *name) {
  qa_q2m_saved_noise_result out;
  memset(&out, 0, sizeof out);
  qa_save_reader f = q2c_field(r, name);
  out.has_value = !qa_save_reader_is_null(&f);
  if (out.has_value) out.value = read_noise(&f);
  qa_save_reader_destroy(&f);
  return out;
}
static qa_q2m_perception_checkpoint read_perception(const qa_save_reader *r, qa_persist_decoded *owner) {
  qa_q2m_perception_checkpoint out;
  memset(&out, 0, sizeof out);
  out.sight_client.has_value = q2c_nullable_actor(r, "sightClient", &out.sight_client.value);
  qa_save_reader sight = q2c_field(r, "sight");
  out.sight.has_value = !qa_save_reader_is_null(&sight);
  if (out.sight.has_value) out.sight.value = read_sighting(&sight);
  qa_save_reader_destroy(&sight);
  qa_save_reader alerted = q2c_field(r, "alerted");
  out.alerted_count = qa_save_reader_list_count(&alerted);
  qa_q2m_alerted_entry *alerts = qa_persist_decoded_array(owner, out.alerted_count, sizeof *alerts);
  for (size_t i = 0; i < out.alerted_count; i++) {
    qa_save_reader value = q2c_item(&alerted, i);
    alerts[i].actor = q2c_actor(&value, "actor");
    qa_save_reader sighting = q2c_field(&value, "sighting");
    alerts[i].sighting = read_sighting(&sighting);
    qa_save_reader_destroy(&sighting);
    qa_save_reader_destroy(&value);
  }
  out.alerted = alerts;
  qa_save_reader_destroy(&alerted);
  out.primary = read_nullable_noise(r, "primary");
  out.secondary = read_nullable_noise(r, "secondary");
  qa_save_reader noises = q2c_field(r, "noises");
  out.noise_count = qa_save_reader_list_count(&noises);
  qa_q2m_noise_pair_entry *pairs = qa_persist_decoded_array(owner, out.noise_count, sizeof *pairs);
  for (size_t i = 0; i < out.noise_count; i++) {
    qa_save_reader value = q2c_item(&noises, i);
    pairs[i].actor = q2c_actor(&value, "actor");
    pairs[i].primary = q2c_actor(&value, "primary");
    pairs[i].secondary = q2c_actor(&value, "secondary");
    /* Saves from before per-player records omit them (TS reads undefined as null). */
    if (q2c_present(&value, "primaryRecord")) pairs[i].primary_record = read_nullable_noise(&value, "primaryRecord");
    if (q2c_present(&value, "secondaryRecord")) pairs[i].secondary_record = read_nullable_noise(&value, "secondaryRecord");
    qa_save_reader_destroy(&value);
  }
  out.noises = pairs;
  qa_save_reader_destroy(&noises);
  qa_save_reader trails = q2c_field(r, "trails");
  out.trail_count = qa_save_reader_list_count(&trails);
  qa_q2m_trail_entry *trail_entries = qa_persist_decoded_array(owner, out.trail_count, sizeof *trail_entries);
  for (size_t i = 0; i < out.trail_count; i++) {
    qa_save_reader value = q2c_item(&trails, i);
    trail_entries[i].actor = q2c_actor(&value, "actor");
    qa_save_reader points = q2c_field(&value, "points");
    trail_entries[i].point_count = qa_save_reader_list_count(&points);
    qa_q2m_trail_point *trail = qa_persist_decoded_array(owner, trail_entries[i].point_count, sizeof *trail);
    for (size_t p = 0; p < trail_entries[i].point_count; p++) {
      qa_save_reader point = q2c_item(&points, p);
      trail[p].origin = q2c_vector(&point, "origin");
      trail[p].time = q2c_number(&point, "time");
      trail[p].yaw = q2c_number(&point, "yaw");
      qa_save_reader_destroy(&point);
    }
    trail_entries[i].points = trail;
    qa_save_reader_destroy(&points);
    qa_save_reader_destroy(&value);
  }
  out.trails = trail_entries;
  qa_save_reader_destroy(&trails);
  qa_save_reader origins = q2c_field(r, "playerOrigins");
  out.player_origin_count = qa_save_reader_list_count(&origins);
  qa_q2m_origin_entry *origin_entries = qa_persist_decoded_array(owner, out.player_origin_count, sizeof *origin_entries);
  for (size_t i = 0; i < out.player_origin_count; i++) {
    qa_save_reader value = q2c_item(&origins, i);
    origin_entries[i].actor = q2c_actor(&value, "actor");
    origin_entries[i].origin = q2c_vector(&value, "origin");
    qa_save_reader_destroy(&value);
  }
  out.player_origins = origin_entries;
  qa_save_reader_destroy(&origins);
  qa_save_reader hostile = q2c_field(r, "hostile");
  out.hostile_count = qa_save_reader_list_count(&hostile);
  qa_q2m_hostile_entry *hostile_entries = qa_persist_decoded_array(owner, out.hostile_count, sizeof *hostile_entries);
  for (size_t i = 0; i < out.hostile_count; i++) {
    qa_save_reader value = q2c_item(&hostile, i);
    qa_q2m_saved_sighting sighting = read_sighting(&value);
    hostile_entries[i].actor = sighting.actor;
    hostile_entries[i].time = sighting.time;
    qa_save_reader_destroy(&value);
  }
  out.hostile = hostile_entries;
  qa_save_reader_destroy(&hostile);
  out.has_last_frame = !q2c_is_null(r, "lastFrame");
  if (out.has_last_frame) out.last_frame = q2c_number(r, "lastFrame");
  return out;
}
/* Sighting {actor, time}; noise records are created {actor, owner, origin, time}. */
static qa_save_value *sighting_value(qa_saved_actor_id actor, double time) {
  q2c_object o = q2c_object_new();
  q2c_set(&o, "actor", q2c_actor_value(actor));
  q2c_set(&o, "time", qa_save_value_number(time));
  return q2c_done(&o);
}
static qa_save_value *noise_value(const qa_q2m_saved_noise_result *noise) {
  if (!noise->has_value) return qa_save_value_null();
  q2c_object o = q2c_object_new();
  q2c_set(&o, "actor", q2c_actor_value(noise->value.actor));
  q2c_set(&o, "owner", q2c_actor_value(noise->value.owner));
  q2c_set(&o, "origin", q2c_vector_value(noise->value.origin));
  q2c_set(&o, "time", qa_save_value_number(noise->value.time));
  return q2c_done(&o);
}
static qa_save_value *perception_value(const qa_q2m_perception_checkpoint *p) {
  q2c_object o = q2c_object_new();
  q2c_set(&o, "sightClient", q2c_nullable_actor_value(p->sight_client.has_value, p->sight_client.value));
  q2c_set(&o, "sight", p->sight.has_value ? sighting_value(p->sight.value.actor, p->sight.value.time) : qa_save_value_null());
  qa_save_value *alerted = qa_save_value_array();
  for (size_t i = 0; alerted != NULL && i < p->alerted_count; i++) {
    q2c_object entry = q2c_object_new();
    q2c_set(&entry, "actor", q2c_actor_value(p->alerted[i].actor));
    q2c_set(&entry, "sighting", sighting_value(p->alerted[i].sighting.actor, p->alerted[i].sighting.time));
    if (!q2c_push(alerted, q2c_done(&entry))) { qa_save_value_free(alerted); alerted = NULL; }
  }
  q2c_set(&o, "alerted", alerted);
  q2c_set(&o, "primary", noise_value(&p->primary));
  q2c_set(&o, "secondary", noise_value(&p->secondary));
  qa_save_value *noises = qa_save_value_array();
  for (size_t i = 0; noises != NULL && i < p->noise_count; i++) {
    q2c_object entry = q2c_object_new();
    q2c_set(&entry, "actor", q2c_actor_value(p->noises[i].actor));
    q2c_set(&entry, "primary", q2c_actor_value(p->noises[i].primary));
    q2c_set(&entry, "secondary", q2c_actor_value(p->noises[i].secondary));
    q2c_set(&entry, "primaryRecord", noise_value(&p->noises[i].primary_record));
    q2c_set(&entry, "secondaryRecord", noise_value(&p->noises[i].secondary_record));
    if (!q2c_push(noises, q2c_done(&entry))) { qa_save_value_free(noises); noises = NULL; }
  }
  q2c_set(&o, "noises", noises);
  qa_save_value *trails = qa_save_value_array();
  for (size_t i = 0; trails != NULL && i < p->trail_count; i++) {
    qa_save_value *points = qa_save_value_array();
    for (size_t k = 0; points != NULL && k < p->trails[i].point_count; k++) {
      const qa_q2m_trail_point *point = &p->trails[i].points[k];
      q2c_object entry = q2c_object_new();
      q2c_set(&entry, "origin", q2c_vector_value(point->origin));
      q2c_set(&entry, "time", qa_save_value_number(point->time));
      q2c_set(&entry, "yaw", qa_save_value_number(point->yaw));
      if (!q2c_push(points, q2c_done(&entry))) { qa_save_value_free(points); points = NULL; }
    }
    q2c_object entry = q2c_object_new();
    q2c_set(&entry, "actor", q2c_actor_value(p->trails[i].actor));
    q2c_set(&entry, "points", points);
    if (!q2c_push(trails, q2c_done(&entry))) { qa_save_value_free(trails); trails = NULL; }
  }
  q2c_set(&o, "trails", trails);
  qa_save_value *origins = qa_save_value_array();
  for (size_t i = 0; origins != NULL && i < p->player_origin_count; i++) {
    q2c_object entry = q2c_object_new();
    q2c_set(&entry, "actor", q2c_actor_value(p->player_origins[i].actor));
    q2c_set(&entry, "origin", q2c_vector_value(p->player_origins[i].origin));
    if (!q2c_push(origins, q2c_done(&entry))) { qa_save_value_free(origins); origins = NULL; }
  }
  q2c_set(&o, "playerOrigins", origins);
  qa_save_value *hostile = qa_save_value_array();
  for (size_t i = 0; hostile != NULL && i < p->hostile_count; i++)
    if (!q2c_push(hostile, sighting_value(p->hostile[i].actor, p->hostile[i].time))) { qa_save_value_free(hostile); hostile = NULL; }
  q2c_set(&o, "hostile", hostile);
  q2c_set(&o, "lastFrame", p->has_last_frame ? qa_save_value_number(p->last_frame) : qa_save_value_null());
  return q2c_done(&o);
}

/* ---- TS readQ2MonstersCheckpoint ---- */
qa_q2m_monsters_checkpoint qa_read_q2_monsters_checkpoint(const qa_save_reader *reader, qa_persist_decoded *owner) {
  qa_q2m_monsters_checkpoint out;
  memset(&out, 0, sizeof out);
  qa_save_reader version = q2c_field(reader, "version");
  out.version = qa_save_reader_literal_number(&version, 1);
  qa_save_reader_destroy(&version);
  qa_save_reader actors = q2c_field(reader, "actors");
  out.actor_count = qa_save_reader_list_count(&actors);
  qa_q2m_actor_checkpoint *entries = qa_persist_decoded_array(owner, out.actor_count, sizeof *entries);
  for (size_t i = 0; i < out.actor_count; i++) {
    qa_save_reader value = q2c_item(&actors, i);
    qa_q2m_actor_checkpoint *entry = &entries[i];
    entry->actor = q2c_actor(&value, "actor");
    entry->definition = q2c_string(&value, "definition");
    qa_save_reader state = q2c_field(&value, "state");
    read_state(&state, &entry->state);
    qa_save_reader_destroy(&state);
    qa_save_reader pending = q2c_field(&value, "pendingDamage");
    entry->pending_damage.has_value = !qa_save_reader_is_null(&pending);
    if (entry->pending_damage.has_value) {
      qa_q2m_damage_checkpoint *damage = &entry->pending_damage.value;
      qa_save_reader reaction = q2c_field(&pending, "reaction");
      damage->damage = q2c_number(&reaction, "damage");
      damage->kick = q2c_number(&reaction, "kick");
      damage->point = q2c_vector(&reaction, "point");
      damage->has_attacker = q2c_nullable_actor(&reaction, "attacker", &damage->attacker);
      damage->has_inflictor = q2c_nullable_actor(&reaction, "inflictor", &damage->inflictor);
      qa_save_reader_destroy(&reaction);
      qa_save_reader attack = q2c_field(&pending, "attack");
      damage->has_attack = !qa_save_reader_is_null(&attack);
      if (damage->has_attack) damage->attack = qa_read_q2_attack_checkpoint(&attack);
      qa_save_reader_destroy(&attack);
    }
    qa_save_reader_destroy(&pending);
    qa_save_reader_destroy(&value);
  }
  out.actors = entries;
  qa_save_reader_destroy(&actors);
  qa_save_reader perception = q2c_field(reader, "perception");
  out.perception = read_perception(&perception, owner);
  qa_save_reader_destroy(&perception);
  return out;
}

/* TS Q2Monsters.capture(): {version, perception, actors}; actor records
 * {actor, definition, state, pendingDamage}. */
static qa_save_value *monsters_value(const qa_q2m_monsters_checkpoint *checkpoint) {
  qa_save_value *actors = qa_save_value_array();
  for (size_t i = 0; actors != NULL && i < checkpoint->actor_count; i++) {
    const qa_q2m_actor_checkpoint *entry = &checkpoint->actors[i];
    q2c_object o = q2c_object_new();
    q2c_set(&o, "actor", q2c_actor_value(entry->actor));
    q2c_set(&o, "definition", q2c_string_value(entry->definition));
    q2c_set(&o, "state", state_value(&entry->state));
    if (entry->pending_damage.has_value) {
      const qa_q2m_damage_checkpoint *damage = &entry->pending_damage.value;
      q2c_object reaction = q2c_object_new();
      q2c_set(&reaction, "damage", qa_save_value_number(damage->damage));
      q2c_set(&reaction, "kick", qa_save_value_number(damage->kick));
      q2c_set(&reaction, "point", q2c_vector_value(damage->point));
      q2c_set(&reaction, "attacker", q2c_nullable_actor_value(damage->has_attacker, damage->attacker));
      q2c_set(&reaction, "inflictor", q2c_nullable_actor_value(damage->has_inflictor, damage->inflictor));
      q2c_object pending = q2c_object_new();
      q2c_set(&pending, "reaction", q2c_done(&reaction));
      q2c_set(&pending, "attack", damage->has_attack ? qa_q2_attack_checkpoint_value(&damage->attack) : qa_save_value_null());
      q2c_set(&o, "pendingDamage", q2c_done(&pending));
    } else {
      q2c_set(&o, "pendingDamage", qa_save_value_null());
    }
    if (!q2c_push(actors, q2c_done(&o))) { qa_save_value_free(actors); actors = NULL; }
  }
  q2c_object o = q2c_object_new();
  q2c_set(&o, "version", qa_save_value_number(checkpoint->version));
  q2c_set(&o, "perception", perception_value(&checkpoint->perception));
  q2c_set(&o, "actors", actors);
  return q2c_done(&o);
}

uint8_t *qa_encode_q2_monsters_checkpoint(const qa_q2m_monsters_checkpoint *checkpoint, size_t *out_length) {
  return q2c_encode(monsters_value(checkpoint), out_length);
}

bool qa_decode_q2_monsters_checkpoint(const uint8_t *bytes, size_t length, qa_q2m_monsters_checkpoint *out,
                                      qa_persist_decoded *owner) {
  qa_save_reader reader;
  if (!q2c_open(bytes, length, "q2-monsters", owner, &reader)) return false;
  *out = qa_read_q2_monsters_checkpoint(&reader, owner);
  qa_save_reader_destroy(&reader);
  return true;
}
