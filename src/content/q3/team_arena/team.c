/* Port of src/content/q3/team-arena/team.ts. GPL-2.0-or-later. */
#include "qa/content/q3/team_arena/team.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q3/base/game/format.h"
#include "qa/content/q3/base/game/ground.h"
#include "qa/content/q3/base/game/rankings.h"
#include "qa/content/q3/base/game/save_callbacks.h"
#include "qa/content/q3/base/game/save_module_values.h"
#include "qa/content/q3/base/game/utilities.h"
#include "qa/content/q3/base/shared/entity_shared.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"

enum { AWARDS = 0x8 | 0x40 | 0x800 | 0x8000 | 0x10000 | 0x20000 };
static double s32(double v) { return qa_js_to_int32(v); }
static double f32(double v) { return (float)v; }
static double imul(double a, double b) { return (int32_t)(qa_js_to_int32(a) * qa_js_to_int32(b)); }
static double slot_get(qa_q3_base_player_state_slots *s, double i) { return qa_q3_base_player_state_slots_get(s, (size_t)i); }
static void slot_set(qa_q3_base_player_state_slots *s, double i, double v) { qa_q3_base_player_state_slots_set(s, (size_t)i, v); }
static bool mission(qa_q3_ta_team_runtime *r) { return r->host.product == QA_Q3_BASE_PRODUCT_MISSIONPACK; }

void qa_q3_ta_team_game_state_init(qa_q3_ta_team_game_state *state) {
  memset(state, 0, sizeof *state);
  state->red_status = QA_Q3_TA_FLAG_AT_BASE;
  state->blue_status = QA_Q3_TA_FLAG_AT_BASE;
  state->flag_status = QA_Q3_TA_FLAG_AT_BASE;
}

qa_q3_base_team qa_q3_ta_other_team(qa_q3_base_team team) {
  return team == QA_Q3_BASE_TEAM_RED ? QA_Q3_BASE_TEAM_BLUE : team == QA_Q3_BASE_TEAM_BLUE ? QA_Q3_BASE_TEAM_RED : team;
}
const char *qa_q3_ta_team_name(qa_q3_base_team team) {
  return team == QA_Q3_BASE_TEAM_RED ? "RED" : team == QA_Q3_BASE_TEAM_BLUE ? "BLUE"
      : team == QA_Q3_BASE_TEAM_SPECTATOR ? "SPECTATOR" : "FREE";
}
const char *qa_q3_ta_other_team_name(qa_q3_base_team team) { return qa_q3_ta_team_name(qa_q3_ta_other_team(team)); }
bool qa_q3_ta_on_same_team(double game_type, qa_q3_game_entity *first, qa_q3_game_entity *second) {
  return first->client != NULL && second->client != NULL && game_type >= QA_Q3_BASE_GT_TEAM &&
      first->client->sess.session_team == second->client->sess.session_team;
}
void qa_q3_ta_spawn_team_point(qa_q3_game_entity *entity) { (void)entity; }

static qa_q3_game_client *client_of(qa_q3_game_entity *entity) {
  if (entity->client == NULL) qa_range_error("Team player operation requires a client");
  return entity->client;
}
/* TS flagTeam: returns false for TS null (non-flag item). */
static bool flag_team(qa_q3_game_entity *entity, qa_q3_base_team *out) {
  if (entity->item == NULL) qa_range_error("Team item operation requires an item");
  if (entity->item->tag == QA_Q3_PW_REDFLAG) { *out = QA_Q3_BASE_TEAM_RED; return true; }
  if (entity->item->tag == QA_Q3_PW_BLUEFLAG) { *out = QA_Q3_BASE_TEAM_BLUE; return true; }
  if (entity->item->tag == QA_Q3_PW_NEUTRALFLAG) { *out = QA_Q3_BASE_TEAM_FREE; return true; }
  return false;
}
/* TS ctfStatus: -1 yields the one-NUL-char string; buffer holds bytes+len. */
static void ctf_status(double status, char *out, size_t *out_len) {
  if (status == -1) { out[0] = '\0'; *out_len = 1; }
  else if (status == QA_Q3_TA_FLAG_AT_BASE) { out[0] = '0'; *out_len = 1; }
  else if (status == QA_Q3_TA_FLAG_TAKEN) { out[0] = '1'; *out_len = 1; }
  else if (status == QA_Q3_TA_FLAG_TAKEN_RED || status == QA_Q3_TA_FLAG_TAKEN_BLUE) { out[0] = '*'; *out_len = 1; }
  else if (status == QA_Q3_TA_FLAG_DROPPED) { out[0] = '2'; *out_len = 1; }
  else { out[0] = '\0'; *out_len = 0; } /* TS switch falls through to undefined */
}

void qa_q3_ta_team_runtime_init(qa_q3_ta_team_runtime *runtime, qa_q3_ta_team_host host) {
  if (host.team_scores->length != 4) qa_range_error("Team scores require four source team slots");
  runtime->host = host;
  qa_q3_ta_team_game_state_init(&runtime->state);
  runtime->neutral_obelisk = NULL;
  runtime->last_team_location_time = 0;
  qa_q3_ta_team_runtime_bind_save_callbacks(runtime);
}

qa_q3_ta_team_save_state qa_q3_ta_team_runtime_capture_save_state(qa_q3_ta_team_runtime *runtime) {
  qa_q3_ta_team_save_state out;
  out.state = runtime->state;
  out.has_neutral_obelisk = runtime->neutral_obelisk != NULL;
  out.neutral_obelisk_slot = runtime->neutral_obelisk ? runtime->neutral_obelisk->slot : 0;
  out.last_team_location_time = runtime->last_team_location_time;
  return out;
}

void qa_q3_ta_team_runtime_restore_save_state(qa_q3_ta_team_runtime *runtime, const qa_save_value *value) {
  qa_save_reader reader, state, field, neutral, time_reader;
  if (!qa_save_reader_init(&reader, value, "q3.team")) qa_range_error("out of memory");
  if (!qa_save_reader_field(&reader, "state", &state)) qa_range_error("out of memory");
  qa_q3_ta_team_game_state restored;
  static const double status_choices[] = {-1, 0, 1, 2, 3, 4};
  struct { const char *name; double *slot; bool choice; } fields[] = {
    {"lastFlagCapture", &restored.last_flag_capture, false},
    {"lastCaptureTeam", &restored.last_capture_team, false},
    {"redStatus", &restored.red_status, true}, {"blueStatus", &restored.blue_status, true},
    {"flagStatus", &restored.flag_status, true},
    {"redTakenTime", &restored.red_taken_time, false},
    {"blueTakenTime", &restored.blue_taken_time, false},
    {"redObeliskAttackedTime", &restored.red_obelisk_attacked_time, false},
    {"blueObeliskAttackedTime", &restored.blue_obelisk_attacked_time, false}};
  for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++) {
    if (!qa_save_reader_field(&state, fields[i].name, &field)) qa_range_error("out of memory");
    *fields[i].slot = fields[i].choice ? qa_save_reader_choice_number(&field, status_choices, 6)
                                       : qa_save_reader_integer(&field);
    qa_save_reader_destroy(&field);
  }
  qa_save_reader_destroy(&state);
  if (!qa_save_reader_field(&reader, "neutralObelisk", &neutral)) qa_range_error("out of memory");
  qa_q3_game_entity *neutral_entity = qa_save_reader_is_null(&neutral)
      ? NULL : qa_q3_read_module_entity(&neutral, runtime->host.pool);
  qa_save_reader_destroy(&neutral);
  if (!qa_save_reader_field(&reader, "lastTeamLocationTime", &time_reader)) qa_range_error("out of memory");
  double time = qa_save_reader_integer(&time_reader);
  qa_save_reader_destroy(&time_reader);
  qa_save_reader_destroy(&reader);
  runtime->state = restored;
  runtime->neutral_obelisk = neutral_entity;
  runtime->last_team_location_time = time;
}

