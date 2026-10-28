/* Port of src/content/q3/presentation/events.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/events.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q3/base/game/format.h"
#include "qa/content/q3/base/shared/direction_byte.h"
#include "qa/content/q3/base/shared/items.h"
#include "qa/content/q3/base/shared/trajectory.h"
#include "qa/core/common_error.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"

enum { CHAN_AUTO = 0, CHAN_VOICE = 3, CHAN_ITEM = 4, CHAN_BODY = 5, CHAN_ANNOUNCER = 7 };

static double f_(double value) { return (double)(float)value; }
static int32_t to_int32(double value) {
  if (!isfinite(value)) return 0;
  double truncated = trunc(fmod(fabs(value), 4294967296.0));
  uint32_t bits = (uint32_t)truncated;
  if (value < 0) bits = ~bits + 1u;
  int32_t result;
  memcpy(&result, &bits, sizeof result);
  return result;
}
static qa_q3_base_trajectory base_trajectory(const qa_q3_wire_trajectory *wire) {
  qa_q3_base_trajectory result = {wire->type, wire->time, wire->duration, wire->base, wire->delta};
  return result;
}

static const char *const CLIENT_EVENT_SOUND_NAMES[] = {
  "useNothingSound",
  "medkitSound",
  "landSound",
  "jumpPadSound",
  "watrInSound",
  "watrOutSound",
  "watrUnSound",
  "n_healthSound",
  "selectSound",
  "teleInSound",
  "teleOutSound",
  "respawnSound",
  "hgrenb1aSound",
  "hgrenb2aSound",
  "captureYourTeamSound",
  "captureOpponentSound",
  "returnYourTeamSound",
  "returnOpponentSound",
  "blueFlagReturnedSound",
  "redFlagReturnedSound",
  "enemyTookYourFlagSound",
  "yourTeamTookEnemyFlagSound",
  "yourBaseIsUnderAttackSound",
  "redScoredSound",
  "blueScoredSound",
  "redLeadsSound",
  "blueLeadsSound",
  "teamsTiedSound",
  "quadSound",
  "protectSound",
  "regenSound",
  "gibSound",
};
static const char *const MISSION_EVENT_SOUND_NAMES[] = {
  "useInvulnerabilitySound",
  "scoutSound",
  "guardSound",
  "doublerSound",
  "ammoregenSound",
  "wstbimplSound",
  "wstbimpmSound",
  "wstbimpdSound",
  "wstbactvSound",
  "yourTeamTookTheFlagSound",
  "enemyTookTheFlagSound",
  "kamikazeFarSound",
};
static const char *const ENTITY_EVENT_NAMES[] = {
  "EV_NONE", /* 0 */
  "EV_FOOTSTEP", /* 1 */
  "EV_FOOTSTEP_METAL", /* 2 */
  "EV_FOOTSPLASH", /* 3 */
  "EV_FOOTWADE", /* 4 */
  "EV_SWIM", /* 5 */
  "EV_STEP_4", /* 6 */
  "EV_STEP_8", /* 7 */
  "EV_STEP_12", /* 8 */
  "EV_STEP_16", /* 9 */
  "EV_FALL_SHORT", /* 10 */
  "EV_FALL_MEDIUM", /* 11 */
  "EV_FALL_FAR", /* 12 */
  "EV_JUMP_PAD", /* 13 */
  "EV_JUMP", /* 14 */
  "EV_WATER_TOUCH", /* 15 */
  "EV_WATER_LEAVE", /* 16 */
  "EV_WATER_UNDER", /* 17 */
  "EV_WATER_CLEAR", /* 18 */
  "EV_ITEM_PICKUP", /* 19 */
  "EV_GLOBAL_ITEM_PICKUP", /* 20 */
  "EV_NOAMMO", /* 21 */
  "EV_CHANGE_WEAPON", /* 22 */
  "EV_FIRE_WEAPON", /* 23 */
  "EV_USE_ITEM0", /* 24 */
  "EV_USE_ITEM1", /* 25 */
  "EV_USE_ITEM2", /* 26 */
  "EV_USE_ITEM3", /* 27 */
  "EV_USE_ITEM4", /* 28 */
  "EV_USE_ITEM5", /* 29 */
  "EV_USE_ITEM6", /* 30 */
  "EV_USE_ITEM7", /* 31 */
  "EV_USE_ITEM8", /* 32 */
  "EV_USE_ITEM9", /* 33 */
  "EV_USE_ITEM10", /* 34 */
  "EV_USE_ITEM11", /* 35 */
  "EV_USE_ITEM12", /* 36 */
  "EV_USE_ITEM13", /* 37 */
  "EV_USE_ITEM14", /* 38 */
  "EV_USE_ITEM15", /* 39 */
  "EV_ITEM_RESPAWN", /* 40 */
  "EV_ITEM_POP", /* 41 */
  "EV_PLAYER_TELEPORT_IN", /* 42 */
  "EV_PLAYER_TELEPORT_OUT", /* 43 */
  "EV_GRENADE_BOUNCE", /* 44 */
  "EV_GENERAL_SOUND", /* 45 */
  "EV_GLOBAL_SOUND", /* 46 */
  "EV_GLOBAL_TEAM_SOUND", /* 47 */
  "EV_BULLET_HIT_FLESH", /* 48 */
  "EV_BULLET_HIT_WALL", /* 49 */
  "EV_MISSILE_HIT", /* 50 */
  "EV_MISSILE_MISS", /* 51 */
  "EV_MISSILE_MISS_METAL", /* 52 */
  "EV_RAILTRAIL", /* 53 */
  "EV_SHOTGUN", /* 54 */
  "EV_BULLET", /* 55 */
  "EV_PAIN", /* 56 */
  "EV_DEATH1", /* 57 */
  "EV_DEATH2", /* 58 */
  "EV_DEATH3", /* 59 */
  "EV_OBITUARY", /* 60 */
  "EV_POWERUP_QUAD", /* 61 */
  "EV_POWERUP_BATTLESUIT", /* 62 */
  "EV_POWERUP_REGEN", /* 63 */
  "EV_GIB_PLAYER", /* 64 */
  "EV_SCOREPLUM", /* 65 */
  "EV_PROXIMITY_MINE_STICK", /* 66 */
  "EV_PROXIMITY_MINE_TRIGGER", /* 67 */
  "EV_KAMIKAZE", /* 68 */
  "EV_OBELISKEXPLODE", /* 69 */
  "EV_OBELISKPAIN", /* 70 */
  "EV_INVUL_IMPACT", /* 71 */
  "EV_JUICED", /* 72 */
  "EV_LIGHTNINGBOLT", /* 73 */
  "EV_DEBUG_LINE", /* 74 */
  "EV_STOPLOOPINGSOUND", /* 75 */
  "EV_TAUNT", /* 76 */
  "EV_TAUNT_YES", /* 77 */
  "EV_TAUNT_NO", /* 78 */
  "EV_TAUNT_FOLLOWME", /* 79 */
  "EV_TAUNT_GETFLAG", /* 80 */
  "EV_TAUNT_GUARDBASE", /* 81 */
  "EV_TAUNT_PATROL", /* 82 */
};

