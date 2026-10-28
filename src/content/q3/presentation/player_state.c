/* Port of src/content/q3/presentation/player-state.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/player_state.h"

#include <math.h>
#include <string.h>

#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"

enum { LOCAL_SOUND = 6, ANNOUNCER = 7 };

static double f32(double value) { return (double)(float)value; }
static int32_t to_int32(double value) {
  if (!isfinite(value)) return 0;
  double truncated = trunc(fmod(fabs(value), 4294967296.0));
  uint32_t bits = (uint32_t)truncated;
  if (value < 0) bits = ~bits + 1u;
  int32_t result;
  memcpy(&result, &bits, sizeof result);
  return result;
}
static int32_t imul(double a, double b) {
  uint32_t product = (uint32_t)to_int32(a) * (uint32_t)to_int32(b);
  int32_t result;
  memcpy(&result, &product, sizeof result);
  return result;
}

static const char *const PLAYER_STATE_SOUND_NAMES[] = {
  "noAmmoSound",
  "hitSound",
  "hitTeamSound",
  "captureAwardSound",
  "impressiveSound",
  "excellentSound",
  "humiliationSound",
  "defendSound",
  "assistSound",
  "deniedSound",
  "holyShitSound",
  "youHaveFlagSound",
  "takenLeadSound",
  "tiedLeadSound",
  "lostLeadSound",
  "suddenDeathSound",
  "oneMinuteSound",
  "fiveMinuteSound",
  "oneFragSound",
  "twoFragSound",
  "threeFragSound",
};
static const char *const MISSION_PLAYER_STATE_SOUND_NAMES[] = {
  "hitSoundHighArmor",
  "hitSoundLowArmor",
  "firstImpressiveSound",
  "firstExcellentSound",
  "firstHumiliationSound",
};
static const char *const REWARD_MEDAL_NAMES[] = {
  "medalCapture",
  "medalImpressive",
  "medalExcellent",
  "medalGauntlet",
  "medalDefend",
  "medalAssist",
};

const char *qa_q3_pres_player_state_sound_name(qa_q3_pres_player_state_sound sound) {
  if (sound < 0 || sound >= QA_Q3_PRES_PLAYER_STATE_SOUND_COUNT) return "";
  return PLAYER_STATE_SOUND_NAMES[sound];
}
const char *qa_q3_pres_mission_player_state_sound_name(qa_q3_pres_mission_player_state_sound sound) {
  if (sound < 0 || sound >= QA_Q3_PRES_MISSION_PLAYER_STATE_SOUND_COUNT) return "";
  return MISSION_PLAYER_STATE_SOUND_NAMES[sound];
}
const char *qa_q3_pres_reward_medal_name(qa_q3_pres_reward_medal medal) {
  if (medal < 0 || medal >= QA_Q3_PRES_REWARD_MEDAL_COUNT) return "";
  return REWARD_MEDAL_NAMES[medal];
}

typedef qa_q3_pres_player_state_runtime runtime_t;
static qa_q3_pres_retail_snapshot *snapshot_of(runtime_t *runtime) {
  qa_q3_pres_retail_snapshot *snapshot = runtime->state->snap;
  if (snapshot == NULL) qa_range_error("Player-state transition requires cg.snap");
  return snapshot;
}
static double pers_get(const qa_q3_base_source_player_state *ps, qa_q3_base_persistent_index index) {
  return qa_q3_base_player_state_slots_get(&ps->persistant, index);
}
static void local_sound(runtime_t *runtime, qa_q3_pres_player_state_sound key, double channel) {
  runtime->host.start_local_sound(runtime->host.ctx, runtime->host.sounds[key], channel);
}
static void mission_local_sound(runtime_t *runtime, qa_q3_pres_mission_player_state_sound key, double channel) {
  runtime->host.start_local_sound(runtime->host.ctx, runtime->host.mission_sounds[key], channel);
}
static void buffered(runtime_t *runtime, qa_q3_pres_player_state_sound key) {
  runtime->host.add_buffered_sound(runtime->host.ctx, runtime->host.sounds[key]);
}

void qa_q3_pres_player_state_runtime_init(runtime_t *runtime, qa_q3_pres_client_game_state *state,
    qa_q3_pres_player_state_host host) {
  if (state->product != host.product || state->product != host.static_state->product) {
    qa_range_error("Player-state transition product mismatch");
  }
  runtime->state = state;
  runtime->host = host;
}

void qa_q3_pres_player_state_runtime_check_ammo(runtime_t *runtime) {
  qa_q3_pres_client_game_state *state = runtime->state;
  if (runtime->host.weapon_hud != NULL) {
    qa_q3_pres_weapon_hud_reading reading = runtime->host.weapon_hud(runtime->host.weapon_hud_ctx);
    double previous = state->low_ammo_warning;
    state->low_ammo_warning = reading.warning != NULL && strcmp(reading.warning, "empty") == 0 ? 2
        : reading.warning != NULL && strcmp(reading.warning, "low") == 0 ? 1 : 0;
    if (state->low_ammo_warning != 0 && state->low_ammo_warning != previous) {
      local_sound(runtime, QA_Q3_PRES_NO_AMMO_SOUND, LOCAL_SOUND);
    }
    return;
  }
  qa_q3_base_source_player_state *ps = &snapshot_of(runtime)->player_state;
  qa_q3_base_product product = qa_q3_base_player_state_record_product(ps);
  double weapons = qa_q3_base_player_state_slots_get(&ps->stats,
      qa_q3_base_stat_schema_for(product)->weapons);
  int32_t total = 0;
  for (int32_t weapon = QA_Q3_WP_MACHINEGUN; weapon < qa_q3_base_weapon_count(product); weapon++) {
    if ((to_int32(weapons) & (1 << weapon)) == 0) continue;
    bool slow = weapon == QA_Q3_WP_ROCKET_LAUNCHER || weapon == QA_Q3_WP_GRENADE_LAUNCHER ||
                weapon == QA_Q3_WP_RAILGUN || weapon == QA_Q3_WP_SHOTGUN ||
                (product == QA_Q3_BASE_PRODUCT_MISSIONPACK && weapon == QA_Q3_WP_PROX_LAUNCHER);
    total = to_int32((double)total + imul(qa_q3_base_player_state_slots_get(&ps->ammo, weapon),
                                        slow ? 1000 : 200));
    if (total >= 5000) {
      state->low_ammo_warning = 0;
      return;
    }
  }
  double previous = state->low_ammo_warning;
  state->low_ammo_warning = total == 0 ? 2 : 1;
  if (state->low_ammo_warning != previous) local_sound(runtime, QA_Q3_PRES_NO_AMMO_SOUND, LOCAL_SOUND);
}

void qa_q3_pres_player_state_runtime_damage_feedback(runtime_t *runtime, double yaw_byte,
    double pitch_byte, double damage) {
  qa_q3_pres_client_game_state *state = runtime->state;
  qa_q3_pres_retail_snapshot *snapshot = snapshot_of(runtime);
  double health = qa_q3_base_player_state_record_health(&snapshot->player_state);
  state->attacker_time = state->time;
  double scale = health < 40 ? 1 : f32(40 / f32(health));
  double kick = f32(f32(damage) * scale);
  if (kick < 5) kick = 5;
  if (kick > 10) kick = 10;
  if (yaw_byte == 255 && pitch_byte == 255) {
    state->damage_x = 0;
    state->damage_y = 0;
    state->damage_roll = 0;
    state->damage_pitch = -kick;
  } else {
    double pitch = f32(f32(f32(pitch_byte) / 255) * 360);
    double yaw = f32(f32(f32(yaw_byte) / 255) * 360);
    qa_angle_vectors vectors = qa_qvm_angle_vectors(qa_math_vec3(pitch, yaw, 0));
    qa_vec3 direction = qa_sub3(qa_math_vec3(0, 0, 0), vectors.forward);
    double front = qa_dot3(direction, state->refdef.view_axis.vectors[0]);
    double left = qa_dot3(direction, state->refdef.view_axis.vectors[1]);
    double up = qa_dot3(direction, state->refdef.view_axis.vectors[2]);
    double distance = qa_length3(qa_math_vec3(front, left, 0));
    if (distance < f32(0.1)) distance = f32(0.1);
    state->damage_roll = f32(kick * left);
    state->damage_pitch = f32(-kick * front);
    if (front <= f32(0.1)) front = f32(0.1);
    state->damage_x = f32(-left / front);
    state->damage_y = f32(up / distance);
  }
  if (state->damage_x > 1) state->damage_x = 1;
  if (state->damage_x < -1) state->damage_x = -1;
  if (state->damage_y > 1) state->damage_y = 1;
  if (state->damage_y < -1) state->damage_y = -1;
  if (kick > 10) kick = 10;
  state->damage_value = kick;
  state->damage_kick_end_time = f32((double)to_int32(state->time + 500));
  state->damage_time = f32((double)snapshot->server_time);
}

void qa_q3_pres_player_state_runtime_respawn(runtime_t *runtime) {
  runtime->state->this_frame_teleport = true;
  runtime->state->weapon_select_time = runtime->state->time;
  runtime->state->weapon_select = snapshot_of(runtime)->player_state.weapon;
}

void qa_q3_pres_player_state_runtime_check_playerstate_events(runtime_t *runtime,
    qa_q3_base_source_player_state *current, const qa_q3_base_source_player_state *previous) {
  qa_q3_pres_client_game_state *state = runtime->state;
  if (current->external_event != 0 && current->external_event != previous->external_event) {
    qa_q3_pres_client_entity *entity =
        qa_q3_pres_client_game_state_entity_at(state, current->client_num);
    entity->current_state.event = to_int32(current->external_event);
    entity->current_state.event_parm = to_int32(current->external_event_parm);
    runtime->host.events.entity_event(runtime->host.events.ctx, entity, entity->lerp_origin);
  }
  qa_q3_pres_client_entity *entity = &state->predicted_player_entity;
  for (double index = (double)to_int32(current->event_sequence - 2); index < current->event_sequence; index++) {
    if (index >= previous->event_sequence ||
        (index > (double)to_int32(previous->event_sequence - 2) &&
         qa_q3_base_player_state_slots_get(&current->events, to_int32(index) & 1) !=
             qa_q3_base_player_state_slots_get(&previous->events, to_int32(index) & 1))) {
      double event = qa_q3_base_player_state_slots_get(&current->events, to_int32(index) & 1);
      entity->current_state.event = to_int32(event);
      entity->current_state.event_parm =
          to_int32(qa_q3_base_player_state_slots_get(&current->event_parms, to_int32(index) & 1));
      runtime->host.events.entity_event(runtime->host.events.ctx, entity, entity->lerp_origin);
      qa_q3_base_player_state_slots_set(&state->predictable_events, to_int32(index) & 15, event);
      state->event_sequence = to_int32(state->event_sequence + 1);
    }
  }
}

void qa_q3_pres_player_state_runtime_check_changed_predictable_events(runtime_t *runtime,
    const qa_q3_base_source_player_state *ps) {
  qa_q3_pres_client_game_state *state = runtime->state;
  qa_q3_pres_client_entity *entity = &state->predicted_player_entity;
  for (double index = (double)to_int32(ps->event_sequence - 2); index < ps->event_sequence; index++) {
    if (index >= state->event_sequence) continue;
    if (index > (double)to_int32(state->event_sequence - 16) &&
        qa_q3_base_player_state_slots_get(&ps->events, to_int32(index) & 1) !=
            qa_q3_base_player_state_slots_get(&state->predictable_events, to_int32(index) & 15)) {
      double event = qa_q3_base_player_state_slots_get(&ps->events, to_int32(index) & 1);
      entity->current_state.event = to_int32(event);
      entity->current_state.event_parm =
          to_int32(qa_q3_base_player_state_slots_get(&ps->event_parms, to_int32(index) & 1));
      runtime->host.events.entity_event(runtime->host.events.ctx, entity, entity->lerp_origin);
      qa_q3_base_player_state_slots_set(&state->predictable_events, to_int32(index) & 15, event);
      if (runtime->host.show_miss) runtime->host.print(runtime->host.ctx, "WARNING: changed predicted event\n");
    }
  }
}

static void push_reward(runtime_t *runtime, qa_pcm_sound *sound, const qa_q3_pres_scene_shader *shader,
    double count) {
  qa_q3_pres_client_game_state *state = runtime->state;
  if (state->reward_stack < 9) {
    state->reward_stack = to_int32(state->reward_stack + 1);
    int32_t index = to_int32(state->reward_stack);
    if (index < 0 || index >= QA_Q3_PRES_REWARD_COUNT) {
      qa_range_error("Invalid source reward stack index");
    }
    state->rewards[index].sound = sound;
    state->rewards[index].shader = shader;
    state->rewards[index].count = count;
  }
}

void qa_q3_pres_player_state_runtime_check_local_sounds(runtime_t *runtime,
    const qa_q3_base_source_player_state *ps, const qa_q3_base_source_player_state *previous) {
  if (pers_get(ps, QA_Q3_BASE_PERS_TEAM) != pers_get(previous, QA_Q3_BASE_PERS_TEAM)) return;
  qa_q3_pres_player_state_host *host = &runtime->host;
  qa_q3_pres_client_game_state *state = runtime->state;
  qa_q3_pres_client_game_static_state *cgs = host->static_state;
  if (pers_get(ps, QA_Q3_BASE_PERS_HITS) > pers_get(previous, QA_Q3_BASE_PERS_HITS)) {
    int32_t armor = to_int32(pers_get(ps, QA_Q3_BASE_PERS_ATTACKEE_ARMOR)) & 255;
    int32_t health = to_int32(pers_get(ps, QA_Q3_BASE_PERS_ATTACKEE_ARMOR)) >> 8;
    if (host->product == QA_Q3_BASE_PRODUCT_MISSIONPACK && armor > 50) {
      mission_local_sound(runtime, QA_Q3_PRES_HIT_SOUND_HIGH_ARMOR, LOCAL_SOUND);
    } else if (host->product == QA_Q3_BASE_PRODUCT_MISSIONPACK && (armor != 0 || health > 100)) {
      mission_local_sound(runtime, QA_Q3_PRES_HIT_SOUND_LOW_ARMOR, LOCAL_SOUND);
    } else {
      local_sound(runtime, QA_Q3_PRES_HIT_SOUND, LOCAL_SOUND);
    }
  } else if (pers_get(ps, QA_Q3_BASE_PERS_HITS) < pers_get(previous, QA_Q3_BASE_PERS_HITS)) {
    local_sound(runtime, QA_Q3_PRES_HIT_TEAM_SOUND, LOCAL_SOUND);
  }
  double ps_health = qa_q3_base_player_state_record_health(ps);
  double previous_health = qa_q3_base_player_state_record_health(previous);
  if (ps_health < (double)to_int32(previous_health - 1) && ps_health > 0) {
    host->events.pain_event(host->events.ctx, &state->predicted_player_entity, ps_health);
  }
  if (state->intermission_started) return;
  bool rewarded = false;
  #define CHANGED(index) (pers_get(ps, index) != pers_get(previous, index))
  #define AWARD(sound_value, medal, index)                                     \
    do {                                                                       \
      push_reward(runtime, sound_value, host->medals[medal], pers_get(ps, index)); \
      rewarded = true;                                                         \
    } while (0)
  if (CHANGED(QA_Q3_BASE_PERS_CAPTURES)) {
    AWARD(host->sounds[QA_Q3_PRES_CAPTURE_AWARD_SOUND], QA_Q3_PRES_MEDAL_CAPTURE, QA_Q3_BASE_PERS_CAPTURES);
  }
  if (CHANGED(QA_Q3_BASE_PERS_IMPRESSIVE_COUNT)) {
    AWARD(host->product == QA_Q3_BASE_PRODUCT_MISSIONPACK &&
                  pers_get(ps, QA_Q3_BASE_PERS_IMPRESSIVE_COUNT) == 1
              ? host->mission_sounds[QA_Q3_PRES_FIRST_IMPRESSIVE_SOUND]
              : host->sounds[QA_Q3_PRES_IMPRESSIVE_SOUND],
          QA_Q3_PRES_MEDAL_IMPRESSIVE, QA_Q3_BASE_PERS_IMPRESSIVE_COUNT);
  }
  if (CHANGED(QA_Q3_BASE_PERS_EXCELLENT_COUNT)) {
    AWARD(host->product == QA_Q3_BASE_PRODUCT_MISSIONPACK &&
                  pers_get(ps, QA_Q3_BASE_PERS_EXCELLENT_COUNT) == 1
              ? host->mission_sounds[QA_Q3_PRES_FIRST_EXCELLENT_SOUND]
              : host->sounds[QA_Q3_PRES_EXCELLENT_SOUND],
          QA_Q3_PRES_MEDAL_EXCELLENT, QA_Q3_BASE_PERS_EXCELLENT_COUNT);
  }
  if (CHANGED(QA_Q3_BASE_PERS_GAUNTLET_FRAG_COUNT)) {
    AWARD(host->product == QA_Q3_BASE_PRODUCT_MISSIONPACK &&
                  pers_get(previous, QA_Q3_BASE_PERS_GAUNTLET_FRAG_COUNT) == 1
              ? host->mission_sounds[QA_Q3_PRES_FIRST_HUMILIATION_SOUND]
              : host->sounds[QA_Q3_PRES_HUMILIATION_SOUND],
          QA_Q3_PRES_MEDAL_GAUNTLET, QA_Q3_BASE_PERS_GAUNTLET_FRAG_COUNT);
  }
  if (CHANGED(QA_Q3_BASE_PERS_DEFEND_COUNT)) {
    AWARD(host->sounds[QA_Q3_PRES_DEFEND_SOUND], QA_Q3_PRES_MEDAL_DEFEND, QA_Q3_BASE_PERS_DEFEND_COUNT);
  }
  if (CHANGED(QA_Q3_BASE_PERS_ASSIST_COUNT)) {
    AWARD(host->sounds[QA_Q3_PRES_ASSIST_SOUND], QA_Q3_PRES_MEDAL_ASSIST, QA_Q3_BASE_PERS_ASSIST_COUNT);
  }
  if (CHANGED(QA_Q3_BASE_PERS_PLAYEREVENTS)) {
    int32_t changed_bits = to_int32(pers_get(ps, QA_Q3_BASE_PERS_PLAYEREVENTS)) ^
                           to_int32(pers_get(previous, QA_Q3_BASE_PERS_PLAYEREVENTS));
    if ((changed_bits & 1) != 0) local_sound(runtime, QA_Q3_PRES_DENIED_SOUND, ANNOUNCER);
    else if ((changed_bits & 2) != 0) local_sound(runtime, QA_Q3_PRES_HUMILIATION_SOUND, ANNOUNCER);
    else if ((changed_bits & 4) != 0) local_sound(runtime, QA_Q3_PRES_HOLY_SHIT_SOUND, ANNOUNCER);
    rewarded = true;
  }
  #undef AWARD
  #undef CHANGED
  if (cgs->game_type >= QA_Q3_BASE_GT_TEAM) {
    const qa_q3_powerup flag_powerups[] = {QA_Q3_PW_REDFLAG, QA_Q3_PW_BLUEFLAG, QA_Q3_PW_NEUTRALFLAG};
    for (size_t index = 0; index < 3; index++) {
      double current = qa_q3_base_player_state_slots_get(&ps->powerups, flag_powerups[index]);
      double before = qa_q3_base_player_state_slots_get(&previous->powerups, flag_powerups[index]);
      if (current != before && current != 0) {
        local_sound(runtime, QA_Q3_PRES_YOU_HAVE_FLAG_SOUND, ANNOUNCER);
        break;
      }
    }
  }
  if (!rewarded && state->warmup == 0 &&
      pers_get(ps, QA_Q3_BASE_PERS_RANK) != pers_get(previous, QA_Q3_BASE_PERS_RANK) &&
      cgs->game_type < QA_Q3_BASE_GT_TEAM) {
    double rank = pers_get(ps, QA_Q3_BASE_PERS_RANK);
    if (rank == 0) buffered(runtime, QA_Q3_PRES_TAKEN_LEAD_SOUND);
    else if (rank == 0x4000) buffered(runtime, QA_Q3_PRES_TIED_LEAD_SOUND);
    else if ((to_int32(pers_get(previous, QA_Q3_BASE_PERS_RANK)) & ~0x4000) == 0) {
      buffered(runtime, QA_Q3_PRES_LOST_LEAD_SOUND);
    }
  }
  if (cgs->timelimit > 0) {
    int32_t msec = to_int32(state->time - cgs->level_start_time);
    if ((to_int32(state->timelimit_warnings) & 4) == 0 &&
        msec > imul(to_int32(imul(cgs->timelimit, 60) + 2), 1000)) {
      state->timelimit_warnings = to_int32(state->timelimit_warnings) | 7;
      local_sound(runtime, QA_Q3_PRES_SUDDEN_DEATH_SOUND, ANNOUNCER);
    } else if ((to_int32(state->timelimit_warnings) & 2) == 0 &&
               msec > imul(imul(to_int32(cgs->timelimit - 1), 60), 1000)) {
      state->timelimit_warnings = to_int32(state->timelimit_warnings) | 3;
      local_sound(runtime, QA_Q3_PRES_ONE_MINUTE_SOUND, ANNOUNCER);
    } else if (cgs->timelimit > 5 && (to_int32(state->timelimit_warnings) & 1) == 0 &&
               msec > imul(imul(to_int32(cgs->timelimit - 5), 60), 1000)) {
      state->timelimit_warnings = to_int32(state->timelimit_warnings) | 1;
      local_sound(runtime, QA_Q3_PRES_FIVE_MINUTE_SOUND, ANNOUNCER);
    }
  }
  if (cgs->fraglimit > 0 && cgs->game_type < QA_Q3_BASE_GT_CTF) {
    if ((to_int32(state->fraglimit_warnings) & 4) == 0 && cgs->scores1 == to_int32(cgs->fraglimit - 1)) {
      state->fraglimit_warnings = to_int32(state->fraglimit_warnings) | 7;
      buffered(runtime, QA_Q3_PRES_ONE_FRAG_SOUND);
    } else if (cgs->fraglimit > 2 && (to_int32(state->fraglimit_warnings) & 2) == 0 &&
               cgs->scores1 == to_int32(cgs->fraglimit - 2)) {
      state->fraglimit_warnings = to_int32(state->fraglimit_warnings) | 3;
      buffered(runtime, QA_Q3_PRES_TWO_FRAG_SOUND);
    } else if (cgs->fraglimit > 3 && (to_int32(state->fraglimit_warnings) & 1) == 0 &&
               cgs->scores1 == to_int32(cgs->fraglimit - 3)) {
      state->fraglimit_warnings = to_int32(state->fraglimit_warnings) | 1;
      buffered(runtime, QA_Q3_PRES_THREE_FRAG_SOUND);
    }
  }
}

void qa_q3_pres_player_state_runtime_transition_player_state(runtime_t *runtime,
    qa_q3_base_source_player_state *current, qa_q3_base_source_player_state *previous) {
  qa_q3_pres_client_game_state *state = runtime->state;
  if (current->client_num != previous->client_num) {
    state->this_frame_teleport = true;
    qa_q3_base_player_state_record_copy_from(previous, current, QA_Q3_BASE_REPLACE_AUTHORITY);
  }
  if (current->damage_event != previous->damage_event && current->damage_count != 0) {
    qa_q3_pres_player_state_runtime_damage_feedback(runtime, current->damage_yaw, current->damage_pitch,
        current->damage_count);
  }
  if (pers_get(current, QA_Q3_BASE_PERS_SPAWN_COUNT) != pers_get(previous, QA_Q3_BASE_PERS_SPAWN_COUNT)) {
    qa_q3_pres_player_state_runtime_respawn(runtime);
  }
  if (state->map_restart) {
    qa_q3_pres_player_state_runtime_respawn(runtime);
    state->map_restart = false;
  }
  if (snapshot_of(runtime)->player_state.pm_type != QA_Q3_PM_INTERMISSION &&
      pers_get(current, QA_Q3_BASE_PERS_TEAM) != QA_Q3_BASE_TEAM_SPECTATOR) {
    qa_q3_pres_player_state_runtime_check_local_sounds(runtime, current, previous);
  }
  qa_q3_pres_player_state_runtime_check_ammo(runtime);
  qa_q3_pres_player_state_runtime_check_playerstate_events(runtime, current, previous);
  if (current->viewheight != previous->viewheight) {
    state->duck_change = f32((double)to_int32(current->viewheight - previous->viewheight));
    state->duck_time = state->time;
  }
}