void qa_q3_ta_team_runtime_init_game(qa_q3_ta_team_runtime *runtime) {
  qa_q3_ta_team_game_state_init(&runtime->state);
  if (runtime->host.game_type == QA_Q3_BASE_GT_CTF) {
    runtime->state.red_status = runtime->state.blue_status = -1;
    qa_q3_ta_team_runtime_set_flag_status(runtime, QA_Q3_BASE_TEAM_RED, QA_Q3_TA_FLAG_AT_BASE);
    qa_q3_ta_team_runtime_set_flag_status(runtime, QA_Q3_BASE_TEAM_BLUE, QA_Q3_TA_FLAG_AT_BASE);
  } else if (mission(runtime) && runtime->host.game_type == QA_Q3_BASE_GT_1FCTF) {
    runtime->state.flag_status = -1;
    qa_q3_ta_team_runtime_set_flag_status(runtime, QA_Q3_BASE_TEAM_FREE, QA_Q3_TA_FLAG_AT_BASE);
  }
}

void qa_q3_ta_team_runtime_print_message(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity, const char *text) {
  size_t length = strlen(text); /* C string already ends at TS first NUL */
  if (length > 1024) qa_range_error("PrintMsg overrun");
  char *message = malloc(length + 1);
  char *command = malloc(length + 16);
  if (!message || !command) { free(message); free(command); qa_range_error("out of memory"); }
  for (size_t i = 0; i < length; i++) message[i] = text[i] == '"' ? '\'' : text[i];
  message[length] = '\0';
  (void)snprintf(command, length + 16, "print \"%s\"", message);
  runtime->host.send_server_command(runtime->host.ctx, entity == NULL ? -1 : entity->slot, command);
  free(message);
  free(command);
}

void qa_q3_ta_team_runtime_add_team_score(qa_q3_ta_team_runtime *runtime, qa_vec3 origin, qa_q3_base_team team, double score) {
  qa_q3_game_entity *event = qa_q3_entity_pool_temp_entity(runtime->host.pool, origin, QA_Q3_EV_GLOBAL_TEAM_SOUND);
  event->r.sv_flags = (double)(qa_js_to_int32(event->r.sv_flags) | QA_Q3_BASE_SVF_BROADCAST);
  double red = slot_get(runtime->host.team_scores, QA_Q3_BASE_TEAM_RED);
  double blue = slot_get(runtime->host.team_scores, QA_Q3_BASE_TEAM_BLUE);
  if (team == QA_Q3_BASE_TEAM_RED)
    event->s.event_parm = s32(red + score) == blue ? QA_Q3_TA_GTS_TIED
        : (red <= blue && s32(red + score) > blue ? QA_Q3_TA_GTS_RED_TOOK_LEAD : QA_Q3_TA_GTS_RED_SCORED);
  else
    event->s.event_parm = s32(blue + score) == red ? QA_Q3_TA_GTS_TIED
        : (blue <= red && s32(blue + score) > red ? QA_Q3_TA_GTS_BLUE_TOOK_LEAD : QA_Q3_TA_GTS_BLUE_SCORED);
  slot_set(runtime->host.team_scores, team, slot_get(runtime->host.team_scores, team) + score);
}

void qa_q3_ta_team_runtime_set_flag_status(qa_q3_ta_team_runtime *runtime, qa_q3_base_team team, qa_q3_ta_flag_status status) {
  double *slot = team == QA_Q3_BASE_TEAM_RED ? &runtime->state.red_status
      : team == QA_Q3_BASE_TEAM_BLUE ? &runtime->state.blue_status
      : team == QA_Q3_BASE_TEAM_FREE ? &runtime->state.flag_status : NULL;
  if (slot == NULL || *slot == status) return;
  *slot = status;
  char value[8];
  if (runtime->host.game_type == QA_Q3_BASE_GT_CTF) {
    size_t len1, len2;
    ctf_status(runtime->state.red_status, value, &len1);
    ctf_status(runtime->state.blue_status, value + len1, &len2);
    value[len1 + len2] = '\0'; /* embedded NUL truncates like TS slice at NUL */
  } else {
    (void)snprintf(value, sizeof value, "%lld", (long long)runtime->state.flag_status);
  }
  runtime->host.set_configstring(runtime->host.ctx, 23, value);
}

void qa_q3_ta_team_runtime_check_dropped_item(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity) {
  qa_q3_base_team team;
  if (flag_team(entity, &team)) qa_q3_ta_team_runtime_set_flag_status(runtime, team, QA_Q3_TA_FLAG_DROPPED);
}

void qa_q3_ta_team_runtime_force_gesture(qa_q3_ta_team_runtime *runtime, qa_q3_base_team team) {
  for (int index = 0; index < QA_Q3_MAX_CLIENTS; index++) {
    qa_q3_game_entity *entity = qa_q3_entity_pool_at(runtime->host.pool, index);
    if (qa_q3_game_entity_inuse(entity) && entity->client != NULL && entity->client->sess.session_team == team)
      entity->flags = (double)(qa_js_to_int32(entity->flags) | QA_Q3_GAME_FLAG_FORCE_GESTURE);
  }
}

static void award(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *player, double flag) {
  qa_q3_game_client *client = client_of(player);
  client->ps.e_flags = (double)((qa_js_to_int32(client->ps.e_flags) & ~AWARDS) | qa_js_to_int32(flag));
  client->reward_time = s32(runtime->host.time + 2000);
}

void qa_q3_ta_team_runtime_check_hurt_carrier(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *target, qa_q3_game_entity *attacker) {
  if (target->client == NULL || attacker->client == NULL) return;
  double flag = target->client->sess.session_team == QA_Q3_BASE_TEAM_RED ? QA_Q3_PW_BLUEFLAG : QA_Q3_PW_REDFLAG;
  if ((slot_get(&target->client->ps.powerups, flag) != 0 ||
       target->client->ps.generic1 != 0) &&
      target->client->sess.session_team != attacker->client->sess.session_team)
    attacker->client->pers.team_state.last_hurt_carrier = f32(runtime->host.time);
}

static void print_formatted(qa_q3_ta_team_runtime *r, qa_q3_game_entity *entity, const char *format,
    const qa_q3_game_format_arg *args, size_t count) {
  char *text = qa_q3_game_format(format, args, count, 32000);
  qa_q3_ta_team_runtime_print_message(r, entity, text);
  free(text);
}

