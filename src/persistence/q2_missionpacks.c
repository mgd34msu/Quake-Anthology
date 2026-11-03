/* Port of src/persistence/q2-missionpacks.ts. GPL-2.0-or-later. */
#include "qa/persistence/q2_missionpacks.h"

#include <stdlib.h>
#include <string.h>

#include "q2_codec_internal.h"

/* ---- value builders (TS encodeCheckpointValue of the captured object) ---- */
static bool set_own(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL) return false;
  if (!qa_save_value_set(object, key, child)) { qa_save_value_free(child); return false; }
  return true;
}
/* TS decode*(bytes): readX(new SaveReader(decodeCheckpointValue(bytes), path)). */
typedef bool (*read_fn)(const qa_save_reader *reader, void *out);
static bool decode_with(const uint8_t *bytes, size_t length, const char *path, read_fn read, void *out) {
  qa_save_value *root = qa_decode_checkpoint_value(bytes, length);
  if (root == NULL) return false;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, root, path)) { qa_save_value_free(root); return false; }
  bool ok = read(&reader, out);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(root);
  return ok;
}

/* ---- Q2MissionPackItemsCheckpoint ---- */
bool qa_read_q2_mission_pack_items_checkpoint(const qa_save_reader *reader, qa_q2mp_items_checkpoint *out) {
  memset(out, 0, sizeof *out);
  qa_save_reader powers = q2c_field(reader, "powers");
  size_t count = qa_save_reader_list_count(&powers);
  if (count != 0) {
    out->powers = calloc(count, sizeof *out->powers);
    if (out->powers == NULL) { qa_save_reader_destroy(&powers); return false; }
  }
  for (size_t i = 0; i < count; i++) {
    qa_save_reader value = q2c_item(&powers, i);
    qa_q2mp_item_power_checkpoint *entry = &out->powers[i];
    entry->actor = q2c_actor(&value, "actor");
    qa_save_reader state = q2c_field(&value, "state");
    entry->state.quad_fire_until = q2c_number(&state, "quadFireUntil");
    entry->state.double_until = q2c_number(&state, "doubleUntil");
    entry->state.ir_until = q2c_number(&state, "irUntil");
    qa_save_reader_destroy(&state);
    qa_save_reader_destroy(&value);
    out->power_count = i + 1;
  }
  qa_save_reader_destroy(&powers);
  return true;
}
/* TS Q2MissionPackItems.capture: { powers: [{ actor, state: { quadFireUntil, doubleUntil, irUntil } }] }. */
static qa_save_value *items_value(const qa_q2mp_items_checkpoint *checkpoint) {
  qa_save_value *obj = qa_save_value_object(), *powers = qa_save_value_array();
  if (obj == NULL || powers == NULL) goto fail;
  for (size_t i = 0; i < checkpoint->power_count; i++) {
    const qa_q2mp_item_power_checkpoint *entry = &checkpoint->powers[i];
    qa_save_value *power = qa_save_value_object(), *state = qa_save_value_object();
    if (power == NULL || state == NULL || !set_own(state, "quadFireUntil", qa_save_value_number(entry->state.quad_fire_until)) ||
        !set_own(state, "doubleUntil", qa_save_value_number(entry->state.double_until)) ||
        !set_own(state, "irUntil", qa_save_value_number(entry->state.ir_until)) ||
        !set_own(power, "actor", q2c_actor_value(entry->actor))) {
      qa_save_value_free(power); qa_save_value_free(state);
      goto fail;
    }
    if (!set_own(power, "state", state)) { qa_save_value_free(power); goto fail; }
    if (!q2c_push(powers, power)) goto fail;
  }
  if (!set_own(obj, "powers", powers)) { powers = NULL; goto fail; }
  return obj;
fail:
  qa_save_value_free(powers);
  qa_save_value_free(obj);
  return NULL;
}
uint8_t *qa_encode_q2_mission_pack_items_checkpoint(const qa_q2mp_items_checkpoint *checkpoint, size_t *out_length) {
  return q2c_encode(items_value(checkpoint), out_length);
}
static bool read_items(const qa_save_reader *reader, void *out) {
  if (qa_read_q2_mission_pack_items_checkpoint(reader, out)) return true;
  qa_q2mp_items_checkpoint_free(out);
  return false;
}
bool qa_decode_q2_mission_pack_items_checkpoint(const uint8_t *bytes, size_t length, qa_q2mp_items_checkpoint *out) {
  memset(out, 0, sizeof *out);
  return decode_with(bytes, length, "q2-missionpack-items", read_items, out);
}