const char *qa_q3_pres_client_event_sound_name(qa_q3_pres_client_event_sound sound) {
  if (sound < 0 || sound >= QA_Q3_PRES_CLIENT_EVENT_SOUND_COUNT) return "";
  return CLIENT_EVENT_SOUND_NAMES[sound];
}
const char *qa_q3_pres_mission_event_sound_name(qa_q3_pres_mission_event_sound sound) {
  if (sound < 0 || sound >= QA_Q3_PRES_MISSION_EVENT_SOUND_COUNT) return "";
  return MISSION_EVENT_SOUND_NAMES[sound];
}
static const char *entity_event_name(int32_t event) {
  if (event < 0 || event >= (int32_t)(sizeof ENTITY_EVENT_NAMES / sizeof ENTITY_EVENT_NAMES[0])) return NULL;
  return ENTITY_EVENT_NAMES[event];
}

char *qa_q3_pres_place_string(double rank_value) {
  int32_t rank = to_int32(rank_value);
  bool tied = (rank & 0x4000) != 0;
  rank &= ~0x4000;
  char place_buffer[64];
  const char *place = place_buffer;
  char *formatted = NULL;
  if (rank == 1) place = "^41st^7";
  else if (rank == 2) place = "^12nd^7";
  else if (rank == 3) place = "^33rd^7";
  else {
    const char *suffix = rank == 11 || rank == 12 || rank == 13 ? "th"
        : rank % 10 == 1 ? "st" : rank % 10 == 2 ? "nd" : rank % 10 == 3 ? "rd" : "th";
    char format[16];
    snprintf(format, sizeof format, "%%i%s", suffix);
    qa_q3_game_format_arg args[1] = {qa_q3_game_format_number(rank)};
    formatted = qa_q3_game_format(format, args, 1, 32000);
    if (formatted == NULL) return NULL;
    place = formatted;
  }
  qa_q3_game_format_arg args[2] = {qa_q3_game_format_string(tied ? "Tied for " : ""),
                                   qa_q3_game_format_string(place)};
  char *result = qa_q3_game_format("%s%s", args, 2, 64);
  free(formatted);
  return result;
}

typedef qa_q3_pres_client_event_runtime runtime_t;
static qa_q3_pres_retail_snapshot *snapshot_of(runtime_t *runtime) {
  qa_q3_pres_retail_snapshot *snapshot = runtime->state->snap;
  if (snapshot == NULL) qa_range_error("CG event requires cg.snap");
  return snapshot;
}
static double center_y(runtime_t *runtime) {
  return runtime->state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK ? 144 : 143;
}
static void start_sound(runtime_t *runtime, bool has_origin, qa_vec3 origin, double entity_num,
    double channel, qa_pcm_sound *sound) {
  runtime->host.start_sound(runtime->host.ctx, has_origin, origin, entity_num, channel, sound);
}
static void media_sound(runtime_t *runtime, qa_q3_pres_client_entity *entity, double channel,
    qa_q3_pres_client_event_sound key) {
  start_sound(runtime, false, qa_math_vec3(0, 0, 0), entity->current_state.number, channel,
      runtime->host.media.sounds[key]);
}
static void custom_sound(runtime_t *runtime, qa_q3_pres_client_entity *entity, double channel,
    const char *name) {
  start_sound(runtime, false, qa_math_vec3(0, 0, 0), entity->current_state.number, channel,
      runtime->host.custom_sound(runtime->host.ctx, entity->current_state.number, name));
}

void qa_q3_pres_client_event_runtime_init(runtime_t *runtime, qa_q3_pres_client_game_state *state,
    qa_q3_pres_client_event_host host) {
  if (state->product != host.product) qa_range_error("Event host product differs from cgame state");
  runtime->state = state;
  runtime->host = host;
}

void qa_q3_pres_client_event_runtime_pain_event(runtime_t *runtime, qa_q3_pres_client_entity *entity,
    double health) {
  if (to_int32(runtime->state->time - entity->player.pose.pain_time) < 500) return;
  int level = health < 25 ? 25 : health < 50 ? 50 : health < 75 ? 75 : 100;
  char name[32];
  snprintf(name, sizeof name, "*pain%d_1.wav", level);
  custom_sound(runtime, entity, CHAN_VOICE, name);
  entity->player.pose.pain_time = runtime->state->time;
  entity->player.pose.pain_direction = !entity->player.pose.pain_direction;
}