void qa_q3_ta_team_runtime_frag_bonuses(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *target, qa_q3_game_entity *attacker) {
  void * value10;
  qa_vec3 value11;
  qa_vec3 value12;
  void * value7;
  qa_vec3 value8;
  qa_vec3 value9;
  if (target->client == NULL || attacker == NULL || attacker->client == NULL || target == attacker ||
      qa_q3_ta_on_same_team(runtime->host.game_type, target, attacker)) return;
  qa_q3_game_client *victim = target->client, *killer = attacker->client;
  double team = victim->sess.session_team;
  qa_q3_base_team opposing = qa_q3_ta_other_team((qa_q3_base_team)team);
  double flag = team == QA_Q3_BASE_TEAM_RED ? QA_Q3_PW_REDFLAG : QA_Q3_PW_BLUEFLAG;
  double enemy_flag = runtime->host.game_type == QA_Q3_BASE_GT_1FCTF ? QA_Q3_PW_NEUTRALFLAG
      : team == QA_Q3_BASE_TEAM_RED ? QA_Q3_PW_BLUEFLAG : QA_Q3_PW_REDFLAG;
  double tokens = (mission(runtime) && runtime->host.game_type == QA_Q3_BASE_GT_HARVESTER)
      ? victim->ps.generic1 : 0;
  bool miss = mission(runtime);
  if (slot_get(&victim->ps.powerups, enemy_flag) != 0 || tokens != 0) {
    bool has_flag = slot_get(&victim->ps.powerups, enemy_flag) != 0;
    killer->pers.team_state.last_fragged_carrier = f32(runtime->host.time);
    runtime->host.add_score(runtime->host.ctx, attacker, qa_q3_base_entity_shared_current_origin(&target->r),
        has_flag ? (miss ? 20 : 2) : imul(imul(miss ? 20 : 2, tokens), tokens));
    killer->pers.team_state.frag_carrier = s32(killer->pers.team_state.frag_carrier + 1);
    qa_q3_game_format_arg args[] = {qa_q3_game_format_string(killer->pers.netname),
        qa_q3_game_format_string(qa_q3_ta_team_name((qa_q3_base_team)team)),
        qa_q3_game_format_string(has_flag ? "flag" : "skull")};
    print_formatted(runtime, NULL, "%s^7 fragged %s's %s carrier!\n", args, 3);
    for (double index = 0; index < runtime->host.pool->max_clients; index++) {
      qa_q3_game_entity *entity = qa_q3_entity_pool_at(runtime->host.pool, index);
      if (qa_q3_game_entity_inuse(entity) && client_of(entity)->sess.session_team == opposing)
        client_of(entity)->pers.team_state.last_hurt_carrier = 0;
    }
    return;
  }
  if (victim->pers.team_state.last_hurt_carrier != 0 &&
      f32(f32(runtime->host.time) - victim->pers.team_state.last_hurt_carrier) < 8000) {
    runtime->host.add_score(runtime->host.ctx, attacker, qa_q3_base_entity_shared_current_origin(&target->r), miss ? 5 : 2);
    killer->pers.team_state.carrier_defense = s32(killer->pers.team_state.carrier_defense + 1);
    victim->pers.team_state.last_hurt_carrier = 0;
    slot_set(&killer->ps.persistant, QA_Q3_BASE_PERS_DEFEND_COUNT,
        slot_get(&killer->ps.persistant, QA_Q3_BASE_PERS_DEFEND_COUNT) + 1);
    award(runtime, attacker, 0x10000);
    return;
  }
  const char *classname;
  qa_q3_game_entity *carrier = NULL;
  if (miss && runtime->host.game_type == QA_Q3_BASE_GT_OBELISK) {
    if (killer->sess.session_team != QA_Q3_BASE_TEAM_RED && killer->sess.session_team != QA_Q3_BASE_TEAM_BLUE) return;
    classname = killer->sess.session_team == QA_Q3_BASE_TEAM_RED ? "team_redobelisk" : "team_blueobelisk";
  } else if (miss && runtime->host.game_type == QA_Q3_BASE_GT_HARVESTER) {
    classname = "team_neutralobelisk";
  } else {
    if (killer->sess.session_team != QA_Q3_BASE_TEAM_RED && killer->sess.session_team != QA_Q3_BASE_TEAM_BLUE) return;
    classname = killer->sess.session_team == QA_Q3_BASE_TEAM_RED ? "team_CTF_redflag" : "team_CTF_blueflag";
    for (double index = 0; index < runtime->host.pool->max_clients; index++) {
      qa_q3_game_entity *entity = qa_q3_entity_pool_at(runtime->host.pool, index);
      if (qa_q3_game_entity_inuse(entity) && slot_get(&client_of(entity)->ps.powerups, flag) != 0) { carrier = entity; break; }
    }
  }
  qa_q3_game_entity *base = NULL;
  while ((base = qa_q3_find_entity(runtime->host.pool, base, QA_Q3_FIELD_CLASSNAME, classname)) != NULL)
    if ((qa_js_to_int32(base->flags) & QA_Q3_GAME_FLAG_DROPPED_ITEM) == 0) break;
  if (base == NULL) return;
  qa_vec3 a3 = qa_q3_base_entity_shared_current_origin(&target->r);
  qa_vec3 b3 = qa_q3_base_entity_shared_current_origin(&base->r);
  double target_distance = qa_length3(qa_sub3(a3, b3));
  qa_vec3 a2 = qa_q3_base_entity_shared_current_origin(&attacker->r);
  qa_vec3 b2 = qa_q3_base_entity_shared_current_origin(&base->r);
  double attacker_distance = qa_length3(qa_sub3(a2, b2));
  if (((target_distance < 1000 && (value10 = runtime->host.ctx, value11 = qa_q3_base_entity_shared_current_origin(&base->r), value12 = qa_q3_base_entity_shared_current_origin(&target->r), runtime->host.in_pvs(value10, value11, value12))) ||
       (attacker_distance < 1000 && (value7 = runtime->host.ctx, value8 = qa_q3_base_entity_shared_current_origin(&base->r), value9 = qa_q3_base_entity_shared_current_origin(&attacker->r), runtime->host.in_pvs(value7, value8, value9)))) &&
      killer->sess.session_team != victim->sess.session_team) {
    runtime->host.add_score(runtime->host.ctx, attacker, qa_q3_base_entity_shared_current_origin(&target->r), miss ? 10 : 1);
    killer->pers.team_state.base_defense = s32(killer->pers.team_state.base_defense + 1);
    slot_set(&killer->ps.persistant, QA_Q3_BASE_PERS_DEFEND_COUNT,
        slot_get(&killer->ps.persistant, QA_Q3_BASE_PERS_DEFEND_COUNT) + 1);
    award(runtime, attacker, 0x10000);
    return;
  }
  if (carrier != NULL && carrier != attacker) {
    void * value4;
    qa_vec3 value5;
    qa_vec3 value6;
    void * value;
    qa_vec3 value2;
    qa_vec3 value3;
    qa_vec3 a = qa_q3_base_entity_shared_current_origin(&attacker->r);
    qa_vec3 b = qa_q3_base_entity_shared_current_origin(&carrier->r);
    double distance = qa_length3(qa_sub3(a, b));
    if (((distance < 1000 && (value4 = runtime->host.ctx, value5 = qa_q3_base_entity_shared_current_origin(&carrier->r), value6 = qa_q3_base_entity_shared_current_origin(&target->r), runtime->host.in_pvs(value4, value5, value6))) ||
         (attacker_distance < 1000 && (value = runtime->host.ctx, value2 = qa_q3_base_entity_shared_current_origin(&carrier->r), value3 = qa_q3_base_entity_shared_current_origin(&attacker->r), runtime->host.in_pvs(value, value2, value3)))) &&
        killer->sess.session_team != victim->sess.session_team) {
      runtime->host.add_score(runtime->host.ctx, attacker, qa_q3_base_entity_shared_current_origin(&target->r), miss ? 2 : 1);
      killer->pers.team_state.carrier_defense = s32(killer->pers.team_state.carrier_defense + 1);
      slot_set(&killer->ps.persistant, QA_Q3_BASE_PERS_DEFEND_COUNT,
          slot_get(&killer->ps.persistant, QA_Q3_BASE_PERS_DEFEND_COUNT) + 1);
      award(runtime, attacker, 0x10000);
    }
  }
}