/* ---- Q2MissionPackMonstersCheckpoint ---- */
static bool read_monster_state(const qa_save_reader *state, qa_q2mpm_saved_state *out) {
  out->blocked = q2c_boolean(state, "blocked");
  out->turret_orientation = q2c_number(state, "turretOrientation");
  out->has_healer = q2c_nullable_actor(state, "healer", &out->healer);
  out->has_bad_medic1 = q2c_nullable_actor(state, "badMedic1", &out->bad_medic1);
  out->has_bad_medic2 = q2c_nullable_actor(state, "badMedic2", &out->bad_medic2);
  out->medic_tries = q2c_number(state, "medicTries");
  qa_save_reader list = q2c_field(state, "chosenReinforcements");
  size_t count = qa_save_reader_list_count(&list);
  if (count != 0) {
    out->chosen_reinforcements = calloc(count, sizeof *out->chosen_reinforcements);
    if (out->chosen_reinforcements == NULL) { qa_save_reader_destroy(&list); return false; }
  }
  for (size_t i = 0; i < count; i++) {
    qa_save_reader value = q2c_item(&list, i);
    double index = qa_save_reader_integer_min(&value, 0);
    if (index >= 255) qa_save_reader_fail(&value, "expected a reinforcement index below 255");
    out->chosen_reinforcements[i] = index;
    out->reinforcement_count = i + 1;
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&list);
  qa_save_reader react = q2c_field(state, "reactToDamageTime");
  out->react_to_damage_time = qa_save_reader_finite(&react);
  qa_save_reader_destroy(&react);
  out->summon_strength = q2c_number(state, "summonStrength");
  out->has_last_player_enemy = q2c_nullable_actor(state, "lastPlayerEnemy", &out->last_player_enemy);
  out->has_bad_area = q2c_nullable_actor(state, "badArea", &out->bad_area);
  out->good_guy = q2c_boolean(state, "goodGuy");
  out->widow_quad_until = q2c_number(state, "widowQuadUntil");
  out->widow_double_until = q2c_number(state, "widowDoubleUntil");
  out->widow_invulnerable_until = q2c_number(state, "widowInvulnerableUntil");
  return true;
}
bool qa_read_q2_mission_pack_monsters_checkpoint(const qa_save_reader *reader, qa_q2mpm_monsters_checkpoint *out) {
  memset(out, 0, sizeof *out);
  qa_save_reader f = q2c_field(reader, "version");
  out->version = qa_save_reader_literal_number(&f, 1);
  qa_save_reader_destroy(&f);
  f = q2c_field(reader, "flyerNextMove");
  static const char *const moves[] = {"none", "run"};
  out->flyer_next_move = strcmp(qa_save_reader_choice_string(&f, moves, 2), "run") == 0 ? QA_Q2MPM_FLYER_RUN : QA_Q2MPM_FLYER_NONE;
  qa_save_reader_destroy(&f);
  f = q2c_field(reader, "hints");
  if (!qa_save_reader_is_null(&f)) {
    if (!qa_q2mpm_decode_hints_checkpoint(&f, &out->hints)) { qa_save_reader_destroy(&f); return false; }
    out->has_hints = true;
  }
  qa_save_reader_destroy(&f);
  out->widow_shots_fired = q2c_number(reader, "widowShotsFired");
  f = q2c_field(reader, "widowDamageMultiplier");
  static const double multipliers[] = {1, 2, 4};
  out->widow_damage_multiplier = qa_save_reader_choice_number(&f, multipliers, 3);
  qa_save_reader_destroy(&f);
  qa_save_reader actors = q2c_field(reader, "actors");
  size_t count = qa_save_reader_list_count(&actors);
  if (count != 0) {
    out->actors = calloc(count, sizeof *out->actors);
    if (out->actors == NULL) { qa_save_reader_destroy(&actors); return false; }
  }
  for (size_t i = 0; i < count; i++) {
    qa_save_reader value = q2c_item(&actors, i);
    out->actors[i].actor = q2c_actor(&value, "actor");
    qa_save_reader state = q2c_field(&value, "state");
    out->actor_count = i + 1;
    bool ok = read_monster_state(&state, &out->actors[i].state);
    qa_save_reader_destroy(&state);
    qa_save_reader_destroy(&value);
    if (!ok) { qa_save_reader_destroy(&actors); return false; }
  }
  qa_save_reader_destroy(&actors);
  return true;
}
/* TS Q2RogueHints.capture: { version, present, starts, nodes: [{ actor, chain, next }],
 * monsters: [{ actor, goal, lastTime }] }. */