static void item_pickup(runtime_t *runtime, double index) {
  qa_q3_pres_client_game_state *state = runtime->state;
  state->item_pickup = index;
  state->item_pickup_time = state->time;
  state->item_pickup_blend_time = state->time;
  const qa_q3_base_item_definition *item = qa_q3_base_item_at(state->product, index);
  if (item->type == QA_Q3_BASE_IT_WEAPON && runtime->host.options.autoswitch &&
      item->tag != QA_Q3_WP_MACHINEGUN) {
    state->weapon_select_time = state->time;
    state->weapon_select = item->tag;
  }
}

static void use_item(runtime_t *runtime, qa_q3_pres_client_entity *entity) {
  qa_q3_base_entity_state *es = &entity->current_state;
  int32_t item = (es->event & ~QA_Q3_BASE_EV_EVENT_BITS) - QA_Q3_EV_USE_ITEM0;
  if (item < 0 || item > QA_Q3_HI_NUM_HOLDABLE) item = 0;
  if (es->number == snapshot_of(runtime)->player_state.client_num) {
    char text[160];
    if (item == 0) {
      snprintf(text, sizeof text, "No item to use");
    } else {
      const qa_q3_base_item_definition *definition =
          qa_q3_base_find_item_for_holdable(runtime->state->product, (qa_q3_holdable)item);
      if (definition == NULL) qa_range_error("Cannot read properties of null (reading 'pickupName')");
      snprintf(text, sizeof text, "Use %.120s", definition->pickup_name != NULL ? definition->pickup_name : "null");
    }
    runtime->host.center_print(runtime->host.ctx, text, center_y(runtime), 16);
  }
  if (item == QA_Q3_HI_TELEPORTER) return;
  if (item == QA_Q3_HI_MEDKIT) {
    if (es->client_num >= 0 && es->client_num < 64) {
      runtime->host.client_info(runtime->host.client_info_ctx, es->client_num)->medkit_usage_time =
          runtime->state->time;
    }
    media_sound(runtime, entity, CHAN_BODY, QA_Q3_PRES_MEDKIT_SOUND);
    return;
  }
  if (runtime->host.product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    if (item == QA_Q3_HI_KAMIKAZE || item == QA_Q3_HI_PORTAL) return;
    if (item == QA_Q3_HI_INVULNERABILITY) {
      start_sound(runtime, false, qa_math_vec3(0, 0, 0), es->number, CHAN_BODY,
          runtime->host.mission_sounds[QA_Q3_PRES_USE_INVULNERABILITY_SOUND]);
      return;
    }
  }
  media_sound(runtime, entity, CHAN_BODY, QA_Q3_PRES_USE_NOTHING_SOUND);
}

static void sliced_name(char *out, size_t out_size, const char *name) {
  size_t take = strlen(name);
  if (take > 29) take = 29;
  if (take + 3 >= out_size) take = out_size - 3;
  memcpy(out, name, take);
  memcpy(out + take, "^7", 3);
}
static void obituary(runtime_t *runtime, qa_q3_base_entity_state *es) {
  qa_q3_pres_client_event_host *host = &runtime->host;
  int32_t target = es->other_entity_num;
  int32_t mod = es->event_parm;
  int32_t attacker = es->other_entity_num2;
  if (target < 0 || target >= 64) {
    qa_common_error(QA_COMMON_ERROR_DROP, "CG_Obituary: target out of range");
  }
  qa_q3_pres_client_info *info = host->client_info(host->client_info_ctx, target);
  const char *attacker_name = NULL;
  if (attacker < 0 || attacker >= 64) {
    attacker = 1022;
  } else {
    attacker_name = host->player_name(host->ctx, attacker);
  }
  const char *name = host->player_name(host->ctx, target);
  if (name == NULL) return;
  char target_name[48];
  sliced_name(target_name, sizeof target_name, name);
  const char *message = NULL;
  char message_buffer[96];
  switch (mod) {
    case 20: message = "suicides"; break;
    case 19: message = "cratered"; break;
    case 17: message = "was squished"; break;
    case 14: message = "sank like a rock"; break;
    case 15: message = "melted"; break;
    case 16: message = "does a back flip into the lava"; break;
    case 21: message = "saw the light"; break;
    case 22: message = "was in the wrong place"; break;
    default: break;
  }
  if (attacker == target) {
    const char *self = info->gender == QA_Q3_GENDER_FEMALE ? "herself"
        : info->gender == QA_Q3_GENDER_NEUTER ? "itself" : "himself";
    const char *possessive = info->gender == QA_Q3_GENDER_FEMALE ? "her"
        : info->gender == QA_Q3_GENDER_NEUTER ? "its" : "his";
    if (runtime->state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK && mod == 26) {
      message = "goes out with a bang";
    } else if (mod == 5) {
      snprintf(message_buffer, sizeof message_buffer, "tripped on %.20s own grenade", possessive);
      message = message_buffer;
    } else if (mod == 7) {
      snprintf(message_buffer, sizeof message_buffer, "blew %.20s up", self);
      message = message_buffer;
    } else if (mod == 9) {
      snprintf(message_buffer, sizeof message_buffer, "melted %.20s", self);
      message = message_buffer;
    } else if (mod == 13) {
      message = "should have used a smaller gun";
    } else if (runtime->state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK && mod == 25) {
      snprintf(message_buffer, sizeof message_buffer, "found %.20s prox mine",
          info->gender == QA_Q3_GENDER_NEUTER ? "it's" : possessive);
      message = message_buffer;
    } else {
      snprintf(message_buffer, sizeof message_buffer, "killed %.20s", self);
      message = message_buffer;
    }
  }
  char line[256];
  if (message != NULL) {
    snprintf(line, sizeof line, "%.80s %.120s.\n", target_name, message);
    host->print(host->ctx, line);
    return;
  }
  qa_q3_base_source_player_state *ps = &snapshot_of(runtime)->player_state;
  if (attacker == ps->client_num) {
    char text[256];
    snprintf(text, sizeof text, "You fragged %.80s", target_name);
    if (host->options.game_type < QA_Q3_BASE_GT_TEAM) {
      char *place = qa_q3_pres_place_string(
          to_int32(qa_q3_base_player_state_slots_get(&ps->persistant, QA_Q3_BASE_PERS_RANK) + 1));
      qa_q3_game_format_arg args[2] = {
          qa_q3_game_format_string(place != NULL ? place : ""),
          qa_q3_game_format_number(qa_q3_base_player_state_slots_get(&ps->persistant, QA_Q3_BASE_PERS_SCORE))};
      char *suffix_text = qa_q3_game_format("\n%s place with %i", args, 2, 32000);
      free(place);
      if (suffix_text != NULL) {
        strncat(text, suffix_text, sizeof text - strlen(text) - 1);
        free(suffix_text);
      }
    }
    if (runtime->state->product != QA_Q3_BASE_PRODUCT_MISSIONPACK ||
        !(host->options.single_player_active && host->options.camera_orbit)) {
      host->center_print(host->ctx, text, center_y(runtime), 16);
    }
  }
  char attacker_buffer[48];
  if (attacker_name == NULL) {
    attacker = 1022;
    snprintf(attacker_buffer, sizeof attacker_buffer, "noname");
  } else {
    sliced_name(attacker_buffer, sizeof attacker_buffer, attacker_name);
    if (target == snapshot_of(runtime)->player_state.client_num) {
      (void)qa_q3_pres_set_string(&runtime->state->killer_name, attacker_buffer);
    }
  }
  const char *suffix = "";
  if (attacker != 1022) {
    switch (mod) {
      case 2: message = "was pummeled by"; break;
      case 3: message = "was machinegunned by"; break;
      case 1: message = "was gunned down by"; break;
      case 4: message = "ate"; suffix = "'s grenade"; break;
      case 5: message = "was shredded by"; suffix = "'s shrapnel"; break;
      case 6: message = "ate"; suffix = "'s rocket"; break;
      case 7: message = "almost dodged"; suffix = "'s rocket"; break;
      case 8: case 9: message = "was melted by"; suffix = "'s plasmagun"; break;
      case 10: message = "was railed by"; break;
      case 11: message = "was electrocuted by"; break;
      case 12: case 13: message = "was blasted by"; suffix = "'s BFG"; break;
      case 18: message = "tried to invade"; suffix = "'s personal space"; break;
      default:
        if (mod == (runtime->state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK ? 28 : 23)) {
          message = "was caught by";
        } else if (runtime->state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK && mod == 23) {
          message = "was nailed by";
        } else if (runtime->state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK && mod == 24) {
          message = "got lead poisoning from"; suffix = "'s Chaingun";
        } else if (runtime->state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK && mod == 25) {
          message = "was too close to"; suffix = "'s Prox Mine";
        } else if (runtime->state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK && mod == 26) {
          message = "falls to"; suffix = "'s Kamikaze blast";
        } else if (runtime->state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK && mod == 27) {
          message = "was juiced by";
        } else {
          message = "was killed by";
        }
        break;
    }
    snprintf(line, sizeof line, "%.80s %.80s %.80s%.40s\n", target_name, message, attacker_buffer, suffix);
    host->print(host->ctx, line);
    return;
  }
  snprintf(line, sizeof line, "%.80s died.\n", target_name);
  host->print(host->ctx, line);
}