qa_q3_game_entity *qa_q3_ta_team_runtime_reset_flag(qa_q3_ta_team_runtime *runtime, qa_q3_base_team team) {
  const char *classname = team == QA_Q3_BASE_TEAM_RED ? "team_CTF_redflag" : team == QA_Q3_BASE_TEAM_BLUE
      ? "team_CTF_blueflag" : team == QA_Q3_BASE_TEAM_FREE ? "team_CTF_neutralflag" : NULL;
  if (classname == NULL) return NULL;
  qa_q3_game_entity *entity = NULL, *base = NULL;
  while ((entity = qa_q3_find_entity(runtime->host.pool, entity, QA_Q3_FIELD_CLASSNAME, classname)) != NULL) {
    if ((qa_js_to_int32(entity->flags) & QA_Q3_GAME_FLAG_DROPPED_ITEM) != 0) qa_q3_entity_pool_free(runtime->host.pool, entity);
    else { base = entity; runtime->host.respawn_item(runtime->host.ctx, entity); }
  }
  qa_q3_ta_team_runtime_set_flag_status(runtime, team, QA_Q3_TA_FLAG_AT_BASE);
  return base;
}

void qa_q3_ta_team_runtime_reset_flags(qa_q3_ta_team_runtime *runtime) {
  if (runtime->host.game_type == QA_Q3_BASE_GT_CTF) {
    qa_q3_ta_team_runtime_reset_flag(runtime, QA_Q3_BASE_TEAM_RED);
    qa_q3_ta_team_runtime_reset_flag(runtime, QA_Q3_BASE_TEAM_BLUE);
  } else if (mission(runtime) && runtime->host.game_type == QA_Q3_BASE_GT_1FCTF) {
    qa_q3_ta_team_runtime_reset_flag(runtime, QA_Q3_BASE_TEAM_FREE);
  }
}

static void flag_sound(qa_q3_ta_team_runtime *r, qa_q3_game_entity *entity, qa_q3_ta_global_team_sound sound) {
  qa_q3_game_entity *event = qa_q3_entity_pool_temp_entity(r->host.pool, entity->s.pos.base, QA_Q3_EV_GLOBAL_TEAM_SOUND);
  event->s.event_parm = sound;
  event->r.sv_flags = (double)(qa_js_to_int32(event->r.sv_flags) | QA_Q3_BASE_SVF_BROADCAST);
}

void qa_q3_ta_team_runtime_return_flag_sound(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity, qa_q3_base_team team) {
  if (entity == NULL) { runtime->host.warn(runtime->host.ctx, "Warning:  NULL passed to Team_ReturnFlagSound\n"); return; }
  flag_sound(runtime, entity, team == QA_Q3_BASE_TEAM_BLUE ? QA_Q3_TA_GTS_RED_RETURN : QA_Q3_TA_GTS_BLUE_RETURN);
}

void qa_q3_ta_team_runtime_take_flag_sound(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity, qa_q3_base_team team) {
  if (entity == NULL) { runtime->host.warn(runtime->host.ctx, "Warning:  NULL passed to Team_TakeFlagSound\n"); return; }
  if (team == QA_Q3_BASE_TEAM_RED) {
    if (runtime->state.blue_status != QA_Q3_TA_FLAG_AT_BASE && runtime->state.blue_taken_time > s32(runtime->host.time - 10000)) return;
    runtime->state.blue_taken_time = runtime->host.time;
  } else if (team == QA_Q3_BASE_TEAM_BLUE) {
    if (runtime->state.red_status != QA_Q3_TA_FLAG_AT_BASE && runtime->state.red_taken_time > s32(runtime->host.time - 10000)) return;
    runtime->state.red_taken_time = runtime->host.time;
  }
  flag_sound(runtime, entity, team == QA_Q3_BASE_TEAM_BLUE ? QA_Q3_TA_GTS_RED_TAKEN : QA_Q3_TA_GTS_BLUE_TAKEN);
}

void qa_q3_ta_team_runtime_capture_flag_sound(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity, qa_q3_base_team team) {
  if (entity == NULL) { runtime->host.warn(runtime->host.ctx, "Warning:  NULL passed to Team_CaptureFlagSound\n"); return; }
  flag_sound(runtime, entity, team == QA_Q3_BASE_TEAM_BLUE ? QA_Q3_TA_GTS_BLUE_CAPTURE : QA_Q3_TA_GTS_RED_CAPTURE);
}

void qa_q3_ta_team_runtime_return_flag(qa_q3_ta_team_runtime *runtime, qa_q3_base_team team) {
  qa_q3_ta_team_runtime_return_flag_sound(runtime, qa_q3_ta_team_runtime_reset_flag(runtime, team), team);
  if (team == QA_Q3_BASE_TEAM_FREE) qa_q3_ta_team_runtime_print_message(runtime, NULL, "The flag has returned!\n");
  else {
    qa_q3_game_format_arg a = qa_q3_game_format_string(qa_q3_ta_team_name(team));
    print_formatted(runtime, NULL, "The %s flag has returned!\n", &a, 1);
  }
}

void qa_q3_ta_team_runtime_free_entity(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity) {
  qa_q3_base_team team;
  if (flag_team(entity, &team)) qa_q3_ta_team_runtime_return_flag(runtime, team);
}
void qa_q3_ta_team_runtime_dropped_flag_think(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity) {
  qa_q3_base_team team = QA_Q3_BASE_TEAM_FREE;
  (void)flag_team(entity, &team);
  qa_q3_ta_team_runtime_return_flag_sound(runtime, qa_q3_ta_team_runtime_reset_flag(runtime, team), team);
}