static qa_save_value *hints_value(const qa_q2mpm_hints_checkpoint *hints) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "version", qa_save_value_number(hints->version)) ||
      !set_own(obj, "present", qa_save_value_boolean(hints->present))) goto fail;
  qa_save_value *starts = qa_save_value_array();
  if (!set_own(obj, "starts", starts)) goto fail;
  for (size_t i = 0; i < hints->start_count; i++)
    if (!q2c_push(starts, q2c_actor_value(hints->starts[i]))) goto fail;
  qa_save_value *nodes = qa_save_value_array();
  if (!set_own(obj, "nodes", nodes)) goto fail;
  for (size_t i = 0; i < hints->node_count; i++) {
    const qa_q2mpm_hint_node_checkpoint *node = &hints->nodes[i];
    qa_save_value *value = qa_save_value_object();
    if (!q2c_push(nodes, value)) goto fail;
    if (!set_own(value, "actor", q2c_actor_value(node->actor)) || !set_own(value, "chain", qa_save_value_number(node->chain)) ||
        !set_own(value, "next", q2c_nullable_actor_value(node->has_next, node->next))) goto fail;
  }
  qa_save_value *monsters = qa_save_value_array();
  if (!set_own(obj, "monsters", monsters)) goto fail;
  for (size_t i = 0; i < hints->monster_count; i++) {
    const qa_q2mpm_hint_monster_checkpoint *monster = &hints->monsters[i];
    qa_save_value *value = qa_save_value_object();
    if (!q2c_push(monsters, value)) goto fail;
    if (!set_own(value, "actor", q2c_actor_value(monster->actor)) ||
        !set_own(value, "goal", q2c_nullable_actor_value(monster->has_goal, monster->goal)) ||
        !set_own(value, "lastTime", qa_save_value_number(monster->last_time))) goto fail;
  }
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}
/* TS Q2MissionPackMonsterState.capture state: { ...RogueMonsterState } key order. */
static qa_save_value *monster_state_value(const qa_q2mpm_saved_state *state) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "blocked", qa_save_value_boolean(state->blocked)) ||
      !set_own(obj, "turretOrientation", qa_save_value_number(state->turret_orientation)) ||
      !set_own(obj, "healer", q2c_nullable_actor_value(state->has_healer, state->healer)) ||
      !set_own(obj, "badMedic1", q2c_nullable_actor_value(state->has_bad_medic1, state->bad_medic1)) ||
      !set_own(obj, "badMedic2", q2c_nullable_actor_value(state->has_bad_medic2, state->bad_medic2)) ||
      !set_own(obj, "medicTries", qa_save_value_number(state->medic_tries))) goto fail;
  qa_save_value *chosen = qa_save_value_array();
  if (!set_own(obj, "chosenReinforcements", chosen)) goto fail;
  for (size_t i = 0; i < state->reinforcement_count; i++)
    if (!q2c_push(chosen, qa_save_value_number(state->chosen_reinforcements[i]))) goto fail;
  if (!set_own(obj, "reactToDamageTime", qa_save_value_number(state->react_to_damage_time)) ||
      !set_own(obj, "summonStrength", qa_save_value_number(state->summon_strength)) ||
      !set_own(obj, "lastPlayerEnemy", q2c_nullable_actor_value(state->has_last_player_enemy, state->last_player_enemy)) ||
      !set_own(obj, "badArea", q2c_nullable_actor_value(state->has_bad_area, state->bad_area)) ||
      !set_own(obj, "goodGuy", qa_save_value_boolean(state->good_guy)) ||
      !set_own(obj, "widowQuadUntil", qa_save_value_number(state->widow_quad_until)) ||
      !set_own(obj, "widowDoubleUntil", qa_save_value_number(state->widow_double_until)) ||
      !set_own(obj, "widowInvulnerableUntil", qa_save_value_number(state->widow_invulnerable_until))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}