static void team_sound(runtime_t *runtime, int32_t event) {
  qa_q3_pres_client_event_host *host = &runtime->host;
  qa_q3_base_team team = host->client_info(host->client_info_ctx, runtime->state->client_num)->team;
  switch (event) {
    case 0: case 1:
      host->add_buffered_sound(host->ctx, host->media.sounds[
          team == (event == 0 ? QA_Q3_BASE_TEAM_RED : QA_Q3_BASE_TEAM_BLUE)
              ? QA_Q3_PRES_CAPTURE_YOUR_TEAM_SOUND : QA_Q3_PRES_CAPTURE_OPPONENT_SOUND]);
      break;
    case 2: case 3:
      host->add_buffered_sound(host->ctx, host->media.sounds[
          team == (event == 2 ? QA_Q3_BASE_TEAM_RED : QA_Q3_BASE_TEAM_BLUE)
              ? QA_Q3_PRES_RETURN_YOUR_TEAM_SOUND : QA_Q3_PRES_RETURN_OPPONENT_SOUND]);
      host->add_buffered_sound(host->ctx, host->media.sounds[
          event == 2 ? QA_Q3_PRES_BLUE_FLAG_RETURNED_SOUND : QA_Q3_PRES_RED_FLAG_RETURNED_SOUND]);
      break;
    case 4: case 5: {
      qa_q3_base_source_player_state *ps = &snapshot_of(runtime)->player_state;
      if (qa_q3_base_player_state_slots_get(&ps->powerups,
              event == 4 ? QA_Q3_PW_BLUEFLAG : QA_Q3_PW_REDFLAG) != 0 ||
          qa_q3_base_player_state_slots_get(&ps->powerups, QA_Q3_PW_NEUTRALFLAG) != 0) {
        break;
      }
      qa_q3_base_team threatened = event == 4 ? QA_Q3_BASE_TEAM_BLUE : QA_Q3_BASE_TEAM_RED;
      if (team != QA_Q3_BASE_TEAM_RED && team != QA_Q3_BASE_TEAM_BLUE) break;
      if (host->product == QA_Q3_BASE_PRODUCT_MISSIONPACK && host->options.game_type == QA_Q3_BASE_GT_1FCTF) {
        host->add_buffered_sound(host->ctx, host->mission_sounds[
            team == threatened ? QA_Q3_PRES_YOUR_TEAM_TOOK_THE_FLAG_SOUND
                               : QA_Q3_PRES_ENEMY_TOOK_THE_FLAG_SOUND]);
      } else {
        host->add_buffered_sound(host->ctx, host->media.sounds[
            team == threatened ? QA_Q3_PRES_ENEMY_TOOK_YOUR_FLAG_SOUND
                               : QA_Q3_PRES_YOUR_TEAM_TOOK_ENEMY_FLAG_SOUND]);
      }
      break;
    }
    case 6: case 7:
      if (team == (event == 6 ? QA_Q3_BASE_TEAM_RED : QA_Q3_BASE_TEAM_BLUE)) {
        host->add_buffered_sound(host->ctx, host->media.sounds[QA_Q3_PRES_YOUR_BASE_IS_UNDER_ATTACK_SOUND]);
      }
      break;
    case 8: host->add_buffered_sound(host->ctx, host->media.sounds[QA_Q3_PRES_RED_SCORED_SOUND]); break;
    case 9: host->add_buffered_sound(host->ctx, host->media.sounds[QA_Q3_PRES_BLUE_SCORED_SOUND]); break;
    case 10: host->add_buffered_sound(host->ctx, host->media.sounds[QA_Q3_PRES_RED_LEADS_SOUND]); break;
    case 11: host->add_buffered_sound(host->ctx, host->media.sounds[QA_Q3_PRES_BLUE_LEADS_SOUND]); break;
    case 12: host->add_buffered_sound(host->ctx, host->media.sounds[QA_Q3_PRES_TEAMS_TIED_SOUND]); break;
    case 13:
      if (host->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
        host->start_local_sound(host->mission_ctx, host->mission_sounds[QA_Q3_PRES_KAMIKAZE_FAR_SOUND],
            CHAN_ANNOUNCER);
      }
      break;
    default: break;
  }
}