double qa_q3_ta_team_runtime_touch_our_flag(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity, qa_q3_game_entity *other, qa_q3_base_team team) {
  qa_q3_game_client *client = client_of(other);
  bool miss = mission(runtime);
  bool one_flag = miss && runtime->host.game_type == QA_Q3_BASE_GT_1FCTF;
  double enemy_flag = one_flag ? QA_Q3_PW_NEUTRALFLAG
      : client->sess.session_team == QA_Q3_BASE_TEAM_RED ? QA_Q3_PW_BLUEFLAG : QA_Q3_PW_REDFLAG;
  if (!one_flag && (qa_js_to_int32(entity->flags) & QA_Q3_GAME_FLAG_DROPPED_ITEM) != 0) {
    qa_q3_game_format_arg a[] = {qa_q3_game_format_string(client->pers.netname),
        qa_q3_game_format_string(qa_q3_ta_team_name(team))};
    print_formatted(runtime, NULL, "%s^7 returned the %s flag!\n", a, 2);
    runtime->host.add_score(runtime->host.ctx, other, qa_q3_base_entity_shared_current_origin(&entity->r), miss ? 10 : 1);
    client->pers.team_state.flag_recovery = s32(client->pers.team_state.flag_recovery + 1);
    client->pers.team_state.last_returned_flag = f32(runtime->host.time);
    qa_q3_ta_team_runtime_return_flag_sound(runtime, qa_q3_ta_team_runtime_reset_flag(runtime, team), team);
    return 0;
  }
  if (slot_get(&client->ps.powerups, enemy_flag) == 0) return 0;
  if (one_flag) {
    qa_q3_game_format_arg a = qa_q3_game_format_string(client->pers.netname);
    print_formatted(runtime, NULL, "%s^7 captured the flag!\n", &a, 1);
  } else {
    qa_q3_game_format_arg a[] = {qa_q3_game_format_string(client->pers.netname),
        qa_q3_game_format_string(qa_q3_ta_other_team_name(team))};
    print_formatted(runtime, NULL, "%s^7 captured the %s flag!\n", a, 2);
  }
  slot_set(&client->ps.powerups, enemy_flag, 0);
  runtime->state.last_flag_capture = f32(runtime->host.time);
  runtime->state.last_capture_team = team;
  qa_q3_ta_team_runtime_add_team_score(runtime, entity->s.pos.base, (qa_q3_base_team)client->sess.session_team, 1);
  qa_q3_ta_team_runtime_force_gesture(runtime, (qa_q3_base_team)client->sess.session_team);
  client->pers.team_state.captures = s32(client->pers.team_state.captures + 1);
  qa_q3_ranking_reports_capture(&runtime->host.pool->rankings, other->slot);
  award(runtime, other, 0x800);
  slot_set(&client->ps.persistant, QA_Q3_BASE_PERS_CAPTURES, slot_get(&client->ps.persistant, QA_Q3_BASE_PERS_CAPTURES) + 1);
  runtime->host.add_score(runtime->host.ctx, other, qa_q3_base_entity_shared_current_origin(&entity->r), miss ? 100 : 5);
  qa_q3_ta_team_runtime_capture_flag_sound(runtime, entity, team);
  for (double index = 0; index < runtime->host.pool->max_clients; index++) {
    qa_q3_game_entity *player = qa_q3_entity_pool_at(runtime->host.pool, index);
    if (!qa_q3_game_entity_inuse(player)) continue;
    qa_q3_game_client *teammate = client_of(player);
    if (teammate->sess.session_team != client->sess.session_team) teammate->pers.team_state.last_hurt_carrier = -5;
    else {
      if (player != other) runtime->host.add_score(runtime->host.ctx, player, qa_q3_base_entity_shared_current_origin(&entity->r), miss ? 25 : 0);
      bool has_bonus = false;
      double bonus = 0;
      if (f32(teammate->pers.team_state.last_returned_flag + 10000) > f32(runtime->host.time)) { bonus = miss ? 10 : 1; has_bonus = true; }
      else if (f32(teammate->pers.team_state.last_fragged_carrier + 10000) > f32(runtime->host.time)) { bonus = miss ? 10 : 2; has_bonus = true; }
      if (has_bonus) {
        runtime->host.add_score(runtime->host.ctx, player, qa_q3_base_entity_shared_current_origin(&entity->r), bonus);
        client->pers.team_state.assists = s32(client->pers.team_state.assists + 1);
        slot_set(&teammate->ps.persistant, QA_Q3_BASE_PERS_ASSIST_COUNT,
            slot_get(&teammate->ps.persistant, QA_Q3_BASE_PERS_ASSIST_COUNT) + 1);
        award(runtime, player, 0x20000);
      }
    }
  }
  qa_q3_ta_team_runtime_reset_flags(runtime);
  runtime->host.calculate_ranks(runtime->host.ctx);
  return 0;
}

double qa_q3_ta_team_runtime_touch_enemy_flag(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity, qa_q3_game_entity *other, qa_q3_base_team team) {
  qa_q3_game_client *client = client_of(other);
  bool miss = mission(runtime);
  if (miss && runtime->host.game_type == QA_Q3_BASE_GT_1FCTF) {
    qa_q3_game_format_arg a = qa_q3_game_format_string(client->pers.netname);
    print_formatted(runtime, NULL, "%s^7 got the flag!\n", &a, 1);
    slot_set(&client->ps.powerups, QA_Q3_PW_NEUTRALFLAG, 2147483647);
    qa_q3_ta_team_runtime_set_flag_status(runtime, QA_Q3_BASE_TEAM_FREE,
        team == QA_Q3_BASE_TEAM_RED ? QA_Q3_TA_FLAG_TAKEN_RED : QA_Q3_TA_FLAG_TAKEN_BLUE);
  } else {
    qa_q3_game_format_arg a[] = {qa_q3_game_format_string(client->pers.netname),
        qa_q3_game_format_string(qa_q3_ta_team_name(team))};
    print_formatted(runtime, NULL, "%s^7 got the %s flag!\n", a, 2);
    slot_set(&client->ps.powerups, team == QA_Q3_BASE_TEAM_RED ? QA_Q3_PW_REDFLAG : QA_Q3_PW_BLUEFLAG, 2147483647);
    qa_q3_ranking_reports_pickup_powerup(&runtime->host.pool->rankings, other->slot,
        team == QA_Q3_BASE_TEAM_RED ? QA_Q3_PW_REDFLAG : QA_Q3_PW_BLUEFLAG);
    qa_q3_ta_team_runtime_set_flag_status(runtime, team, QA_Q3_TA_FLAG_TAKEN);
  }
  runtime->host.add_score(runtime->host.ctx, other, qa_q3_base_entity_shared_current_origin(&entity->r), miss ? 10 : 0);
  client->pers.team_state.flag_since = f32(runtime->host.time);
  qa_q3_ta_team_runtime_take_flag_sound(runtime, entity, team);
  return -1;
}

double qa_q3_ta_team_runtime_pickup_team(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity, qa_q3_game_entity *other) {
  qa_q3_game_client *client = client_of(other);
  if (mission(runtime)) {
    if (runtime->host.game_type == QA_Q3_BASE_GT_OBELISK) { qa_q3_entity_pool_free(runtime->host.pool, entity); return 0; }
    if (runtime->host.game_type == QA_Q3_BASE_GT_HARVESTER) {
      if (entity->spawnflags != client->sess.session_team)
        client->ps.generic1 = s32(client->ps.generic1 + 1);
      qa_q3_entity_pool_free(runtime->host.pool, entity);
      return 0;
    }
  }
  const char *classname = qa_q3_game_entity_classname(entity);
  bool has_team = true;
  qa_q3_base_team team = QA_Q3_BASE_TEAM_FREE;
  if (classname && strcmp(classname, "team_CTF_redflag") == 0) team = QA_Q3_BASE_TEAM_RED;
  else if (classname && strcmp(classname, "team_CTF_blueflag") == 0) team = QA_Q3_BASE_TEAM_BLUE;
  else if (mission(runtime) && classname && strcmp(classname, "team_CTF_neutralflag") == 0) team = QA_Q3_BASE_TEAM_FREE;
  else has_team = false;
  if (!has_team) { qa_q3_ta_team_runtime_print_message(runtime, other, "Don't know what team the flag is on.\n"); return 0; }
  if (mission(runtime) && runtime->host.game_type == QA_Q3_BASE_GT_1FCTF) {
    if (team == QA_Q3_BASE_TEAM_FREE)
      return qa_q3_ta_team_runtime_touch_enemy_flag(runtime, entity, other, (qa_q3_base_team)client->sess.session_team);
    return team != client->sess.session_team
        ? qa_q3_ta_team_runtime_touch_our_flag(runtime, entity, other, (qa_q3_base_team)client->sess.session_team) : 0;
  }
  return team == client->sess.session_team
      ? qa_q3_ta_team_runtime_touch_our_flag(runtime, entity, other, team)
      : qa_q3_ta_team_runtime_touch_enemy_flag(runtime, entity, other, team);
}

qa_q3_game_entity *qa_q3_ta_team_runtime_get_location(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity) {
  qa_q3_game_entity *best = NULL;
  double best_length = 3.0 * 8192 * 8192;
  for (qa_q3_game_entity *location = runtime->host.location_head; location != NULL; location = location->next_train) {
    void * value;
    qa_vec3 value2;
    qa_vec3 value3;
    qa_vec3 a = qa_q3_base_entity_shared_current_origin(&entity->r);
    qa_vec3 b = qa_q3_base_entity_shared_current_origin(&location->r);
    qa_vec3 delta = qa_sub3(a, b);
    double length = qa_dot3(delta, delta);
    if (length > best_length || !(value = runtime->host.ctx, value2 = qa_q3_base_entity_shared_current_origin(&entity->r), value3 = qa_q3_base_entity_shared_current_origin(&location->r), runtime->host.in_pvs(value, value2, value3))) continue;
    best_length = length;
    best = location;
  }
  return best;
}

