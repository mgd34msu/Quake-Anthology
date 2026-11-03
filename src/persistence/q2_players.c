/* Port of src/persistence/q2-players.ts. GPL-2.0-or-later. */
#include "qa/persistence/q2_players.h"

#include <stdio.h>
#include <string.h>

#include "q2_codec_internal.h"
#include "qa/persistence/q2_foundation.h"
#include "qa/persistence/save_image.h"

/* ---- shared leaves (save-image.ts readers; builders follow the captured
 * object's key order) ---- */
static qa_save_value *regular_armor_value(const qa_regular_armor_state *in) {
  static const char *const names[] = {"none", "q1", "q2", "q3", "source"};
  q2c_object o = q2c_object_new();
  q2c_set(&o, "kind", qa_save_value_string(names[in->kind]));
  if (in->kind == QA_REGULAR_ARMOR_SOURCE) {
    q2c_set(&o, "points", qa_save_value_number(in->points));
    q2c_set(&o, "item", in->item != NULL ? qa_save_value_string(in->item) : qa_save_value_null());
  } else if (in->kind == QA_REGULAR_ARMOR_Q1) {
    q2c_set(&o, "points", qa_save_value_number(in->points));
    q2c_set(&o, "absorption", qa_save_value_number(in->absorption));
    q2c_set(&o, "item", q2c_string_value(in->item));
  } else if (in->kind == QA_REGULAR_ARMOR_Q3) {
    q2c_set(&o, "points", qa_save_value_number(in->points));
    q2c_set(&o, "protection", qa_save_value_number(in->protection));
  } else if (in->kind == QA_REGULAR_ARMOR_Q2) {
    q2c_set(&o, "points", qa_save_value_number(in->points));
    q2c_set(&o, "normalProtection", qa_save_value_number(in->normal_protection));
    q2c_set(&o, "energyProtection", qa_save_value_number(in->energy_protection));
    q2c_set(&o, "item", q2c_string_value(in->item));
  }
  return q2c_done(&o);
}
static qa_save_value *armor_value(const qa_armor_state *in) {
  q2c_object powered = q2c_object_new();
  q2c_set(&powered, "kind", qa_save_value_string(in->powered.kind != NULL ? in->powered.kind : "none"));
  if (in->powered.kind != NULL && strcmp(in->powered.kind, "none") != 0)
    q2c_set(&powered, "cells", qa_save_value_number(in->powered.cells));
  q2c_object o = q2c_object_new();
  q2c_set(&o, "regular", regular_armor_value(&in->regular));
  q2c_set(&o, "powered", q2c_done(&powered));
  return q2c_done(&o);
}
static qa_save_value *inventory_entry_value(const qa_inventory_entry *in) {
  q2c_object o = q2c_object_new();
  q2c_set(&o, "item", q2c_string_value(in->item));
  q2c_set(&o, "count", qa_save_value_number(in->count));
  q2c_set(&o, "capacity", qa_save_value_number(in->capacity));
  if (in->has_count_policy) {
    q2c_object policy = q2c_object_new();
    q2c_set(&policy, "kind", qa_save_value_string(in->count_policy.kind != NULL ? in->count_policy.kind : "stack"));
    if (in->count_policy.kind != NULL && strcmp(in->count_policy.kind, "source-counter") == 0)
      q2c_set(&policy, "arithmetic", qa_save_value_string(in->count_policy.arithmetic != NULL ? in->count_policy.arithmetic : "binary64"));
    q2c_set(&o, "countPolicy", q2c_done(&policy));
  }
  return q2c_done(&o);
}
static qa_save_value *inventory_value(const qa_inventory_entry *entries, size_t count) {
  qa_save_value *array = qa_save_value_array();
  for (size_t i = 0; array != NULL && i < count; i++)
    if (!q2c_push(array, inventory_entry_value(&entries[i]))) { qa_save_value_free(array); return NULL; }
  return array;
}
static qa_inventory_entry *read_inventory(const qa_save_reader *reader, const char *name, size_t *out_count,
                                          qa_persist_decoded *owner) {
  qa_save_reader list = q2c_field(reader, name);
  size_t count = qa_save_reader_list_count(&list);
  qa_inventory_entry *entries = qa_persist_decoded_array(owner, count, sizeof *entries);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader it = q2c_item(&list, i);
    entries[i] = qa_read_inventory_entry(&it);
    qa_save_reader_destroy(&it);
  }
  qa_save_reader_destroy(&list);
  *out_count = count;
  return entries;
}
/* TS `nullable(namespaced)`. */
static const char *read_nullable_namespaced(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = q2c_field(reader, name);
  const char *value = qa_save_reader_is_null(&f) ? NULL : qa_namespaced(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static qa_save_value *nullable_string_value(const char *text) {
  return text != NULL ? qa_save_value_string(text) : qa_save_value_null();
}

/* ---- Q2PlayerCarry (saveCarry literal order) ---- */
static qa_q2_base_player_carry read_carry(const qa_save_reader *reader, qa_persist_decoded *owner) {
  qa_q2_base_player_carry out;
  memset(&out, 0, sizeof out);
  out.health = q2c_number(reader, "health");
  out.maximum_health = q2c_number(reader, "maximumHealth");
  qa_save_reader armor = q2c_field(reader, "armor");
  out.armor = qa_read_armor(&armor);
  qa_save_reader_destroy(&armor);
  out.inventory = read_inventory(reader, "inventory", &out.inventory_count, owner);
  qa_save_reader weapon = q2c_field(reader, "weapon");
  out.weapon = qa_save_reader_is_null(&weapon) ? NULL : qa_save_reader_string(&weapon);
  qa_save_reader_destroy(&weapon);
  out.selected_item = read_nullable_namespaced(reader, "selectedItem");
  out.score = q2c_number(reader, "score");
  out.flags = q2c_number(reader, "flags");
  out.power_cubes = q2c_number(reader, "powerCubes");
  return out;
}
static qa_save_value *carry_value(const qa_q2_base_player_carry *in) {
  q2c_object o = q2c_object_new();
  q2c_set(&o, "health", qa_save_value_number(in->health));
  q2c_set(&o, "maximumHealth", qa_save_value_number(in->maximum_health));
  q2c_set(&o, "armor", armor_value(&in->armor));
  q2c_set(&o, "inventory", inventory_value(in->inventory, in->inventory_count));
  q2c_set(&o, "weapon", nullable_string_value(in->weapon));
  q2c_set(&o, "selectedItem", nullable_string_value(in->selected_item));
  q2c_set(&o, "score", qa_save_value_number(in->score));
  q2c_set(&o, "flags", qa_save_value_number(in->flags));
  q2c_set(&o, "powerCubes", qa_save_value_number(in->power_cubes));
  return q2c_done(&o);
}

/* ---- Q2PlayerRules (createQ2PlayerRules order) ---- */
static qa_q2_base_player_rules read_rules(const qa_save_reader *reader, qa_persist_decoded *owner) {
  qa_q2_base_player_rules out;
  memset(&out, 0, sizeof out);
  out.password = q2c_string(reader, "password");
  out.spectator_password = q2c_string(reader, "spectatorPassword");
  out.max_spectators = q2c_number(reader, "maxSpectators");
  out.cheats = q2c_boolean(reader, "cheats");
  out.time_limit_minutes = q2c_number(reader, "timeLimitMinutes");
  out.frag_limit = q2c_number(reader, "fragLimit");
  qa_save_reader list = q2c_field(reader, "mapList");
  size_t count = qa_save_reader_list_count(&list);
  const char **maps = qa_persist_decoded_array(owner, count, sizeof *maps);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader it = q2c_item(&list, i);
    maps[i] = qa_save_reader_string(&it);
    qa_save_reader_destroy(&it);
  }
  qa_save_reader_destroy(&list);
  out.map_list = maps; out.map_list_count = count;
  out.map_list_shuffle = q2c_present(reader, "mapListShuffle") ? q2c_boolean(reader, "mapListShuffle") : false;
  out.next_map = q2c_string(reader, "nextMap");
  out.spawn_point = q2c_string(reader, "spawnPoint");
  out.flood_messages = q2c_number(reader, "floodMessages");
  out.flood_seconds = q2c_number(reader, "floodSeconds");
  out.flood_wait_seconds = q2c_number(reader, "floodWaitSeconds");
  out.roll_speed = q2c_number(reader, "rollSpeed");
  out.roll_angle = q2c_number(reader, "rollAngle");
  out.run_pitch = q2c_number(reader, "runPitch");
  out.run_roll = q2c_number(reader, "runRoll");
  out.bob_up = q2c_number(reader, "bobUp");
  out.bob_pitch = q2c_number(reader, "bobPitch");
  out.bob_roll = q2c_number(reader, "bobRoll");
  out.gun_offset = q2c_vector(reader, "gunOffset");
  return out;
}
static qa_save_value *rules_value(const qa_q2_base_player_rules *in) {
  q2c_object o = q2c_object_new();
  q2c_set(&o, "password", q2c_string_value(in->password));
  q2c_set(&o, "spectatorPassword", q2c_string_value(in->spectator_password));
  q2c_set(&o, "maxSpectators", qa_save_value_number(in->max_spectators));
  q2c_set(&o, "cheats", qa_save_value_boolean(in->cheats));
  q2c_set(&o, "timeLimitMinutes", qa_save_value_number(in->time_limit_minutes));
  q2c_set(&o, "fragLimit", qa_save_value_number(in->frag_limit));
  qa_save_value *maps = qa_save_value_array();
  for (size_t i = 0; maps != NULL && i < in->map_list_count; i++)
    if (!q2c_push(maps, q2c_string_value(in->map_list[i]))) { qa_save_value_free(maps); maps = NULL; }
  q2c_set(&o, "mapList", maps);
  q2c_set(&o, "mapListShuffle", qa_save_value_boolean(in->map_list_shuffle));
  q2c_set(&o, "nextMap", q2c_string_value(in->next_map));
  q2c_set(&o, "spawnPoint", q2c_string_value(in->spawn_point));
  q2c_set(&o, "floodMessages", qa_save_value_number(in->flood_messages));
  q2c_set(&o, "floodSeconds", qa_save_value_number(in->flood_seconds));
  q2c_set(&o, "floodWaitSeconds", qa_save_value_number(in->flood_wait_seconds));
  q2c_set(&o, "rollSpeed", qa_save_value_number(in->roll_speed));
  q2c_set(&o, "rollAngle", qa_save_value_number(in->roll_angle));
  q2c_set(&o, "runPitch", qa_save_value_number(in->run_pitch));
  q2c_set(&o, "runRoll", qa_save_value_number(in->run_roll));
  q2c_set(&o, "bobUp", qa_save_value_number(in->bob_up));
  q2c_set(&o, "bobPitch", qa_save_value_number(in->bob_pitch));
  q2c_set(&o, "bobRoll", qa_save_value_number(in->bob_roll));
  q2c_set(&o, "gunOffset", q2c_vector_value(in->gun_offset));
  return q2c_done(&o);
}

/* ---- Q2PlayerIntermissionCheckpoint ---- */
static qa_q2_base_player_intermission_checkpoint read_intermission(const qa_save_reader *reader) {
  static const char *const kinds[] = {"playing", "intermission"};
  qa_q2_base_player_intermission_checkpoint out;
  memset(&out, 0, sizeof out);
  out.kind = q2c_choice(reader, "kind", kinds, 2);
  if (strcmp(out.kind, "playing") == 0) return out;
  out.map = q2c_string(reader, "map");
  out.started = q2c_number(reader, "started");
  out.exit = q2c_boolean(reader, "exit");
  qa_save_reader landmark = q2c_field(reader, "landmark");
  if (!qa_save_reader_is_null(&landmark)) {
    out.has_landmark = true;
    out.landmark.player = q2c_actor(&landmark, "player");
    out.landmark.name = q2c_string(&landmark, "name");
    out.landmark.relative_origin = q2c_vector(&landmark, "relativeOrigin");
    out.landmark.relative_velocity = q2c_vector(&landmark, "relativeVelocity");
    out.landmark.relative_view_angles = q2c_vector(&landmark, "relativeViewAngles");
  }
  qa_save_reader_destroy(&landmark);
  return out;
}
static qa_save_value *intermission_value(const qa_q2_base_player_intermission_checkpoint *in) {
  q2c_object o = q2c_object_new();
  bool intermission = in->kind != NULL && strcmp(in->kind, "intermission") == 0;
  q2c_set(&o, "kind", qa_save_value_string(intermission ? "intermission" : "playing"));
  if (!intermission) return q2c_done(&o);
  qa_save_value *landmark = qa_save_value_null();
  if (in->has_landmark) {
    /* { ...landmark, player: saved }: the carry literal puts player first. */
    q2c_object l = q2c_object_new();
    q2c_set(&l, "player", q2c_actor_value(in->landmark.player));
    q2c_set(&l, "name", q2c_string_value(in->landmark.name));
    q2c_set(&l, "relativeOrigin", q2c_vector_value(in->landmark.relative_origin));
    q2c_set(&l, "relativeVelocity", q2c_vector_value(in->landmark.relative_velocity));
    q2c_set(&l, "relativeViewAngles", q2c_vector_value(in->landmark.relative_view_angles));
    qa_save_value_free(landmark);
    landmark = q2c_done(&l);
  }
  q2c_set(&o, "map", q2c_string_value(in->map));
  if (in->landmark_first) q2c_set(&o, "landmark", landmark);
  q2c_set(&o, "started", qa_save_value_number(in->started));
  q2c_set(&o, "exit", qa_save_value_boolean(in->exit));
  if (!in->landmark_first) q2c_set(&o, "landmark", landmark);
  return q2c_done(&o);
}

/* ---- Q2PlayerStateCheckpoint (Q2PlayerState class field order) ---- */
static void copy_text(char *dst, size_t cap, const char *src) { (void)snprintf(dst, cap, "%s", src != NULL ? src : ""); }
qa_q2_base_player_state_checkpoint qa_read_q2_player_state_checkpoint(const qa_save_reader *reader,
                                                                      qa_persist_decoded *owner) {
  static const char *const genders[] = {"male", "female", "neutral"};
  static const char *const hands[] = {"right", "left", "center"};
  qa_q2_base_player_state_checkpoint out;
  memset(&out, 0, sizeof out);
  out.slot = q2c_integer(reader, "slot", 0);
  out.entered_at = q2c_number(reader, "enteredAt");
  out.use_q2_weapons = q2c_boolean(reader, "useQ2Weapons");
  out.use_q2_inventory = q2c_boolean(reader, "useQ2Inventory");
  out.spawn_inventory = read_inventory(reader, "spawnInventory", &out.spawn_inventory_count, owner);
  copy_text(out.userinfo, sizeof out.userinfo, q2c_string(reader, "userinfo"));
  copy_text(out.name, sizeof out.name, q2c_string(reader, "name"));
  copy_text(out.skin, sizeof out.skin, q2c_string(reader, "skin"));
  out.gender = q2c_choice(reader, "gender", genders, 3);
  out.fov = q2c_number(reader, "fov");
  out.hand = q2c_choice(reader, "hand", hands, 3);
  out.spectator = q2c_boolean(reader, "spectator");
  out.requested_spectator = q2c_boolean(reader, "requestedSpectator");
  out.connected = q2c_boolean(reader, "connected");
  out.dead = q2c_boolean(reader, "dead");
  out.gibbed = q2c_boolean(reader, "gibbed");
  out.noclip = q2c_boolean(reader, "noclip");
  out.god = q2c_boolean(reader, "god");
  out.notarget = q2c_boolean(reader, "notarget");
  out.score = q2c_number(reader, "score");
  out.ping = q2c_number(reader, "ping");
  out.respawn_time = q2c_number(reader, "respawnTime");
  out.air_finished = q2c_number(reader, "airFinished");
  out.next_drown_time = q2c_number(reader, "nextDrownTime");
  out.drown_damage = q2c_number(reader, "drownDamage");
  out.old_water_level = q2c_number(reader, "oldWaterLevel");
  out.breather_sound = q2c_number(reader, "breatherSound");
  out.pain_debounce = q2c_number(reader, "painDebounce");
  out.damage_blood = q2c_number(reader, "damageBlood");
  out.damage_armor = q2c_number(reader, "damageArmor");
  out.damage_power_armor = q2c_number(reader, "damagePowerArmor");
  out.damage_knockback = q2c_number(reader, "damageKnockback");
  out.damage_from = q2c_vector(reader, "damageFrom");
  out.damage_blend = q2c_vector(reader, "damageBlend");
  out.damage_alpha = q2c_number(reader, "damageAlpha");
  out.bonus_alpha = q2c_number(reader, "bonusAlpha");
  out.damage_pitch = q2c_number(reader, "damagePitch");
  out.damage_roll = q2c_number(reader, "damageRoll");
  out.damage_time = q2c_number(reader, "damageTime");
  out.power_armor_time = q2c_number(reader, "powerArmorTime");
  out.fall_time = q2c_number(reader, "fallTime");
  out.fall_value = q2c_number(reader, "fallValue");
  out.landmark_free_fall = q2c_boolean(reader, "landmarkFreeFall");
  out.landmark_noise_time = q2c_number(reader, "landmarkNoiseTime");
  out.old_velocity = q2c_vector(reader, "oldVelocity");
  out.old_view_angles = q2c_vector(reader, "oldViewAngles");
  out.killer_yaw = q2c_number(reader, "killerYaw");
  out.buttons = q2c_number(reader, "buttons");
  out.latched_buttons = q2c_number(reader, "latchedButtons");
  out.weapon_thunk = q2c_boolean(reader, "weaponThunk");
  out.bob_time = q2c_number(reader, "bobTime");
  out.bob_move = q2c_number(reader, "bobMove");
  out.event = q2c_string(reader, "event");
  out.animation_priority = q2c_number(reader, "animationPriority");
  out.animation_end = q2c_number(reader, "animationEnd");
  out.animation_duck = q2c_boolean(reader, "animationDuck");
  out.animation_run = q2c_boolean(reader, "animationRun");
  out.loop_sound = q2c_string(reader, "loopSound");
  out.selected_item = read_nullable_namespaced(reader, "selectedItem");
  out.jump_held = false; /* TS: not read; a restored player starts released */
  out.show_scores = q2c_boolean(reader, "showScores");
  out.show_inventory = q2c_boolean(reader, "showInventory");
  out.show_help = q2c_boolean(reader, "showHelp");
  out.help_changed = q2c_present(reader, "helpChanged") ? q2c_number(reader, "helpChanged") : 0;
  out.game_help_changed = q2c_present(reader, "gameHelpChanged") ? q2c_number(reader, "gameHelpChanged") : 0;
  out.has_chase_target = q2c_nullable_actor(reader, "chaseTarget", &out.chase_target);
  qa_save_reader coop = q2c_field(reader, "coopRespawn");
  if (!qa_save_reader_is_null(&coop)) { out.has_coop_respawn = true; out.coop_respawn = read_carry(&coop, owner); }
  qa_save_reader_destroy(&coop);
  qa_save_reader flood = q2c_field(reader, "floodTimes");
  out.flood_time_count = qa_save_reader_list_count(&flood);
  out.flood_times = qa_persist_decoded_array(owner, out.flood_time_count, sizeof *out.flood_times);
  for (size_t i = 0; i < out.flood_time_count; i++) {
    qa_save_reader it = q2c_item(&flood, i);
    out.flood_times[i] = qa_save_reader_number(&it);
    qa_save_reader_destroy(&it);
  }
  qa_save_reader_destroy(&flood);
  out.flood_lock_until = q2c_number(reader, "floodLockUntil");
  return out;
}
qa_save_value *qa_q2_player_state_checkpoint_value(const qa_q2_base_player_state_checkpoint *in) {
  q2c_object o = q2c_object_new();
  q2c_set(&o, "slot", qa_save_value_number(in->slot));
  q2c_set(&o, "enteredAt", qa_save_value_number(in->entered_at));
  q2c_set(&o, "useQ2Weapons", qa_save_value_boolean(in->use_q2_weapons));
  q2c_set(&o, "useQ2Inventory", qa_save_value_boolean(in->use_q2_inventory));
  q2c_set(&o, "spawnInventory", inventory_value(in->spawn_inventory, in->spawn_inventory_count));
  q2c_set(&o, "userinfo", q2c_string_value(in->userinfo));
  q2c_set(&o, "name", q2c_string_value(in->name));
  q2c_set(&o, "skin", q2c_string_value(in->skin));
  q2c_set(&o, "gender", q2c_string_value(in->gender));
  q2c_set(&o, "fov", qa_save_value_number(in->fov));
  q2c_set(&o, "hand", q2c_string_value(in->hand));
  q2c_set(&o, "spectator", qa_save_value_boolean(in->spectator));
  q2c_set(&o, "requestedSpectator", qa_save_value_boolean(in->requested_spectator));
  q2c_set(&o, "connected", qa_save_value_boolean(in->connected));
  q2c_set(&o, "dead", qa_save_value_boolean(in->dead));
  q2c_set(&o, "gibbed", qa_save_value_boolean(in->gibbed));
  q2c_set(&o, "noclip", qa_save_value_boolean(in->noclip));
  q2c_set(&o, "god", qa_save_value_boolean(in->god));
  q2c_set(&o, "notarget", qa_save_value_boolean(in->notarget));
  q2c_set(&o, "score", qa_save_value_number(in->score));
  q2c_set(&o, "ping", qa_save_value_number(in->ping));
  q2c_set(&o, "respawnTime", qa_save_value_number(in->respawn_time));
  q2c_set(&o, "airFinished", qa_save_value_number(in->air_finished));
  q2c_set(&o, "nextDrownTime", qa_save_value_number(in->next_drown_time));
  q2c_set(&o, "drownDamage", qa_save_value_number(in->drown_damage));
  q2c_set(&o, "oldWaterLevel", qa_save_value_number(in->old_water_level));
  q2c_set(&o, "breatherSound", qa_save_value_number(in->breather_sound));
  q2c_set(&o, "painDebounce", qa_save_value_number(in->pain_debounce));
  q2c_set(&o, "damageBlood", qa_save_value_number(in->damage_blood));
  q2c_set(&o, "damageArmor", qa_save_value_number(in->damage_armor));
  q2c_set(&o, "damagePowerArmor", qa_save_value_number(in->damage_power_armor));
  q2c_set(&o, "damageKnockback", qa_save_value_number(in->damage_knockback));
  q2c_set(&o, "damageFrom", q2c_vector_value(in->damage_from));
  q2c_set(&o, "damageBlend", q2c_vector_value(in->damage_blend));
  q2c_set(&o, "damageAlpha", qa_save_value_number(in->damage_alpha));
  q2c_set(&o, "bonusAlpha", qa_save_value_number(in->bonus_alpha));
  q2c_set(&o, "damagePitch", qa_save_value_number(in->damage_pitch));
  q2c_set(&o, "damageRoll", qa_save_value_number(in->damage_roll));
  q2c_set(&o, "damageTime", qa_save_value_number(in->damage_time));
  q2c_set(&o, "powerArmorTime", qa_save_value_number(in->power_armor_time));
  q2c_set(&o, "fallTime", qa_save_value_number(in->fall_time));
  q2c_set(&o, "fallValue", qa_save_value_number(in->fall_value));
  q2c_set(&o, "helpChanged", qa_save_value_number(in->help_changed));
  q2c_set(&o, "gameHelpChanged", qa_save_value_number(in->game_help_changed));
  q2c_set(&o, "landmarkFreeFall", qa_save_value_boolean(in->landmark_free_fall));
  q2c_set(&o, "landmarkNoiseTime", qa_save_value_number(in->landmark_noise_time));
  q2c_set(&o, "oldVelocity", q2c_vector_value(in->old_velocity));
  q2c_set(&o, "oldViewAngles", q2c_vector_value(in->old_view_angles));
  q2c_set(&o, "killerYaw", qa_save_value_number(in->killer_yaw));
  q2c_set(&o, "buttons", qa_save_value_number(in->buttons));
  q2c_set(&o, "latchedButtons", qa_save_value_number(in->latched_buttons));
  q2c_set(&o, "weaponThunk", qa_save_value_boolean(in->weapon_thunk));
  q2c_set(&o, "bobTime", qa_save_value_number(in->bob_time));
  q2c_set(&o, "bobMove", qa_save_value_number(in->bob_move));
  q2c_set(&o, "event", q2c_string_value(in->event));
  q2c_set(&o, "animationPriority", qa_save_value_number(in->animation_priority));
  q2c_set(&o, "animationEnd", qa_save_value_number(in->animation_end));
  q2c_set(&o, "animationDuck", qa_save_value_boolean(in->animation_duck));
  q2c_set(&o, "animationRun", qa_save_value_boolean(in->animation_run));
  q2c_set(&o, "loopSound", q2c_string_value(in->loop_sound));
  q2c_set(&o, "selectedItem", nullable_string_value(in->selected_item));
  q2c_set(&o, "jumpHeld", qa_save_value_boolean(in->jump_held));
  q2c_set(&o, "showScores", qa_save_value_boolean(in->show_scores));
  q2c_set(&o, "showInventory", qa_save_value_boolean(in->show_inventory));
  q2c_set(&o, "showHelp", qa_save_value_boolean(in->show_help));
  q2c_set(&o, "chaseTarget", q2c_nullable_actor_value(in->has_chase_target, in->chase_target));
  q2c_set(&o, "coopRespawn", in->has_coop_respawn ? carry_value(&in->coop_respawn) : qa_save_value_null());
  qa_save_value *flood = qa_save_value_array();
  for (size_t i = 0; flood != NULL && i < in->flood_time_count; i++)
    if (!q2c_push(flood, qa_save_value_number(in->flood_times[i]))) { qa_save_value_free(flood); flood = NULL; }
  q2c_set(&o, "floodTimes", flood);
  q2c_set(&o, "floodLockUntil", qa_save_value_number(in->flood_lock_until));
  return q2c_done(&o);
}

/* ---- Q2PlayersCheckpoint (Q2Players.capture literal order) ---- */
qa_q2_base_players_checkpoint qa_read_q2_players_checkpoint(const qa_save_reader *reader, qa_persist_decoded *owner) {
  qa_q2_base_players_checkpoint out;
  memset(&out, 0, sizeof out);
  qa_save_reader version = q2c_field(reader, "version");
  out.version = qa_save_reader_literal_number(&version, 1);
  qa_save_reader_destroy(&version);
  out.corpse_index = q2c_integer(reader, "corpseIndex", 0);
  out.death_animation = q2c_number(reader, "deathAnimation");
  out.pain_animation = q2c_number(reader, "painAnimation");
  qa_save_reader rules = q2c_field(reader, "rules");
  out.rules = read_rules(&rules, owner);
  qa_save_reader_destroy(&rules);
  qa_save_reader intermission = q2c_field(reader, "intermission");
  out.intermission = read_intermission(&intermission);
  qa_save_reader_destroy(&intermission);
  qa_save_reader list = q2c_field(reader, "players");
  out.player_count = qa_save_reader_list_count(&list);
  out.players = qa_persist_decoded_array(owner, out.player_count, sizeof *out.players);
  for (size_t i = 0; i < out.player_count; i++) {
    qa_save_reader it = q2c_item(&list, i);
    out.players[i].actor = q2c_actor(&it, "actor");
    qa_save_reader state = q2c_field(&it, "state");
    out.players[i].state = qa_read_q2_player_state_checkpoint(&state, owner);
    qa_save_reader_destroy(&state);
    qa_save_reader_destroy(&it);
  }
  qa_save_reader_destroy(&list);
  return out;
}
static qa_save_value *players_value(const qa_q2_base_players_checkpoint *in) {
  q2c_object o = q2c_object_new();
  q2c_set(&o, "version", qa_save_value_number(in->version));
  q2c_set(&o, "corpseIndex", qa_save_value_number(in->corpse_index));
  q2c_set(&o, "deathAnimation", qa_save_value_number(in->death_animation));
  q2c_set(&o, "painAnimation", qa_save_value_number(in->pain_animation));
  q2c_set(&o, "rules", rules_value(&in->rules));
  q2c_set(&o, "intermission", intermission_value(&in->intermission));
  qa_save_value *players = qa_save_value_array();
  for (size_t i = 0; players != NULL && i < in->player_count; i++) {
    q2c_object entry = q2c_object_new();
    q2c_set(&entry, "actor", q2c_actor_value(in->players[i].actor));
    q2c_set(&entry, "state", qa_q2_player_state_checkpoint_value(&in->players[i].state));
    if (!q2c_push(players, q2c_done(&entry))) { qa_save_value_free(players); players = NULL; }
  }
  q2c_set(&o, "players", players);
  return q2c_done(&o);
}
uint8_t *qa_encode_q2_players_checkpoint(const qa_q2_base_players_checkpoint *checkpoint, size_t *out_length) {
  return q2c_encode(players_value(checkpoint), out_length);
}
bool qa_decode_q2_players_checkpoint(const uint8_t *bytes, size_t length, qa_q2_base_players_checkpoint *out,
                                     qa_persist_decoded *owner) {
  qa_save_reader reader;
  if (!q2c_open(bytes, length, "q2-players", owner, &reader)) return false;
  *out = qa_read_q2_players_checkpoint(&reader, owner);
  qa_save_reader_destroy(&reader);
  return true;
}

/* ---- Q2CharacterCheckpoint (Q2CharacterActor.capture literal order) ---- */
qa_q2_base_character_checkpoint qa_read_q2_character_checkpoint(const qa_save_reader *reader, qa_persist_decoded *owner) {
  qa_q2_base_character_checkpoint out;
  memset(&out, 0, sizeof out);
  qa_save_reader version = q2c_field(reader, "version");
  out.version = qa_save_reader_literal_number(&version, 1);
  qa_save_reader_destroy(&version);
  out.pain_index = q2c_number(reader, "painIndex");
  out.death_index = q2c_number(reader, "deathIndex");
  qa_save_reader state = q2c_field(reader, "state");
  out.state = qa_read_q2_player_state_checkpoint(&state, owner);
  qa_save_reader_destroy(&state);
  qa_save_reader rules = q2c_field(reader, "rules");
  out.rules = read_rules(&rules, owner);
  qa_save_reader_destroy(&rules);
  qa_save_reader attack = q2c_field(reader, "lastAttack");
  if (!qa_save_reader_is_null(&attack)) { out.has_last_attack = true; out.last_attack = qa_read_q2_attack_checkpoint(&attack); }
  qa_save_reader_destroy(&attack);
  qa_save_reader entity = q2c_field(reader, "entity");
  out.entity.model = q2c_string(&entity, "model");
  out.entity.model2 = q2c_string(&entity, "model2");
  out.entity.model3 = q2c_string(&entity, "model3");
  out.entity.model4 = q2c_string(&entity, "model4");
  out.entity.skin = q2c_number(&entity, "skin");
  out.entity.frame = q2c_number(&entity, "frame");
  out.entity.old_frame = q2c_number(&entity, "oldFrame");
  out.entity.scale = q2c_number(&entity, "scale");
  out.entity.effects = q2c_number(&entity, "effects");
  out.entity.render_flags = q2c_number(&entity, "renderFlags");
  out.entity.flags = q2c_number(&entity, "flags");
  out.entity.server_flags = q2c_number(&entity, "serverFlags");
  out.entity.view_height = q2c_number(&entity, "viewHeight");
  out.entity.max_health = q2c_number(&entity, "maxHealth");
  out.entity.sound = q2c_string(&entity, "sound");
  out.entity.visible = q2c_boolean(&entity, "visible");
  qa_save_reader_destroy(&entity);
  return out;
}
static qa_save_value *character_value(const qa_q2_base_character_checkpoint *in) {
  q2c_object entity = q2c_object_new();
  q2c_set(&entity, "model", q2c_string_value(in->entity.model));
  q2c_set(&entity, "model2", q2c_string_value(in->entity.model2));
  q2c_set(&entity, "model3", q2c_string_value(in->entity.model3));
  q2c_set(&entity, "model4", q2c_string_value(in->entity.model4));
  q2c_set(&entity, "skin", qa_save_value_number(in->entity.skin));
  q2c_set(&entity, "frame", qa_save_value_number(in->entity.frame));
  q2c_set(&entity, "oldFrame", qa_save_value_number(in->entity.old_frame));
  q2c_set(&entity, "scale", qa_save_value_number(in->entity.scale));
  q2c_set(&entity, "effects", qa_save_value_number(in->entity.effects));
  q2c_set(&entity, "renderFlags", qa_save_value_number(in->entity.render_flags));
  q2c_set(&entity, "flags", qa_save_value_number(in->entity.flags));
  q2c_set(&entity, "serverFlags", qa_save_value_number(in->entity.server_flags));
  q2c_set(&entity, "viewHeight", qa_save_value_number(in->entity.view_height));
  q2c_set(&entity, "maxHealth", qa_save_value_number(in->entity.max_health));
  q2c_set(&entity, "sound", q2c_string_value(in->entity.sound));
  q2c_set(&entity, "visible", qa_save_value_boolean(in->entity.visible));
  q2c_object o = q2c_object_new();
  q2c_set(&o, "version", qa_save_value_number(in->version));
  q2c_set(&o, "painIndex", qa_save_value_number(in->pain_index));
  q2c_set(&o, "deathIndex", qa_save_value_number(in->death_index));
  q2c_set(&o, "state", qa_q2_player_state_checkpoint_value(&in->state));
  q2c_set(&o, "rules", rules_value(&in->rules));
  q2c_set(&o, "entity", q2c_done(&entity));
  q2c_set(&o, "lastAttack", in->has_last_attack ? qa_q2_attack_checkpoint_value(&in->last_attack) : qa_save_value_null());
  return q2c_done(&o);
}
uint8_t *qa_encode_q2_character_checkpoint(const qa_q2_base_character_checkpoint *checkpoint, size_t *out_length) {
  return q2c_encode(character_value(checkpoint), out_length);
}
bool qa_decode_q2_character_checkpoint(const uint8_t *bytes, size_t length, qa_q2_base_character_checkpoint *out,
                                       qa_persist_decoded *owner) {
  qa_save_reader reader;
  if (!q2c_open(bytes, length, "q2-character", owner, &reader)) return false;
  *out = qa_read_q2_character_checkpoint(&reader, owner);
  qa_save_reader_destroy(&reader);
  return true;
}