typedef struct event_source { runtime_t *runtime; qa_q3_pres_client_entity *entity; qa_vec3 position; } event_source;
static void source_entity_event(runtime_t *runtime, qa_q3_pres_client_entity *entity, qa_vec3 position);
static void run_source(void *ctx) {
  const event_source *source = ctx;
  source_entity_event(source->runtime, source->entity, source->position);
}
void qa_q3_pres_client_event_runtime_entity_event(runtime_t *runtime, qa_q3_pres_client_entity *entity,
    qa_vec3 position) {
  event_source source = {runtime, entity, position};
  runtime->host.present_event(runtime->host.ctx, entity, position, run_source, &source);
}

static void debug_name(runtime_t *runtime, const char *name) {
  if (!runtime->host.options.debug_events) return;
  char line[80];
  snprintf(line, sizeof line, "%.60s\n", name);
  runtime->host.print(runtime->host.ctx, line);
}
static void smoke_puff_event(runtime_t *runtime, qa_vec3 origin, qa_vec3 velocity, double radius,
    qa_vec4 color, double duration, double start_time, double fade_in_time, double flags,
    const qa_q3_pres_scene_shader *shader) {
  qa_q3_pres_smoke_puff_options_mirror options;
  memset(&options, 0, sizeof options);
  options.origin = origin;
  options.velocity = velocity;
  options.radius = radius;
  options.color = color;
  options.duration = duration;
  options.start_time = start_time;
  options.fade_in_time = fade_in_time;
  options.flags = flags;
  options.shader = shader;
  (void)runtime->host.effects.smoke_puff(runtime->host.effects.ctx, options);
}