char *qa_q3_ta_team_runtime_get_location_message(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity, double capacity, bool *out_is_null) {
  qa_q3_game_entity *location = qa_q3_ta_team_runtime_get_location(runtime, entity);
  if (location == NULL) { *out_is_null = true; return NULL; }
  *out_is_null = false;
  if (location->count != 0) {
    location->count = qa_js_max(0, qa_js_min(7, location->count));
    qa_q3_game_format_arg a[] = {qa_q3_game_format_number(94), qa_q3_game_format_number(location->count + 48),
        qa_q3_game_format_string(location->message)};
    return qa_q3_game_format("%c%c%s^7", a, 3, capacity);
  }
  qa_q3_game_format_arg a = qa_q3_game_format_string(location->message);
  return qa_q3_game_format("%s", &a, 1, capacity);
}

static int compare_doubles(const void *a, const void *b) {
  double x = *(const double *)a, y = *(const double *)b;
  return x < y ? -1 : x > y ? 1 : 0;
}

void qa_q3_ta_team_runtime_teamplay_info_message(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity) {
  if (!client_of(entity)->pers.team_info) return;
  double clients[32];
  size_t client_count = 0;
  for (double index = 0; index < runtime->host.pool->max_clients && client_count < 32; index++) {
    if ((size_t)index >= runtime->host.sorted_client_count)
      qa_range_error("Missing sorted client slot %s", QA_JS_NUMBER(index));
    double client_num = runtime->host.sorted_clients[(size_t)index];
    qa_q3_game_entity *player = qa_q3_entity_pool_at(runtime->host.pool, client_num);
    if (qa_q3_game_entity_inuse(player) && client_of(player)->sess.session_team == client_of(entity)->sess.session_team) {
      if ((size_t)index >= runtime->host.sorted_client_count)
        qa_range_error("Missing sorted client slot %s", QA_JS_NUMBER(index));
      clients[client_count++] = runtime->host.sorted_clients[(size_t)index];
    }
  }
  qa_js_sort(clients, client_count, sizeof(double), compare_doubles);
  char message[8193] = "";
  size_t length = 0;
  double count = 0;
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(runtime->host.product);
  for (double index = 0; index < runtime->host.pool->max_clients && count < 32; index++) {
    qa_q3_game_entity *player = qa_q3_entity_pool_at(runtime->host.pool, index);
    if (!qa_q3_game_entity_inuse(player) || client_of(player)->sess.session_team != client_of(entity)->sess.session_team) continue;
    qa_q3_game_client *client = client_of(player);
    qa_q3_game_format_arg value = qa_q3_game_format_number(index);
    qa_q3_game_format_arg value2 = qa_q3_game_format_number(client->pers.team_state.location);
    qa_q3_game_format_arg value3 = qa_q3_game_format_number(qa_js_max(0, qa_q3_base_player_state_record_health(&client->ps)));
    qa_q3_game_format_arg value4 = qa_q3_game_format_number(qa_js_max(0, slot_get(&client->ps.stats, schema->armor)));
    qa_q3_game_format_arg value5 = qa_q3_game_format_number(client->ps.weapon);
    qa_q3_game_format_arg value6 = qa_q3_game_format_number(player->s.powerups);
    qa_q3_game_format_arg a[] = {value, value2, value3, value4, value5, value6};
    char *entry = qa_q3_game_format(" %i %i %i %i %i %i", a, 6, 1024);
    size_t entry_length = strlen(entry);
    if (length + entry_length > 8192) { free(entry); break; }
    memcpy(message + length, entry, entry_length + 1);
    length += entry_length;
    free(entry);
    count++;
  }
  qa_q3_game_format_arg a[] = {qa_q3_game_format_number(count), qa_q3_game_format_string(message)};
  char *text = qa_q3_game_format("tinfo %i %s", a, 2, 32000);
  runtime->host.send_server_command(runtime->host.ctx, entity->slot, text);
  free(text);
}

void qa_q3_ta_team_runtime_check_team_status(qa_q3_ta_team_runtime *runtime) {
  if (s32(runtime->host.time - runtime->last_team_location_time) <= 1000) return;
  runtime->last_team_location_time = runtime->host.time;
  for (double index = 0; index < runtime->host.pool->max_clients; index++) {
    qa_q3_game_entity *entity = qa_q3_entity_pool_at(runtime->host.pool, index);
    qa_q3_game_client *client = client_of(entity);
    if (client->pers.connected != QA_Q3_CONN_CONNECTED) continue;
    if (qa_q3_game_entity_inuse(entity) &&
        (client->sess.session_team == QA_Q3_BASE_TEAM_RED || client->sess.session_team == QA_Q3_BASE_TEAM_BLUE)) {
      qa_q3_game_entity *location = qa_q3_ta_team_runtime_get_location(runtime, entity);
      client->pers.team_state.location = location ? qa_q3_game_entity_health(location) : 0;
    }
  }
  for (double index = 0; index < runtime->host.pool->max_clients; index++) {
    qa_q3_game_entity *entity = qa_q3_entity_pool_at(runtime->host.pool, index);
    qa_q3_game_client *client = client_of(entity);
    if (client->pers.connected != QA_Q3_CONN_CONNECTED) continue;
    if (qa_q3_game_entity_inuse(entity) &&
        (client->sess.session_team == QA_Q3_BASE_TEAM_RED || client->sess.session_team == QA_Q3_BASE_TEAM_BLUE))
      qa_q3_ta_team_runtime_teamplay_info_message(runtime, entity);
  }
}