/* TS Q2MissionPackMonsters.capture: { ...source.capture(game), hints } keeps the source
 * capture's key order: version, actors, flyerNextMove, hints, widowShotsFired,
 * widowDamageMultiplier. */
static qa_save_value *monsters_value(const qa_q2mpm_monsters_checkpoint *checkpoint) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "version", qa_save_value_number(checkpoint->version))) goto fail;
  qa_save_value *actors = qa_save_value_array();
  if (!set_own(obj, "actors", actors)) goto fail;
  for (size_t i = 0; i < checkpoint->actor_count; i++) {
    qa_save_value *entry = qa_save_value_object();
    if (!q2c_push(actors, entry)) goto fail;
    if (!set_own(entry, "actor", q2c_actor_value(checkpoint->actors[i].actor)) ||
        !set_own(entry, "state", monster_state_value(&checkpoint->actors[i].state))) goto fail;
  }
  if (!set_own(obj, "flyerNextMove", qa_save_value_string(checkpoint->flyer_next_move == QA_Q2MPM_FLYER_RUN ? "run" : "none")) ||
      !set_own(obj, "hints", checkpoint->has_hints ? hints_value(&checkpoint->hints) : qa_save_value_null()) ||
      !set_own(obj, "widowShotsFired", qa_save_value_number(checkpoint->widow_shots_fired)) ||
      !set_own(obj, "widowDamageMultiplier", qa_save_value_number(checkpoint->widow_damage_multiplier))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}
uint8_t *qa_encode_q2_mission_pack_monsters_checkpoint(const qa_q2mpm_monsters_checkpoint *checkpoint, size_t *out_length) {
  return q2c_encode(monsters_value(checkpoint), out_length);
}
static bool read_monsters(const qa_save_reader *reader, void *out) {
  if (qa_read_q2_mission_pack_monsters_checkpoint(reader, out)) return true;
  qa_q2mpm_monsters_checkpoint_free(out);
  return false;
}
bool qa_decode_q2_mission_pack_monsters_checkpoint(const uint8_t *bytes, size_t length, qa_q2mpm_monsters_checkpoint *out) {
  memset(out, 0, sizeof *out);
  return decode_with(bytes, length, "q2-missionpack-monsters", read_monsters, out);
}