static void source_entity_event(runtime_t *runtime, qa_q3_pres_client_entity *entity, qa_vec3 position) {
  qa_q3_pres_client_game_state *state = runtime->state;
  qa_q3_pres_client_event_host *host = &runtime->host;
  qa_q3_base_entity_state *es = &entity->current_state;
  int32_t event = es->event & ~QA_Q3_BASE_EV_EVENT_BITS;
  if (host->options.debug_events) {
    qa_q3_game_format_arg args[2] = {qa_q3_game_format_number(es->number), qa_q3_game_format_number(event)};
    char *message = qa_q3_game_format("ent:%3i  event:%3i ", args, 2, 32000);
    if (message != NULL) {
      host->print(host->ctx, message);
      free(message);
    }
  }
  if (event == 0) {
    debug_name(runtime, "ZEROEVENT");
    return;
  }
  int32_t client_num = es->client_num < 0 || es->client_num >= 64 ? 0 : es->client_num;
  qa_q3_pres_client_info *info = host->client_info(host->client_info_ctx, client_num);
  bool mission_only = (event >= QA_Q3_EV_PROXIMITY_MINE_STICK && event <= QA_Q3_EV_LIGHTNINGBOLT) ||
                      event >= QA_Q3_EV_TAUNT_YES;
  const char *known = entity_event_name(event);
  if (known == NULL || event == QA_Q3_EV_USE_ITEM15 || event == QA_Q3_EV_BULLET ||
      (mission_only && host->product == QA_Q3_BASE_PRODUCT_BASEQ3)) {
    debug_name(runtime, "UNKNOWN");
    char message[64];
    snprintf(message, sizeof message, "Unknown event: %lld", (long long)event);
    qa_common_error(QA_COMMON_ERROR_DROP, message);
  }
  debug_name(runtime, event >= QA_Q3_EV_STEP_4 && event <= QA_Q3_EV_STEP_16 ? "EV_STEP"
      : event >= QA_Q3_EV_DEATH1 && event <= QA_Q3_EV_DEATH3 ? "EV_DEATHx" : known);
  double event_weapon = event == QA_Q3_EV_RAILTRAIL ? QA_Q3_WP_RAILGUN
      : event == QA_Q3_EV_SHOTGUN ? QA_Q3_WP_SHOTGUN
      : event == QA_Q3_EV_BULLET_HIT_WALL || event == QA_Q3_EV_BULLET_HIT_FLESH ? QA_Q3_WP_MACHINEGUN
      : event == QA_Q3_EV_FIRE_WEAPON || event == QA_Q3_EV_MISSILE_HIT ||
                event == QA_Q3_EV_MISSILE_MISS || event == QA_Q3_EV_MISSILE_MISS_METAL
            ? es->weapon
            : QA_Q3_WP_NONE;
  if (event_weapon != QA_Q3_WP_NONE) host->prepare_weapon(host->ctx, event_weapon);
  if (event >= QA_Q3_EV_USE_ITEM0 && event <= QA_Q3_EV_USE_ITEM14) {
    use_item(runtime, entity);
    return;
  }
  switch (event) {
    case QA_Q3_EV_FOOTSTEP:
    case QA_Q3_EV_FOOTSTEP_METAL:
    case QA_Q3_EV_FOOTSPLASH:
    case QA_Q3_EV_FOOTWADE:
    case QA_Q3_EV_SWIM:
      if (host->options.footsteps) {
        int kind = event == QA_Q3_EV_FOOTSTEP ? (int)info->footsteps
            : event == QA_Q3_EV_FOOTSTEP_METAL ? QA_Q3_PRES_FOOTSTEP_METAL : QA_Q3_PRES_FOOTSTEP_SPLASH;
        qa_pcm_sound *sound = host->media.footsteps[kind][qa_game_random_rand(host->random) & 3];
        start_sound(runtime, false, qa_math_vec3(0, 0, 0), es->number, CHAN_BODY, sound);
      }
      break;
    case QA_Q3_EV_FALL_SHORT:
    case QA_Q3_EV_FALL_MEDIUM:
    case QA_Q3_EV_FALL_FAR:
      if (event == QA_Q3_EV_FALL_SHORT) media_sound(runtime, entity, CHAN_AUTO, QA_Q3_PRES_LAND_SOUND);
      else custom_sound(runtime, entity, event == QA_Q3_EV_FALL_MEDIUM ? CHAN_VOICE : CHAN_AUTO,
          event == QA_Q3_EV_FALL_MEDIUM ? "*pain100_1.wav" : "*fall1.wav");
      if (event == QA_Q3_EV_FALL_FAR) entity->player.pose.pain_time = state->time;
      if (client_num == state->predicted_player_state.client_num) {
        state->land_change = -8 * (event - QA_Q3_EV_FALL_SHORT + 1);
        state->land_time = state->time;
      }
      break;
    case QA_Q3_EV_STEP_4:
    case QA_Q3_EV_STEP_8:
    case QA_Q3_EV_STEP_12:
    case QA_Q3_EV_STEP_16: {
      if (client_num != state->predicted_player_state.client_num) break;
      if (host->options.demo_playback ||
          (to_int32(snapshot_of(runtime)->player_state.pm_flags) & 4096) != 0 ||
          host->options.no_predict || host->options.synchronous_clients) break;
      int32_t delta = to_int32(state->time - state->step_time);
      double old_step = delta < 200 ? f_(f_(state->step_change * f_((double)to_int32(200 - (double)delta))) / 200) : 0;
      double grown = f_(old_step + 4 * (event - QA_Q3_EV_STEP_4 + 1));
      state->step_change = grown < 32 ? grown : 32;
      state->step_time = state->time;
      break;
    }
    case QA_Q3_EV_JUMP_PAD:
      smoke_puff_event(runtime, entity->lerp_origin, qa_math_vec3(0, 0, 1), 32,
          qa_math_vec4(1, 1, 1, f_(0.33)), 1000, state->time, 0, 1, host->media.smoke_puff_shader);
      start_sound(runtime, true, entity->lerp_origin, -1, CHAN_VOICE,
          host->media.sounds[QA_Q3_PRES_JUMP_PAD_SOUND]);
      custom_sound(runtime, entity, CHAN_VOICE, "*jump1.wav");
      break;
    case QA_Q3_EV_JUMP: custom_sound(runtime, entity, CHAN_VOICE, "*jump1.wav"); break;
    case QA_Q3_EV_TAUNT: custom_sound(runtime, entity, CHAN_VOICE, "*taunt.wav"); break;
    case QA_Q3_EV_TAUNT_YES:
    case QA_Q3_EV_TAUNT_NO:
    case QA_Q3_EV_TAUNT_FOLLOWME:
    case QA_Q3_EV_TAUNT_GETFLAG:
    case QA_Q3_EV_TAUNT_GUARDBASE:
    case QA_Q3_EV_TAUNT_PATROL:
      if (host->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
        host->voice_chat_local(host->mission_ctx, 1, false, es->number, 53,
            event == QA_Q3_EV_TAUNT_YES ? "yes" : event == QA_Q3_EV_TAUNT_NO ? "no"
            : event == QA_Q3_EV_TAUNT_FOLLOWME ? "followme" : event == QA_Q3_EV_TAUNT_GETFLAG ? "ongetflag"
            : event == QA_Q3_EV_TAUNT_GUARDBASE ? "ondefense" : "onpatrol");
      }
      break;
    case QA_Q3_EV_WATER_TOUCH: media_sound(runtime, entity, CHAN_AUTO, QA_Q3_PRES_WATR_IN_SOUND); break;
    case QA_Q3_EV_WATER_LEAVE: media_sound(runtime, entity, CHAN_AUTO, QA_Q3_PRES_WATR_OUT_SOUND); break;
    case QA_Q3_EV_WATER_UNDER: media_sound(runtime, entity, CHAN_AUTO, QA_Q3_PRES_WATR_UN_SOUND); break;
    case QA_Q3_EV_WATER_CLEAR: custom_sound(runtime, entity, CHAN_AUTO, "*gasp.wav"); break;
    case QA_Q3_EV_ITEM_PICKUP:
    case QA_Q3_EV_GLOBAL_ITEM_PICKUP: {
      int32_t index = es->event_parm;
      size_t item_count = 0;
      (void)qa_q3_base_item_list(state->product, &item_count);
      if (index < 1 || index >= (int32_t)item_count) break;
      const qa_q3_base_item_definition *item = qa_q3_base_item_at(state->product, index);
      if (event == QA_Q3_EV_GLOBAL_ITEM_PICKUP) {
        if (item->pickup_sound != NULL) {
          start_sound(runtime, false, qa_math_vec3(0, 0, 0), snapshot_of(runtime)->player_state.client_num,
              CHAN_AUTO, host->register_sound(host->ctx, item->pickup_sound, false));
        }
      } else if (item->type == QA_Q3_BASE_IT_POWERUP || item->type == QA_Q3_BASE_IT_TEAM) {
        media_sound(runtime, entity, CHAN_AUTO, QA_Q3_PRES_N_HEALTH_SOUND);
      } else if (item->type == QA_Q3_BASE_IT_PERSISTANT_POWERUP) {
        if (host->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
          bool has_sound = true;
          qa_q3_pres_mission_event_sound key = QA_Q3_PRES_SCOUT_SOUND;
          if (item->tag == QA_Q3_PW_SCOUT) key = QA_Q3_PRES_SCOUT_SOUND;
          else if (item->tag == QA_Q3_PW_GUARD) key = QA_Q3_PRES_GUARD_SOUND;
          else if (item->tag == QA_Q3_PW_DOUBLER) key = QA_Q3_PRES_DOUBLER_SOUND;
          else if (item->tag == QA_Q3_PW_AMMOREGEN) key = QA_Q3_PRES_AMMOREGEN_SOUND;
          else has_sound = false;
          if (has_sound) {
            start_sound(runtime, false, qa_math_vec3(0, 0, 0), es->number, CHAN_AUTO,
                host->mission_sounds[key]);
          }
        }
      } else {
        start_sound(runtime, false, qa_math_vec3(0, 0, 0), es->number, CHAN_AUTO,
            host->register_sound(host->ctx, item->pickup_sound, false));
      }
      if (es->number == snapshot_of(runtime)->player_state.client_num) item_pickup(runtime, index);
      break;
    }
    case QA_Q3_EV_NOAMMO:
      if (es->number == snapshot_of(runtime)->player_state.client_num) {
        host->weapons.out_of_ammo_change(host->weapons.ctx);
      }
      break;
    case QA_Q3_EV_CHANGE_WEAPON: media_sound(runtime, entity, CHAN_AUTO, QA_Q3_PRES_SELECT_SOUND); break;
    case QA_Q3_EV_FIRE_WEAPON: host->weapons.fire_weapon(host->weapons.ctx, entity); break;
    case QA_Q3_EV_PLAYER_TELEPORT_IN:
    case QA_Q3_EV_PLAYER_TELEPORT_OUT:
      media_sound(runtime, entity, CHAN_AUTO,
          event == QA_Q3_EV_PLAYER_TELEPORT_IN ? QA_Q3_PRES_TELE_IN_SOUND : QA_Q3_PRES_TELE_OUT_SOUND);
      (void)host->effects.spawn_effect(host->effects.ctx, position);
      break;
    case QA_Q3_EV_ITEM_POP: media_sound(runtime, entity, CHAN_AUTO, QA_Q3_PRES_RESPAWN_SOUND); break;
    case QA_Q3_EV_ITEM_RESPAWN:
      entity->misc_time = state->time;
      media_sound(runtime, entity, CHAN_AUTO, QA_Q3_PRES_RESPAWN_SOUND);
      break;
    case QA_Q3_EV_GRENADE_BOUNCE:
      media_sound(runtime, entity, CHAN_AUTO,
          (qa_game_random_rand(host->random) & 1) ? QA_Q3_PRES_HGRENB1A_SOUND : QA_Q3_PRES_HGRENB2A_SOUND);
      break;
    case QA_Q3_EV_PROXIMITY_MINE_STICK:
      if (host->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
        start_sound(runtime, false, qa_math_vec3(0, 0, 0), es->number, CHAN_AUTO,
            host->mission_sounds[(es->event_parm & 64) ? QA_Q3_PRES_WSTBIMPL_SOUND
                : (es->event_parm & 4096) ? QA_Q3_PRES_WSTBIMPM_SOUND : QA_Q3_PRES_WSTBIMPD_SOUND]);
      }
      break;
    case QA_Q3_EV_PROXIMITY_MINE_TRIGGER:
      if (host->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
        start_sound(runtime, false, qa_math_vec3(0, 0, 0), es->number, CHAN_AUTO,
            host->mission_sounds[QA_Q3_PRES_WSTBACTV_SOUND]);
      }
      break;
    case QA_Q3_EV_KAMIKAZE:
      if (host->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
        (void)host->mission_effects.kamikaze_effect(host->mission_effects.ctx, entity->lerp_origin);
      }
      break;
    case QA_Q3_EV_OBELISKEXPLODE:
      if (host->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
        host->mission_effects.obelisk_explode(host->mission_effects.ctx, entity->lerp_origin);
      }
      break;
    case QA_Q3_EV_OBELISKPAIN:
      if (host->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
        host->mission_effects.obelisk_pain(host->mission_effects.ctx, entity->lerp_origin);
      }
      break;
    case QA_Q3_EV_INVUL_IMPACT:
      if (host->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
        (void)host->mission_effects.invulnerability_impact(host->mission_effects.ctx, entity->lerp_origin,
            es->angles);
      }
      break;
    case QA_Q3_EV_JUICED:
      if (host->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
        (void)host->mission_effects.invulnerability_juiced(host->mission_effects.ctx, entity->lerp_origin);
      }
      break;
    case QA_Q3_EV_LIGHTNINGBOLT:
      if (host->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
        (void)host->mission_effects.lightning_bolt_beam(host->mission_effects.ctx, es->origin2, es->pos.base);
      }
      break;
    case QA_Q3_EV_SCOREPLUM:
      host->effects.score_plum(host->effects.ctx, es->other_entity_num, entity->lerp_origin, es->time);
      break;
    case QA_Q3_EV_MISSILE_HIT:
      host->weapons.missile_hit_player(host->weapons.ctx, es->weapon, position,
          qa_q3_base_byte_to_direction(es->event_parm), es->other_entity_num);
      break;
    case QA_Q3_EV_MISSILE_MISS:
    case QA_Q3_EV_MISSILE_MISS_METAL:
      host->weapons.missile_hit_wall(host->weapons.ctx, es->weapon, 0, position,
          qa_q3_base_byte_to_direction(es->event_parm),
          event == QA_Q3_EV_MISSILE_MISS ? QA_Q3_PRES_IMPACT_DEFAULT : QA_Q3_PRES_IMPACT_METAL);
      break;
    case QA_Q3_EV_RAILTRAIL:
      es->weapon = QA_Q3_WP_RAILGUN;
      host->weapons.rail_trail(host->weapons.ctx, client_num, es->origin2, es->pos.base);
      if (es->event_parm != 255) {
        host->weapons.missile_hit_wall(host->weapons.ctx, es->weapon, es->client_num, position,
            qa_q3_base_byte_to_direction(es->event_parm), QA_Q3_PRES_IMPACT_DEFAULT);
      }
      break;
    case QA_Q3_EV_BULLET_HIT_WALL: {
      qa_q3_pres_bullet_hit_mirror hit = {true, qa_q3_base_byte_to_direction(es->event_parm), 0};
      host->weapons.bullet(host->weapons.ctx, es->pos.base, es->other_entity_num, hit);
      break;
    }
    case QA_Q3_EV_BULLET_HIT_FLESH: {
      qa_q3_pres_bullet_hit_mirror hit = {false, {0, 0, 0}, es->event_parm};
      host->weapons.bullet(host->weapons.ctx, es->pos.base, es->other_entity_num, hit);
      break;
    }
    case QA_Q3_EV_SHOTGUN: host->weapons.shotgun_fire(host->weapons.ctx, es); break;
    case QA_Q3_EV_GENERAL_SOUND:
    case QA_Q3_EV_GLOBAL_SOUND: {
      if (es->event_parm < 0 || (size_t)es->event_parm >= host->media.game_sound_count) {
        qa_range_error("Unregistered game sound index %lld", (long long)es->event_parm);
      }
      qa_pcm_sound *sound = host->media.game_sounds[es->event_parm];
      start_sound(runtime, false, qa_math_vec3(0, 0, 0),
          event == QA_Q3_EV_GENERAL_SOUND ? (double)es->number
                                             : snapshot_of(runtime)->player_state.client_num,
          event == QA_Q3_EV_GENERAL_SOUND ? CHAN_VOICE : CHAN_AUTO,
          sound == NULL ? host->custom_sound(host->ctx, es->number,
                              host->sound_config_string(host->ctx, es->event_parm))
                        : sound);
      break;
    }
    case QA_Q3_EV_GLOBAL_TEAM_SOUND: team_sound(runtime, es->event_parm); break;
    case QA_Q3_EV_PAIN:
      if (es->number != snapshot_of(runtime)->player_state.client_num) {
        qa_q3_pres_client_event_runtime_pain_event(runtime, entity, es->event_parm);
      }
      break;
    case QA_Q3_EV_DEATH1:
    case QA_Q3_EV_DEATH2:
    case QA_Q3_EV_DEATH3: {
      char name[32];
      snprintf(name, sizeof name, "*death%lld.wav", (long long)(event - QA_Q3_EV_DEATH1 + 1));
      custom_sound(runtime, entity, CHAN_VOICE, name);
      break;
    }
    case QA_Q3_EV_OBITUARY: obituary(runtime, es); break;
    case QA_Q3_EV_POWERUP_QUAD:
    case QA_Q3_EV_POWERUP_BATTLESUIT:
    case QA_Q3_EV_POWERUP_REGEN:
      if (es->number == snapshot_of(runtime)->player_state.client_num) {
        state->powerup_active = event == QA_Q3_EV_POWERUP_QUAD ? QA_Q3_PW_QUAD
            : event == QA_Q3_EV_POWERUP_BATTLESUIT ? QA_Q3_PW_BATTLESUIT : QA_Q3_PW_REGEN;
        state->powerup_time = state->time;
      }
      media_sound(runtime, entity, CHAN_ITEM,
          event == QA_Q3_EV_POWERUP_QUAD ? QA_Q3_PRES_QUAD_SOUND
          : event == QA_Q3_EV_POWERUP_BATTLESUIT ? QA_Q3_PRES_PROTECT_SOUND : QA_Q3_PRES_REGEN_SOUND);
      break;
    case QA_Q3_EV_GIB_PLAYER:
      if (!(es->e_flags & 512)) media_sound(runtime, entity, CHAN_BODY, QA_Q3_PRES_GIB_SOUND);
      host->effects.gib_player(host->effects.ctx, entity->lerp_origin);
      break;
    case QA_Q3_EV_STOPLOOPINGSOUND:
      host->stop_looping_sound(host->ctx, es->number);
      es->loop_sound = 0;
      break;
    case QA_Q3_EV_DEBUG_LINE: host->beam(host->entities_ctx, entity); break;
    default: break;
  }
}

void qa_q3_pres_client_event_runtime_check_events(runtime_t *runtime, qa_q3_pres_client_entity *entity) {
  qa_q3_base_entity_state *es = &entity->current_state;
  if (es->e_type > QA_Q3_BASE_ET_EVENTS) {
    if (entity->previous_event != 0) return;
    if (es->e_flags & 16) es->number = es->other_entity_num;
    entity->previous_event = 1;
    es->event = es->e_type - QA_Q3_BASE_ET_EVENTS;
  } else {
    if (es->event == entity->previous_event) return;
    entity->previous_event = es->event;
    if ((es->event & ~QA_Q3_BASE_EV_EVENT_BITS) == 0) return;
  }
  entity->lerp_origin = qa_q3_base_evaluate_trajectory(base_trajectory(&es->pos),
      (double)snapshot_of(runtime)->server_time);
  runtime->host.set_entity_sound_position(runtime->host.entities_ctx, entity);
  qa_q3_pres_client_event_runtime_entity_event(runtime, entity, entity->lerp_origin);
}