static qa_q3_ta_obelisk_settings obelisk_settings(qa_q3_ta_team_runtime *r) {
  if (!mission(r)) qa_range_error("Obelisk handlers belong to the missionpack product");
  return r->host.obelisk;
}
static qa_q3_game_entity *obelisk_model(qa_q3_game_entity *entity) {
  if (entity->activator == NULL) qa_range_error("Obelisk callback requires its spawned model entity");
  return entity->activator;
}
static qa_q3_base_team obelisk_team(qa_q3_game_entity *entity) {
  if (entity->spawnflags == QA_Q3_BASE_TEAM_FREE || entity->spawnflags == QA_Q3_BASE_TEAM_RED ||
      entity->spawnflags == QA_Q3_BASE_TEAM_BLUE || entity->spawnflags == QA_Q3_BASE_TEAM_SPECTATOR)
    return (qa_q3_base_team)entity->spawnflags;
  qa_range_error("Spawned obelisk has no source team");
}
static void obelisk_regen_fn(void *ctx, qa_q3_game_entity *self) {
  qa_q3_ta_team_runtime *r = ctx;
  qa_q3_ta_obelisk_settings settings = obelisk_settings(r);
  qa_q3_game_entity_set_nextthink(self, s32(r->host.time + imul(settings.regen_period_seconds, 1000)));
  double health = qa_q3_game_entity_health(self);
  if (health >= settings.health) return;
  qa_q3_entity_pool_add_event(r->host.pool, self, QA_Q3_EV_POWERUP_REGEN, 0);
  health = qa_js_min(s32(health + settings.regen_amount), settings.health);
  qa_q3_game_entity_set_health(self, health);
  qa_q3_game_entity *model = obelisk_model(self);
  model->s.modelindex2 = s32(trunc(imul(health, 255) / settings.health));
  model->s.frame = 0;
}
static void obelisk_respawn_fn(void *ctx, qa_q3_game_entity *self) {
  qa_q3_ta_team_runtime *r = ctx;
  qa_q3_ta_obelisk_settings settings = obelisk_settings(r);
  qa_q3_game_entity_set_takedamage(self, true);
  qa_q3_game_entity_set_health(self, settings.health);
  self->think = qa_q3_callback_family_resolve(&r->host.pool->callbacks.think, "q3.team-arena.team.obeliskRespawn.think");
  qa_q3_game_entity_set_nextthink(self, s32(r->host.time + imul(settings.regen_period_seconds, 1000)));
  obelisk_model(self)->s.frame = 0;
}
static void obelisk_die_fn(void *ctx, qa_q3_game_entity *self, qa_q3_damage_inflictor inflictor,
    qa_q3_damage_participant attacker, double damage, double method) {
  (void)inflictor; (void)damage; (void)method;
  qa_q3_ta_team_runtime *r = ctx;
  qa_q3_base_team team = obelisk_team(self), opposing = qa_q3_ta_other_team(team);
  qa_q3_ta_team_runtime_add_team_score(r, self->s.pos.base, opposing, 1);
  qa_q3_ta_team_runtime_force_gesture(r, opposing);
  r->host.calculate_ranks(r->host.ctx);
  qa_q3_game_entity_set_takedamage(self, false);
  self->think = qa_q3_callback_family_resolve(&r->host.pool->callbacks.think, "q3.team-arena.team.obeliskDie.think");
  qa_q3_game_entity_set_nextthink(self, s32(r->host.time + imul(obelisk_settings(r).respawn_delay_seconds, 1000)));
  qa_q3_game_entity *model = obelisk_model(self);
  model->s.modelindex2 = 255;
  model->s.frame = 2;
  qa_q3_entity_pool_add_event(r->host.pool, model, QA_Q3_EV_OBELISKEXPLODE, 0);
  if (attacker.entity != NULL && attacker.entity->client != NULL) {
    r->host.add_score(r->host.ctx, attacker.entity, qa_q3_base_entity_shared_current_origin(&self->r), 100);
    award(r, attacker.entity, 0x800);
    qa_q3_base_player_state_slots *p = &attacker.entity->client->ps.persistant;
    slot_set(p, QA_Q3_BASE_PERS_CAPTURES, slot_get(p, QA_Q3_BASE_PERS_CAPTURES) + 1);
  }
  r->state.red_obelisk_attacked_time = 0;
  r->state.blue_obelisk_attacked_time = 0;
}
static void obelisk_touch_fn(void *ctx, qa_q3_game_entity *self, qa_q3_damage_participant other, qa_touch_contact contact) {
  (void)contact;
  qa_q3_ta_team_runtime *r = ctx;
  if (other.entity == NULL || other.entity->client == NULL ||
      qa_q3_ta_other_team((qa_q3_base_team)other.entity->client->sess.session_team) != self->spawnflags) return;
  double tokens = other.entity->client->ps.generic1;
  if (tokens <= 0) return;
  qa_q3_game_format_arg a[] = {qa_q3_game_format_string(other.entity->client->pers.netname),
      qa_q3_game_format_number(tokens), qa_q3_game_format_string(tokens != 0 ? "s" : "")};
  char *text = qa_q3_game_format("%s^7 brought in %i skull%s.\n", a, 3, 32000);
  qa_q3_ta_team_runtime_print_message(r, NULL, text);
  free(text);
  qa_q3_ta_team_runtime_add_team_score(r, self->s.pos.base,
      (qa_q3_base_team)other.entity->client->sess.session_team, tokens);
  qa_q3_ta_team_runtime_force_gesture(r, (qa_q3_base_team)other.entity->client->sess.session_team);
  r->host.add_score(r->host.ctx, other.entity, qa_q3_base_entity_shared_current_origin(&self->r), imul(100, tokens));
  award(r, other.entity, 0x800);
  qa_q3_base_player_state_slots *p = &other.entity->client->ps.persistant;
  slot_set(p, QA_Q3_BASE_PERS_CAPTURES, slot_get(p, QA_Q3_BASE_PERS_CAPTURES) + tokens);
  other.entity->client->ps.generic1 = 0;
  r->host.calculate_ranks(r->host.ctx);
  qa_q3_ta_team_runtime_capture_flag_sound(r, self, obelisk_team(self));
}
static void obelisk_pain_fn(void *ctx, qa_q3_game_entity *self, qa_q3_damage_participant attacker, double amount) {
  qa_q3_ta_team_runtime *r = ctx;
  double actual = qa_js_max(1, trunc(amount / 10));
  qa_q3_game_entity *model = obelisk_model(self);
  double left = imul(qa_q3_game_entity_health(self), 255);
  double right = obelisk_settings(r).health;
  model->s.modelindex2 = s32(trunc(left / right));
  if (model->s.frame == 0) qa_q3_entity_pool_add_event(r->host.pool, self, QA_Q3_EV_OBELISKPAIN, 0);
  model->s.frame = 1;
  if (attacker.entity != NULL) r->host.add_score(r->host.ctx, attacker.entity, qa_q3_base_entity_shared_current_origin(&self->r), actual);
}
static qa_q3_entity_callback make_callback(const void *fn, size_t fn_size, void *ctx) {
  qa_q3_entity_callback c = {NULL, ctx};
  memcpy(&c.fn, fn, fn_size < sizeof c.fn ? fn_size : sizeof c.fn);
  return c;
}
qa_q3_entity_callback qa_q3_ta_team_runtime_obelisk_regen(qa_q3_ta_team_runtime *r) {
  qa_q3_entity_think_fn fn = obelisk_regen_fn; return make_callback(&fn, sizeof fn, r); }
qa_q3_entity_callback qa_q3_ta_team_runtime_obelisk_respawn(qa_q3_ta_team_runtime *r) {
  qa_q3_entity_think_fn fn = obelisk_respawn_fn; return make_callback(&fn, sizeof fn, r); }
qa_q3_entity_callback qa_q3_ta_team_runtime_obelisk_die(qa_q3_ta_team_runtime *r) {
  qa_q3_entity_die_fn fn = obelisk_die_fn; return make_callback(&fn, sizeof fn, r); }
qa_q3_entity_callback qa_q3_ta_team_runtime_obelisk_touch(qa_q3_ta_team_runtime *r) {
  qa_q3_entity_touch_fn fn = obelisk_touch_fn; return make_callback(&fn, sizeof fn, r); }
qa_q3_entity_callback qa_q3_ta_team_runtime_obelisk_pain(qa_q3_ta_team_runtime *r) {
  qa_q3_entity_pain_fn fn = obelisk_pain_fn; return make_callback(&fn, sizeof fn, r); }