/* ---- Q2RogueEntitiesCheckpoint: { steamId } ---- */
bool qa_read_q2_rogue_entities_checkpoint(const qa_save_reader *reader, qa_q2mp_rogue_entities_checkpoint *out) {
  memset(out, 0, sizeof *out);
  out->steam_id = q2c_number(reader, "steamId");
  return true;
}
uint8_t *qa_encode_q2_rogue_entities_checkpoint(const qa_q2mp_rogue_entities_checkpoint *checkpoint, size_t *out_length) {
  qa_save_value *obj = qa_save_value_object();
  if (obj != NULL && !set_own(obj, "steamId", qa_save_value_number(checkpoint->steam_id))) { qa_save_value_free(obj); obj = NULL; }
  return q2c_encode(obj, out_length);
}
static bool read_rogue(const qa_save_reader *reader, void *out) { return qa_read_q2_rogue_entities_checkpoint(reader, out); }
bool qa_decode_q2_rogue_entities_checkpoint(const uint8_t *bytes, size_t length, qa_q2mp_rogue_entities_checkpoint *out) {
  memset(out, 0, sizeof *out);
  return decode_with(bytes, length, "q2-rogue-entities", read_rogue, out);
}

/* ---- Q2TagCheckpoint: { token, owner, count } ---- */
bool qa_read_q2_tag_checkpoint(const qa_save_reader *reader, qa_q2mp_tag_checkpoint *out) {
  memset(out, 0, sizeof *out);
  out->has_token = q2c_nullable_actor(reader, "token", &out->token);
  out->has_owner = q2c_nullable_actor(reader, "owner", &out->owner);
  out->count = q2c_number(reader, "count");
  return true;
}
uint8_t *qa_encode_q2_tag_checkpoint(const qa_q2mp_tag_checkpoint *checkpoint, size_t *out_length) {
  qa_save_value *obj = qa_save_value_object();
  if (obj != NULL && (!set_own(obj, "token", q2c_nullable_actor_value(checkpoint->has_token, checkpoint->token)) ||
                      !set_own(obj, "owner", q2c_nullable_actor_value(checkpoint->has_owner, checkpoint->owner)) ||
                      !set_own(obj, "count", qa_save_value_number(checkpoint->count)))) {
    qa_save_value_free(obj);
    obj = NULL;
  }
  return q2c_encode(obj, out_length);
}
static bool read_tag(const qa_save_reader *reader, void *out) { return qa_read_q2_tag_checkpoint(reader, out); }
bool qa_decode_q2_tag_checkpoint(const uint8_t *bytes, size_t length, qa_q2mp_tag_checkpoint *out) {
  memset(out, 0, sizeof *out);
  return decode_with(bytes, length, "q2-tag", read_tag, out);
}

/* ---- Q2DeathBallCheckpoint: { ball, starts, team1Score, team2Score } ---- */
bool qa_read_q2_death_ball_checkpoint(const qa_save_reader *reader, qa_q2mp_deathball_checkpoint *out) {
  memset(out, 0, sizeof *out);
  out->has_ball = q2c_nullable_actor(reader, "ball", &out->ball);
  out->starts = q2c_number(reader, "starts");
  out->team1_score = q2c_number(reader, "team1Score");
  out->team2_score = q2c_number(reader, "team2Score");
  return true;
}
uint8_t *qa_encode_q2_death_ball_checkpoint(const qa_q2mp_deathball_checkpoint *checkpoint, size_t *out_length) {
  qa_save_value *obj = qa_save_value_object();
  if (obj != NULL && (!set_own(obj, "ball", q2c_nullable_actor_value(checkpoint->has_ball, checkpoint->ball)) ||
                      !set_own(obj, "starts", qa_save_value_number(checkpoint->starts)) ||
                      !set_own(obj, "team1Score", qa_save_value_number(checkpoint->team1_score)) ||
                      !set_own(obj, "team2Score", qa_save_value_number(checkpoint->team2_score)))) {
    qa_save_value_free(obj);
    obj = NULL;
  }
  return q2c_encode(obj, out_length);
}
static bool read_death_ball(const qa_save_reader *reader, void *out) { return qa_read_q2_death_ball_checkpoint(reader, out); }
bool qa_decode_q2_death_ball_checkpoint(const uint8_t *bytes, size_t length, qa_q2mp_deathball_checkpoint *out) {
  memset(out, 0, sizeof *out);
  return decode_with(bytes, length, "q2-deathball", read_death_ball, out);
}