qa_q3_game_entity *qa_q3_ta_team_runtime_spawn_obelisk(qa_q3_ta_team_runtime *runtime, qa_vec3 origin, qa_q3_base_team team, double spawnflags) {
  qa_q3_ta_obelisk_settings settings = obelisk_settings(runtime);
  qa_q3_game_entity *entity = qa_q3_entity_pool_spawn(runtime->host.pool);
  entity->s.origin = origin;
  entity->s.pos.base = origin;
  qa_q3_base_entity_shared_set_current_origin(&entity->r, origin);
  qa_q3_base_entity_shared_set_mins(&entity->r, qa_math_vec3(-15, -15, 0));
  qa_q3_base_entity_shared_set_maxs(&entity->r, qa_math_vec3(15, 15, 87));
  entity->s.e_type = QA_Q3_BASE_ET_GENERAL;
  entity->flags = QA_Q3_GAME_FLAG_NO_KNOCKBACK;
  if (runtime->host.game_type == QA_Q3_BASE_GT_OBELISK) {
    entity->r.contents = 1;
    qa_q3_game_entity_set_takedamage(entity, true);
    qa_q3_game_entity_set_health(entity, settings.health);
    entity->die = qa_q3_callback_family_resolve(&runtime->host.pool->callbacks.die, "q3.team-arena.team.spawnObelisk.die");
    entity->pain = qa_q3_callback_family_resolve(&runtime->host.pool->callbacks.pain, "q3.team-arena.team.spawnObelisk.pain");
    entity->think = qa_q3_callback_family_resolve(&runtime->host.pool->callbacks.think, "q3.team-arena.team.obeliskRespawn.think");
    qa_q3_game_entity_set_nextthink(entity, s32(runtime->host.time + imul(settings.regen_period_seconds, 1000)));
  }
  if (runtime->host.game_type == QA_Q3_BASE_GT_HARVESTER) {
    entity->r.contents = 0x40000000;
    entity->touch = qa_q3_callback_family_resolve(&runtime->host.pool->callbacks.touch, "q3.team-arena.team.spawnObelisk.touch");
  }
  if ((qa_js_to_int32(spawnflags) & 1) != 0) qa_q3_set_origin(entity, entity->s.origin);
  else {
    entity->s.origin = qa_math_vec3(origin.x, origin.y, origin.z + 1);
    qa_vec3 destination = qa_math_vec3(entity->s.origin.x, entity->s.origin.y, entity->s.origin.z - 4096);
    qa_actor_id pass = qa_q3_game_entity_actor(entity).id;
    qa_vec3 mins = qa_q3_base_entity_shared_mins(&entity->r);
    qa_vec3 maxs = qa_q3_base_entity_shared_maxs(&entity->r);
    qa_q3_base_actor_trace_query query = {entity->s.origin, destination, {QA_Q3_BASE_SHAPE_BOX, mins, maxs}, 1, &pass};
    qa_q3_base_actor_trace_result trace = runtime->host.spatial->trace_actor(runtime->host.spatial->ctx, query);
    if (trace.solidity != QA_Q3_BASE_SOLID_CLEAR) {
      entity->s.origin = qa_math_vec3(entity->s.origin.x, entity->s.origin.y, entity->s.origin.z - 1);
      const char *vtos = (const char *)qa_q3_game_utility_scratch_vtos(runtime->host.pool->utilities, entity->s.origin);
      qa_q3_game_format_arg a[] = {qa_q3_game_format_string(qa_q3_game_entity_classname(entity)),
          qa_q3_game_format_string(vtos)};
      char *text = qa_q3_game_format("SpawnObelisk: %s startsolid at %s\n", a, 2, 32000);
      runtime->host.warn(runtime->host.ctx, text);
      free(text);
      qa_q3_write_ground(entity, NULL, runtime->host.pool);
      qa_q3_set_origin(entity, entity->s.origin);
    } else {
      qa_q3_trace_ground(entity, trace.hit_kind, trace.hit_actor, runtime->host.pool);
      qa_q3_set_origin(entity, trace.end);
    }
  }
  entity->spawnflags = team;
  runtime->host.world->link(runtime->host.world->ctx, entity);
  return entity;
}

void qa_q3_ta_team_runtime_spawn_team_obelisk(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity, qa_q3_base_team team) {
  (void)obelisk_settings(runtime);
  if (runtime->host.game_type <= QA_Q3_BASE_GT_TEAM) { qa_q3_entity_pool_free(runtime->host.pool, entity); return; }
  entity->s.e_type = QA_Q3_BASE_ET_TEAM;
  if (runtime->host.game_type == QA_Q3_BASE_GT_OBELISK || runtime->host.game_type == QA_Q3_BASE_GT_HARVESTER) {
    qa_q3_game_entity *obelisk = qa_q3_ta_team_runtime_spawn_obelisk(runtime, entity->s.origin, team, entity->spawnflags);
    obelisk->activator = entity;
    if (runtime->host.game_type == QA_Q3_BASE_GT_OBELISK) { entity->s.modelindex2 = 255; entity->s.frame = 0; }
  }
  entity->s.modelindex = team;
  runtime->host.world->link(runtime->host.world->ctx, entity);
}

void qa_q3_ta_team_runtime_spawn_neutral_obelisk(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *entity) {
  (void)obelisk_settings(runtime);
  if (runtime->host.game_type != QA_Q3_BASE_GT_1FCTF && runtime->host.game_type != QA_Q3_BASE_GT_HARVESTER) {
    qa_q3_entity_pool_free(runtime->host.pool, entity); return; }
  entity->s.e_type = QA_Q3_BASE_ET_TEAM;
  if (runtime->host.game_type == QA_Q3_BASE_GT_HARVESTER)
    runtime->neutral_obelisk = qa_q3_ta_team_runtime_spawn_obelisk(runtime, entity->s.origin, QA_Q3_BASE_TEAM_FREE, entity->spawnflags);
  entity->s.modelindex = QA_Q3_BASE_TEAM_FREE;
  runtime->host.world->link(runtime->host.world->ctx, entity);
}

bool qa_q3_ta_team_runtime_check_obelisk_attack(qa_q3_ta_team_runtime *runtime, qa_q3_game_entity *obelisk, qa_q3_game_entity *attacker) {
  qa_q3_entity_callback die = qa_q3_ta_team_runtime_obelisk_die(runtime);
  if (obelisk->die.fn != die.fn || obelisk->die.ctx != die.ctx || attacker->client == NULL) return false;
  if (obelisk->spawnflags == attacker->client->sess.session_team) return true;
  bool red = obelisk->spawnflags == QA_Q3_BASE_TEAM_RED, blue = obelisk->spawnflags == QA_Q3_BASE_TEAM_BLUE;
  if ((red && runtime->state.red_obelisk_attacked_time < s32(runtime->host.time - 20000)) ||
      (blue && runtime->state.blue_obelisk_attacked_time < s32(runtime->host.time - 20000))) {
    flag_sound(runtime, obelisk, red ? QA_Q3_TA_GTS_RED_OBELISK_ATTACKED : QA_Q3_TA_GTS_BLUE_OBELISK_ATTACKED);
    if (red) runtime->state.red_obelisk_attacked_time = runtime->host.time;
    else runtime->state.blue_obelisk_attacked_time = runtime->host.time;
  }
  return false;
}

void qa_q3_ta_team_runtime_bind_save_callbacks(qa_q3_ta_team_runtime *runtime) {
  qa_q3_callback_catalog *c = &runtime->host.pool->callbacks;
  (void)qa_q3_callback_family_intern(&c->think, "q3.team-arena.team.obeliskRespawn.think", qa_q3_ta_team_runtime_obelisk_regen(runtime));
  (void)qa_q3_callback_family_intern(&c->think, "q3.team-arena.team.obeliskDie.think", qa_q3_ta_team_runtime_obelisk_respawn(runtime));
  (void)qa_q3_callback_family_intern(&c->die, "q3.team-arena.team.spawnObelisk.die", qa_q3_ta_team_runtime_obelisk_die(runtime));
  (void)qa_q3_callback_family_intern(&c->pain, "q3.team-arena.team.spawnObelisk.pain", qa_q3_ta_team_runtime_obelisk_pain(runtime));
  (void)qa_q3_callback_family_intern(&c->touch, "q3.team-arena.team.spawnObelisk.touch", qa_q3_ta_team_runtime_obelisk_touch(runtime));
}
