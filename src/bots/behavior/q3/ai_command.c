/* See header. Ported from id Software's game/ai_cmd.c. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/ai_command.h"
#include "ai_orders_internal.h"
#include "qa/bots/behavior/library/actions.h"
#include "qa/bots/behavior/q3/ai_combat.h"
#include "qa/bots/behavior/q3/ai_definitions.h"
#include "qa/bots/behavior/q3/ai_navigation.h"
#include "qa/bots/behavior/q3/ai_orders.h"
#include "qa/bots/behavior/q3/ai_team.h"
#include "qa/content/q3/base/game/format.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/core/game_numeric.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GT_TEAM QA_Q3_BASE_GT_TEAM
#define GT_CTF QA_Q3_BASE_GT_CTF
#define GT_1FCTF QA_Q3_BASE_GT_1FCTF
#define GT_OBELISK QA_Q3_BASE_GT_OBELISK
#define GT_HARVESTER QA_Q3_BASE_GT_HARVESTER
#define VAR_SIZE 256

/* TS BotMatchVariable aliases and the BotMatchSubtype task preference values. */
enum {
  VAR_NETNAME = 0,
  VAR_FLAG = 1,
  VAR_ADDRESSEE = 2,
  VAR_ITEM = 3,
  VAR_TEAMMATE = 4,
  VAR_TEAMNAME = 4,
  VAR_ENEMY = 4,
  VAR_KEYAREA = 5,
  VAR_POSITION = 5,
  VAR_NUMBER = 5,
  VAR_TIME = 6,
  VAR_NAME = 6,
  VAR_MORE = 6,
  SUB_DEFENDER = 1,
  SUB_ATTACKER = 2,
  SUB_ROAMER = 4
};

static void print_format(qa_game_ai_context_bots_a *ctx, const char *format,
                         const qa_q3_game_format_arg *args, size_t count) {
  char *text = qa_q3_game_format(format, args, count, 2048);
  if (text == NULL)
    qa_range_error("Bot format allocation failed");
  qa_ai_print_bots_a(ctx, text);
  free(text);
}

/** ai_cmd.c DEBUG helper; the selected release leaves its normal callers inactive. */
void qa_bot_print_team_goal_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  char name[36];
  qa_client_name_bots_a(ctx, state->src->client, 36, name);
  double time = (float)(state->src->team_goal_time - ctx->time);
  const char *action = NULL;
  bool missionpack = qa_ai_missionpack_bots_a(ctx);
  switch (state->src->ltg_type) {
  case QA_LTG_TEAMHELP:
    action = "help a team mate";
    break;
  case QA_LTG_TEAMACCOMPANY:
    action = "accompany a team mate";
    break;
  case QA_LTG_GETFLAG:
    action = "get the flag";
    break;
  case QA_LTG_RUSHBASE:
    action = "rush to the base";
    break;
  case QA_LTG_RETURNFLAG:
    action = "try to return the flag";
    break;
  case QA_LTG_ATTACKENEMYBASE:
    if (missionpack)
      action = "attack the enemy base";
    break;
  case QA_LTG_HARVEST:
    if (missionpack)
      action = "harvest";
    break;
  case QA_LTG_DEFENDKEYAREA:
    action = "defend a key area";
    break;
  case QA_LTG_GETITEM:
    action = "get an item";
    break;
  case QA_LTG_KILL:
    action = "kill someone";
    break;
  case QA_LTG_CAMP:
  case QA_LTG_CAMPORDER:
    action = "camp";
    break;
  case QA_LTG_PATROL:
    action = "patrol";
    break;
  default:
    break;
  }
  if (action == NULL) {
    if (state->src->ctf_roam_time > ctx->time) {
      time = (float)(state->src->ctf_roam_time - ctx->time);
      action = "roam";
    } else {
      qa_q3_game_format_arg args[] = {qa_q3_game_format_string(name)};
      print_format(ctx, "%s: I've got a regular goal\n", args, 1);
      return;
    }
  }
  qa_q3_game_format_arg args[] = {qa_q3_game_format_string(name), qa_q3_game_format_string(action),
                                  qa_q3_game_format_number(time)};
  print_format(ctx, "%s: I'm gonna %s for %1.0f secs\n", args, 3);
}

static int64_t maximum_clients(qa_game_ai_context_bots_a *ctx, const char *name) {
  return qa_ai_cached_max_clients_bots_a(
      ctx, ctx->command.max_clients, &ctx->command.max_client_count,
      sizeof ctx->command.max_clients / sizeof ctx->command.max_clients[0], name);
}

/* TS variable(): out holds VAR_SIZE bytes, and size is at most VAR_SIZE. */
static char *variable_sized(qa_game_ai_context_bots_a *ctx, const qa_chat_match *match, int64_t index,
                            int64_t size, char *out) {
  char *value = qa_bot_chat_library_match_variable(&ctx->library->chat, match, index, size);
  if (value == NULL)
    qa_range_error("Bot match variable allocation failed");
  snprintf(out, VAR_SIZE, "%s", value);
  free(value);
  return out;
}

static char *variable(qa_game_ai_context_bots_a *ctx, const qa_chat_match *match, int64_t index,
                      char *out) {
  return variable_sized(ctx, match, index, VAR_SIZE, out);
}

static int64_t netname_client_from_name(qa_game_ai_context_bots_a *ctx, const qa_chat_match *match) {
  char name[VAR_SIZE];
  return qa_client_from_name_bots_a(ctx, variable(ctx, match, VAR_NETNAME, name));
}

static int64_t netname_find_client(qa_game_ai_context_bots_a *ctx, const qa_chat_match *match) {
  char name[VAR_SIZE];
  return qa_find_client_by_name_bots_a(ctx, variable(ctx, match, VAR_NETNAME, name));
}

static void enter_chat(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, int64_t client,
                       qa_chat_destination destination) {
  qa_bot_chat_library_enter_chat(&ctx->library->chat, state->src->cs, client, destination, false, 0);
}

static void chat1(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, const char *type,
                  const char *variable_text) {
  const char *variables[] = {variable_text};
  qa_bot_initial_chat_bots_a(ctx, state, type, variables, 1);
}

static void debug_print_goal(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (qa_bot_library_debug_build(ctx->library))
    qa_bot_print_team_goal_bots_a(ctx, state);
}

static const char *team_leader_text(const qa_bot_state_bots_a *state) {
  return qa_bot_state_text(state, state->src->team_leader, "teamLeader");
}

static const char *subteam_text(const qa_bot_state_bots_a *state) {
  return qa_bot_state_text(state, state->src->subteam, "subteam");
}

bool qa_bot_get_item_team_goal_bots_a(qa_game_ai_context_bots_a *ctx, const char *name,
                                      qa_bot_goal_state_bots_a *goal) {
  if (name[0] == '\0')
    return false;
  int64_t index = -1;
  do {
    qa_bot_goal prior = qa_bot_goal_state_get(goal), item;
    if (!qa_bot_goal_get_level_item_goal(&ctx->library->goals, index, qa_goal_name_text(name), &prior,
                                         &item))
      return false;
    qa_bot_goal_state_copy_from(goal, item);
    index = item.number;
    if (index > 0 && (goal->flags & QA_GOAL_FLAG_DROPPED) == 0)
      return true;
  } while (index > 0);
  return false;
}

bool qa_bot_get_message_team_goal_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                         const char *name, qa_bot_goal_state_bots_a *goal) {
  if (qa_bot_get_item_team_goal_bots_a(ctx, name, goal))
    return true;
  qa_bot_waypoint_bots_a *checkpoint = qa_bot_find_way_point_bots_a(state->checkpoints, name);
  if (checkpoint == NULL)
    return false;
  *goal = checkpoint->goal;
  return true;
}

double qa_bot_get_time_bots_a(qa_game_ai_context_bots_a *ctx, const qa_chat_match *match) {
  if ((match->subtype & QA_SUB_TIME) == 0)
    return 0;
  char text[VAR_SIZE];
  qa_chat_match time_match;
  if (!qa_bot_chat_library_find_match(&ctx->library->chat, variable(ctx, match, VAR_TIME, text),
                                      QA_MATCH_TIME, &time_match))
    return 0;
  double duration;
  if (time_match.type == QA_MSG_FOREVER)
    duration = (float)99999999;
  else if (time_match.type == QA_MSG_FORAWHILE)
    duration = 600;
  else if (time_match.type == QA_MSG_FORALONGTIME)
    duration = 1800;
  else {
    variable(ctx, &time_match, VAR_TIME, text);
    duration = time_match.type == QA_MSG_MINUTES   ? (float)((double)qa_game_atof(text) * 60)
               : time_match.type == QA_MSG_SECONDS ? (double)qa_game_atof(text)
                                                   : 0;
  }
  return duration > 0 ? (double)(float)(ctx->time + duration) : 0;
}

int64_t qa_find_client_by_name_bots_a(qa_game_ai_context_bots_a *ctx, const char *name) {
  int64_t maximum = maximum_clients(ctx, "FindClientByName");
  char buffer[QA_AI_INFO_SIZE];
  for (int64_t client = 0; client < maximum; client++)
    if (qa_ai_fold_equal_bots_a(qa_client_name_bots_a(ctx, client, 1024, buffer), name))
      return client;
  for (int64_t client = 0; client < maximum; client++)
    if (qa_string_contains_bots_a(qa_client_name_bots_a(ctx, client, 1024, buffer), name, false) >= 0)
      return client;
  return -1;
}

int64_t qa_find_enemy_by_name_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                     const char *name) {
  int64_t maximum = maximum_clients(ctx, "FindEnemyByName");
  char buffer[QA_AI_INFO_SIZE];
  for (int64_t client = 0; client < maximum; client++)
    if (!qa_bot_same_team_bots_a(ctx, state, client) &&
        qa_ai_fold_equal_bots_a(qa_client_name_bots_a(ctx, client, 1024, buffer), name))
      return client;
  for (int64_t client = 0; client < maximum; client++)
    if (!qa_bot_same_team_bots_a(ctx, state, client) &&
        qa_string_contains_bots_a(qa_client_name_bots_a(ctx, client, 1024, buffer), name, false) >= 0)
      return client;
  return -1;
}

int64_t qa_num_players_on_same_team_bots_a(qa_game_ai_context_bots_a *ctx,
                                           qa_bot_state_bots_a *state) {
  int64_t maximum = maximum_clients(ctx, "NumPlayersOnSameTeam");
  int64_t count = 0;
  char info[QA_AI_INFO_SIZE];
  for (int64_t client = 0; client < maximum; client++) {
    /* The original counts i+1 after checking player i's configstring. */
    if (qa_ai_configstring_bots_a(ctx, QA_AI_CS_PLAYERS + client, info)[0] != '\0' &&
        qa_bot_same_team_bots_a(ctx, state, client + 1))
      count++;
  }
  return count;
}

bool qa_bot_get_patrol_waypoints_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                        const qa_chat_match *match) {
  qa_bot_waypoint_bots_a *points = NULL;
  int32_t flags = 0;
  char text[VAR_SIZE], area_name[VAR_SIZE];
  variable(ctx, match, VAR_KEYAREA, text);
  qa_bot_goal_state_bots_a goal;
  qa_bot_goal_state_clear(&goal);
  for (;;) {
    qa_chat_match area_match;
    if (!qa_bot_chat_library_find_match(&ctx->library->chat, text, QA_MATCH_PATROLKEYAREA,
                                        &area_match)) {
      qa_bot_action_say_team(&ctx->library->actions, state->src->client, "what do you say?");
      qa_bot_free_waypoints_bots_a(ctx, points);
      state->patrol_points = NULL;
      return false;
    }
    variable(ctx, &area_match, VAR_KEYAREA, area_name);
    if (!qa_bot_get_message_team_goal_bots_a(ctx, state, area_name, &goal)) {
      qa_bot_free_waypoints_bots_a(ctx, points);
      state->patrol_points = NULL;
      return false;
    }
    qa_bot_waypoint_bots_a *point =
        qa_bot_create_way_point_bots_a(ctx, area_name, qa_bot_v3(goal.origin), goal.area);
    if (point == NULL)
      break;
    point->next = NULL;
    qa_bot_waypoint_bots_a *tail = points;
    while (tail != NULL && tail->next != NULL)
      tail = tail->next;
    if (tail == NULL) {
      points = point;
      point->prev = NULL;
    } else {
      tail->next = point;
      point->prev = tail;
    }
    if ((area_match.subtype & QA_SUB_BACK) != 0) {
      flags = QA_PATROL_LOOP;
      break;
    }
    if ((area_match.subtype & QA_SUB_REVERSE) != 0) {
      flags = QA_PATROL_REVERSE;
      break;
    }
    if ((area_match.subtype & QA_SUB_MORE) == 0)
      break;
    variable(ctx, &area_match, VAR_MORE, text);
  }
  if (points == NULL || points->next == NULL) {
    qa_bot_action_say_team(&ctx->library->actions, state->src->client,
                           "I need more key points to patrol\n");
    qa_bot_free_waypoints_bots_a(ctx, points);
    return false;
  }
  qa_bot_free_waypoints_bots_a(ctx, state->patrol_points);
  state->patrol_points = points;
  state->current_patrol_point = points;
  state->src->patrol_flags = flags;
  return true;
}

bool qa_bot_addressed_to_bot_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                    const qa_chat_match *match) {
  char name[VAR_SIZE];
  variable(ctx, match, VAR_NETNAME, name);
  if (qa_client_on_same_team_from_name_bots_a(ctx, state, name) < 0)
    return false;
  if ((match->subtype & QA_SUB_ADDRESSED) != 0) {
    char addressed[VAR_SIZE], bot_name[128], teammate[VAR_SIZE];
    variable(ctx, match, VAR_ADDRESSEE, addressed);
    qa_client_name_bots_a(ctx, state->src->client, 128, bot_name);
    for (;;) {
      qa_chat_match addressee;
      if (!qa_bot_chat_library_find_match(&ctx->library->chat, addressed, QA_MATCH_ADDRESSEE,
                                          &addressee))
        break;
      if (addressee.type == QA_MSG_EVERYONE)
        return true;
      variable(ctx, &addressee, VAR_TEAMMATE, teammate);
      if (teammate[0] != '\0' && (qa_string_contains_bots_a(bot_name, teammate, false) >= 0 ||
                                  qa_string_contains_bots_a(subteam_text(state), teammate, false) >= 0))
        return true;
      if (addressee.type != QA_MSG_MULTIPLENAMES)
        break;
      variable(ctx, &addressee, VAR_MORE, addressed);
    }
    return false;
  }
  qa_chat_match tell;
  bool has_tell =
      qa_bot_chat_library_find_match(&ctx->library->chat, match->text, QA_MATCH_REPLYCHAT, &tell);
  if (!has_tell || tell.type != QA_MSG_CHATTELL) {
    double draw = qa_game_ai_context_random(ctx);
    double divisor = (double)(qa_num_players_on_same_team_bots_a(ctx, state) - 1);
    if (draw > (double)(float)(1.0 / divisor))
      return false;
  }
  return true;
}

qa_vec3 qa_bot_gps_to_position_bots_a(qa_game_ai_context_bots_a *ctx, const char *text) {
  size_t offset = 0;
  double coords[3];
  for (int k = 0; k < 3; k++) {
    int32_t number = 0;
    while (text[offset] == ' ')
      offset++;
    int32_t sign = text[offset] == '-' ? -1 : 1;
    if (sign == -1)
      offset++;
    while (text[offset]) {
      char character = text[offset++];
      if (character < '0' || character > '9')
        break;
      number = (int32_t)((uint32_t)number * 10u + (uint32_t)(character - 48));
    }
    int32_t value = (int32_t)((uint32_t)sign * (uint32_t)number);
    char line[32];
    snprintf(line, sizeof line, "%d\n", value);
    qa_ai_print_bots_a(ctx, line);
    coords[k] = (float)value;
  }
  return (qa_vec3){coords[0], coords[1], coords[2]};
}

static double message_time(qa_game_ai_context_bots_a *ctx) {
  return (float)(ctx->time + (float)(2 * qa_game_ai_context_random(ctx)));
}

static void ordered(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, int64_t client) {
  state->src->decisionmaker = (int32_t)client;
  state->src->ordered = true;
  state->src->order_time = (float)ctx->time;
  state->src->team_message_time = (float)message_time(ctx);
}

static void locate(qa_game_ai_context_bots_a *ctx, int64_t client, qa_bot_goal_state_bots_a *goal) {
  goal->entity = -1;
  qa_aas_entity_info_bots_a info = qa_bot_entity_info_bots_a(ctx, client);
  if (!info.valid)
    return;
  int64_t area = qa_bot_point_area_num_bots_a(ctx, info.origin);
  if (area == 0)
    return;
  goal->entity = (int32_t)client;
  goal->area = (int32_t)area;
  goal->origin = qa_bot_fv3(info.origin);
  goal->mins = (qa_bot_fvec3){-8, -8, -8};
  goal->maxs = (qa_bot_fvec3){8, 8, 8};
}

static bool team_game_addressed(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                const qa_chat_match *match) {
  return ctx->deathmatch.gametype >= GT_TEAM && qa_bot_addressed_to_bot_bots_a(ctx, state, match);
}

/* Bodies below mark the TS `netname ??= variable(NETNAME)` reads with has_netname. */
QA_BOT_MATCH_FN(qa_bot_match_help_accompany_bots_a) {
  if (!team_game_addressed(ctx, state, match))
    return;
  char teammate[VAR_SIZE], netname[VAR_SIZE];
  bool has_netname = false, other;
  int64_t client;
  variable(ctx, match, VAR_TEAMMATE, teammate);
  qa_chat_match teammate_match;
  bool found = qa_bot_chat_library_find_match(&ctx->library->chat, teammate, QA_MATCH_TEAMMATE,
                                              &teammate_match);
  if (found && teammate_match.type == QA_MSG_ME) {
    variable(ctx, match, VAR_NETNAME, netname);
    has_netname = true;
    client = qa_client_from_name_bots_a(ctx, netname);
    other = false;
  } else {
    client = qa_find_client_by_name_bots_a(ctx, teammate);
    if (client == state->src->client)
      other = false;
    else if (!qa_bot_same_team_bots_a(ctx, state, client))
      return;
    else
      other = true;
  }
  if (client < 0) {
    if (!has_netname)
      variable(ctx, match, VAR_NETNAME, netname);
    chat1(ctx, state, "whois", other ? teammate : netname);
    enter_chat(ctx, state, qa_client_from_name_bots_a(ctx, netname), QA_CHAT_TELL);
    return;
  }
  if (client == state->src->client)
    return;
  locate(ctx, client, &state->src->team_goal);
  if (state->src->team_goal.entity < 0 && (match->subtype & QA_SUB_NEARITEM) != 0) {
    char item[VAR_SIZE];
    if (!qa_bot_get_message_team_goal_bots_a(ctx, state, variable(ctx, match, VAR_ITEM, item),
                                             &state->src->team_goal))
      return;
  }
  if (state->src->team_goal.entity < 0) {
    if (other)
      chat1(ctx, state, "whereis", teammate);
    else {
      if (!has_netname) {
        variable(ctx, match, VAR_NETNAME, netname);
        has_netname = true;
      }
      chat1(ctx, state, "whereareyou", netname);
    }
    if (!has_netname)
      variable(ctx, match, VAR_NETNAME, netname);
    enter_chat(ctx, state, qa_client_from_name_bots_a(ctx, netname), QA_CHAT_TEAM);
    return;
  }
  state->src->teammate = (int32_t)client;
  variable(ctx, match, VAR_NETNAME, netname);
  client = qa_client_from_name_bots_a(ctx, netname);
  state->src->decisionmaker = (int32_t)client;
  state->src->ordered = true;
  state->src->order_time = (float)ctx->time;
  state->src->teammate_visible_time = (float)ctx->time;
  state->src->team_message_time = (float)message_time(ctx);
  state->src->team_goal_time = (float)qa_bot_get_time_bots_a(ctx, match);
  if (match->type == QA_MSG_HELP) {
    state->src->ltg_type = QA_LTG_TEAMHELP;
    if (state->src->team_goal_time == 0)
      state->src->team_goal_time = (float)(ctx->time + QA_TEAM_HELP_TIME);
  } else {
    state->src->ltg_type = QA_LTG_TEAMACCOMPANY;
    if (state->src->team_goal_time == 0)
      state->src->team_goal_time = (float)(ctx->time + QA_TEAM_ACCOMPANY_TIME);
    state->src->formation_dist = 112;
    state->src->arrive_time = 0;
    qa_bot_set_team_status_bots_a(ctx, state);
    qa_bot_remember_last_ordered_task_bots_a(ctx, state);
  }
  debug_print_goal(ctx, state);
}

QA_BOT_MATCH_FN(qa_bot_match_defend_key_area_bots_a) {
  if (!team_game_addressed(ctx, state, match))
    return;
  char keyarea[VAR_SIZE];
  if (!qa_bot_get_message_team_goal_bots_a(ctx, state, variable(ctx, match, VAR_KEYAREA, keyarea),
                                           &state->src->team_goal))
    return;
  ordered(ctx, state, netname_client_from_name(ctx, match));
  state->src->ltg_type = QA_LTG_DEFENDKEYAREA;
  state->src->team_goal_time = (float)qa_bot_get_time_bots_a(ctx, match);
  if (state->src->team_goal_time == 0)
    state->src->team_goal_time = (float)(ctx->time + QA_TEAM_DEFENDKEYAREA_TIME);
  state->src->defend_away_time = 0;
  qa_bot_set_team_status_bots_a(ctx, state);
  qa_bot_remember_last_ordered_task_bots_a(ctx, state);
  debug_print_goal(ctx, state);
}

QA_BOT_MATCH_FN(qa_bot_match_get_item_bots_a) {
  if (!team_game_addressed(ctx, state, match))
    return;
  char item[VAR_SIZE], netname[VAR_SIZE];
  if (!qa_bot_get_message_team_goal_bots_a(ctx, state, variable(ctx, match, VAR_ITEM, item),
                                           &state->src->team_goal))
    return;
  ordered(ctx, state,
          qa_client_on_same_team_from_name_bots_a(ctx, state,
                                                  variable(ctx, match, VAR_NETNAME, netname)));
  state->src->ltg_type = QA_LTG_GETITEM;
  state->src->team_goal_time = (float)(ctx->time + QA_TEAM_GETITEM_TIME);
  qa_bot_set_team_status_bots_a(ctx, state);
  debug_print_goal(ctx, state);
}

QA_BOT_MATCH_FN(qa_bot_match_camp_bots_a) {
  if (!team_game_addressed(ctx, state, match))
    return;
  char netname[VAR_SIZE], keyarea[VAR_SIZE];
  variable(ctx, match, VAR_NETNAME, netname);
  int64_t client = qa_find_client_by_name_bots_a(ctx, netname);
  if (client < 0) {
    chat1(ctx, state, "whois", netname);
    enter_chat(ctx, state, state->src->client, QA_CHAT_TEAM);
    return;
  }
  variable(ctx, match, VAR_KEYAREA, keyarea);
  if ((match->subtype & QA_SUB_THERE) != 0) {
    state->src->team_goal.entity = state->src->entity_num;
    state->src->team_goal.area = state->src->area_num;
    state->src->team_goal.origin = state->src->origin;
    state->src->team_goal.mins = (qa_bot_fvec3){-8, -8, -8};
    state->src->team_goal.maxs = (qa_bot_fvec3){8, 8, 8};
  } else if ((match->subtype & QA_SUB_HERE) != 0) {
    if (client == state->src->client)
      return;
    locate(ctx, client, &state->src->team_goal);
    if (state->src->team_goal.entity < 0) {
      chat1(ctx, state, "whereareyou", netname);
      enter_chat(ctx, state, qa_client_from_name_bots_a(ctx, netname), QA_CHAT_TELL);
      return;
    }
  } else if (!qa_bot_get_message_team_goal_bots_a(ctx, state, keyarea, &state->src->team_goal))
    return;
  ordered(ctx, state, client);
  state->src->ltg_type = QA_LTG_CAMPORDER;
  state->src->team_goal_time = (float)qa_bot_get_time_bots_a(ctx, match);
  if (state->src->team_goal_time == 0)
    state->src->team_goal_time = (float)(ctx->time + QA_TEAM_CAMP_TIME);
  state->src->arrive_time = 0;
  qa_bot_set_team_status_bots_a(ctx, state);
  qa_bot_remember_last_ordered_task_bots_a(ctx, state);
  debug_print_goal(ctx, state);
}

QA_BOT_MATCH_FN(qa_bot_match_patrol_bots_a) {
  if (!team_game_addressed(ctx, state, match) ||
      !qa_bot_get_patrol_waypoints_bots_a(ctx, state, match))
    return;
  ordered(ctx, state, netname_find_client(ctx, match));
  state->src->ltg_type = QA_LTG_PATROL;
  state->src->team_goal_time = (float)qa_bot_get_time_bots_a(ctx, match);
  if (state->src->team_goal_time == 0)
    state->src->team_goal_time = (float)(ctx->time + QA_TEAM_PATROL_TIME);
  qa_bot_set_team_status_bots_a(ctx, state);
  qa_bot_remember_last_ordered_task_bots_a(ctx, state);
  debug_print_goal(ctx, state);
}

QA_BOT_MATCH_FN(qa_bot_match_get_flag_bots_a) {
  const qa_game_ai_deathmatch_state_bots_a *goals = &ctx->deathmatch;
  int64_t gt = goals->gametype;
  if (gt == GT_CTF) {
    if (goals->ctf_red_flag.area == 0 || goals->ctf_blue_flag.area == 0)
      return;
  } else if (qa_ai_missionpack_bots_a(ctx) && gt == GT_1FCTF) {
    if (goals->ctf_neutral_flag.area == 0 || goals->ctf_red_flag.area == 0 ||
        goals->ctf_blue_flag.area == 0)
      return;
  } else
    return;
  if (!qa_bot_addressed_to_bot_bots_a(ctx, state, match))
    return;
  ordered(ctx, state, netname_find_client(ctx, match));
  state->src->ltg_type = QA_LTG_GETFLAG;
  state->src->team_goal_time = (float)(ctx->time + QA_CTF_GETFLAG_TIME);
  if (gt == GT_CTF)
    qa_bot_get_alternate_route_goal_bots_a(ctx, state, qa_bot_opposite_team_bots_a(ctx, state));
  qa_bot_set_team_status_bots_a(ctx, state);
  qa_bot_remember_last_ordered_task_bots_a(ctx, state);
  debug_print_goal(ctx, state);
}

QA_BOT_MATCH_FN(qa_bot_match_attack_enemy_base_bots_a) {
  int64_t gt = ctx->deathmatch.gametype;
  if (gt == GT_CTF)
    qa_bot_match_get_flag_bots_a(ctx, state, match);
  else if (qa_ai_missionpack_bots_a(ctx) &&
           (gt == GT_1FCTF || gt == GT_OBELISK || gt == GT_HARVESTER)) {
    if (ctx->deathmatch.red_obelisk.area == 0 || ctx->deathmatch.blue_obelisk.area == 0)
      return;
  } else
    return;
  /* Source continues after GetFlag in CTF, including its second address check. */
  if (!qa_bot_addressed_to_bot_bots_a(ctx, state, match))
    return;
  ordered(ctx, state, netname_find_client(ctx, match));
  state->src->ltg_type = QA_LTG_ATTACKENEMYBASE;
  state->src->team_goal_time = (float)(ctx->time + QA_TEAM_ATTACKENEMYBASE_TIME);
  state->src->attack_away_time = 0;
  qa_bot_set_team_status_bots_a(ctx, state);
  qa_bot_remember_last_ordered_task_bots_a(ctx, state);
  debug_print_goal(ctx, state);
}

QA_BOT_MATCH_FN(qa_bot_match_harvest_bots_a) {
  const qa_game_ai_deathmatch_state_bots_a *goals = &ctx->deathmatch;
  if (!qa_ai_missionpack_bots_a(ctx) || goals->gametype != GT_HARVESTER ||
      goals->neutral_obelisk.area == 0 || goals->red_obelisk.area == 0 ||
      goals->blue_obelisk.area == 0)
    return;
  if (!qa_bot_addressed_to_bot_bots_a(ctx, state, match))
    return;
  ordered(ctx, state, netname_find_client(ctx, match));
  state->src->ltg_type = QA_LTG_HARVEST;
  state->src->team_goal_time = (float)(ctx->time + QA_TEAM_HARVEST_TIME);
  state->src->harvest_away_time = 0;
  qa_bot_set_team_status_bots_a(ctx, state);
  qa_bot_remember_last_ordered_task_bots_a(ctx, state);
  debug_print_goal(ctx, state);
}

QA_BOT_MATCH_FN(qa_bot_match_rush_base_bots_a) {
  const qa_game_ai_deathmatch_state_bots_a *goals = &ctx->deathmatch;
  int64_t gt = goals->gametype;
  if (gt == GT_CTF) {
    if (goals->ctf_red_flag.area == 0 || goals->ctf_blue_flag.area == 0)
      return;
  } else if (qa_ai_missionpack_bots_a(ctx) && (gt == GT_1FCTF || gt == GT_HARVESTER)) {
    if (goals->red_obelisk.area == 0 || goals->blue_obelisk.area == 0)
      return;
  } else
    return;
  if (!qa_bot_addressed_to_bot_bots_a(ctx, state, match))
    return;
  ordered(ctx, state, netname_find_client(ctx, match));
  state->src->ltg_type = QA_LTG_RUSHBASE;
  state->src->team_goal_time = (float)(ctx->time + QA_CTF_RUSHBASE_TIME);
  state->src->rush_base_away_time = 0;
  qa_bot_set_team_status_bots_a(ctx, state);
  debug_print_goal(ctx, state);
}

QA_BOT_MATCH_FN(qa_bot_match_task_preference_bots_a) {
  char name[36];
  qa_client_name_bots_a(ctx, state->src->client, 36, name);
  if (!qa_ai_fold_equal_bots_a(name, team_leader_text(state)))
    return;
  int64_t teammate = netname_client_from_name(ctx, match);
  if (teammate < 0)
    return;
  int64_t preference = qa_bot_get_team_mate_task_preference_bots_a(ctx, state, teammate);
  switch (match->subtype) {
  case SUB_DEFENDER:
    preference = (preference & ~(int64_t)QA_TASK_ATTACKER) | QA_TASK_DEFENDER;
    break;
  case SUB_ATTACKER:
    preference = (preference & ~(int64_t)QA_TASK_DEFENDER) | QA_TASK_ATTACKER;
    break;
  case SUB_ROAMER:
    preference &= ~(int64_t)(QA_TASK_ATTACKER | QA_TASK_DEFENDER);
    break;
  default:
    break;
  }
  qa_bot_set_team_mate_task_preference_bots_a(ctx, state, teammate, preference);
  char easy[VAR_SIZE];
  chat1(ctx, state, "keepinmind", qa_easy_client_name_bots_a(ctx, teammate, 256, easy));
  enter_chat(ctx, state, teammate, QA_CHAT_TELL);
  qa_bot_voice_chat_only_bots_a(ctx, state, teammate, "yes");
  qa_bot_action_action(&ctx->library->actions, state->src->client, QA_BOT_ACTION_AFFIRMATIVE);
}

QA_BOT_MATCH_FN(qa_bot_match_return_flag_bots_a) {
  int64_t gt = ctx->deathmatch.gametype;
  if (gt != GT_CTF && !(qa_ai_missionpack_bots_a(ctx) && gt == GT_1FCTF))
    return;
  if (!qa_bot_addressed_to_bot_bots_a(ctx, state, match))
    return;
  ordered(ctx, state, netname_find_client(ctx, match));
  state->src->ltg_type = QA_LTG_RETURNFLAG;
  state->src->team_goal_time = (float)(ctx->time + QA_CTF_RETURNFLAG_TIME);
  state->src->rush_base_away_time = 0;
  qa_bot_set_team_status_bots_a(ctx, state);
  debug_print_goal(ctx, state);
}

QA_BOT_MATCH_FN(qa_bot_match_join_subteam_bots_a) {
  if (!team_game_addressed(ctx, state, match))
    return;
  char team[VAR_SIZE];
  variable(ctx, match, VAR_TEAMNAME, team);
  qa_bot_state_copy_subteam(state, team);
  char name[VAR_SIZE];
  variable(ctx, match, VAR_NETNAME, name);
  chat1(ctx, state, "joinedteam", team);
  enter_chat(ctx, state, qa_client_from_name_bots_a(ctx, name), QA_CHAT_TELL);
}

QA_BOT_MATCH_FN(qa_bot_match_leave_subteam_bots_a) {
  if (!team_game_addressed(ctx, state, match))
    return;
  if (subteam_text(state)[0] != '\0') {
    chat1(ctx, state, "leftteam", subteam_text(state));
    enter_chat(ctx, state, netname_client_from_name(ctx, match), QA_CHAT_TELL);
  }
  qa_bot_state_clear_subteam(state);
}

QA_BOT_MATCH_FN(qa_bot_match_which_team_bots_a) {
  if (!team_game_addressed(ctx, state, match))
    return;
  if (subteam_text(state)[0] != '\0')
    chat1(ctx, state, "inteam", subteam_text(state));
  else
    qa_bot_initial_chat_bots_a(ctx, state, "noteam", NULL, 0);
  enter_chat(ctx, state, state->src->client, QA_CHAT_TEAM);
}

QA_BOT_MATCH_FN(qa_bot_match_check_point_bots_a) {
  if (ctx->deathmatch.gametype < GT_TEAM)
    return;
  char position_text[VAR_SIZE], name[VAR_SIZE];
  variable(ctx, match, VAR_POSITION, position_text);
  int64_t client = netname_client_from_name(ctx, match);
  qa_game_vec3 scanned = qa_scan_game_vector(position_text);
  qa_vec3 position = {scanned.x, scanned.y, (float)((double)scanned.z + 0.5)};
  int64_t area = qa_bot_point_area_num_bots_a(ctx, position);
  if (area == 0) {
    if (qa_bot_addressed_to_bot_bots_a(ctx, state, match)) {
      qa_bot_initial_chat_bots_a(ctx, state, "checkpoint_invalid", NULL, 0);
      enter_chat(ctx, state, client, QA_CHAT_TELL);
    }
    return;
  }
  variable(ctx, match, VAR_NAME, name);
  qa_bot_waypoint_bots_a *old = qa_bot_find_way_point_bots_a(state->checkpoints, name);
  if (old != NULL) {
    if (old->next != NULL)
      old->next->prev = old->prev;
    if (old->prev != NULL)
      old->prev->next = old->next;
    else
      state->checkpoints = old->next;
    old->inuse = false;
    /* C leaks the replaced waypoint, so repeated chats from any player would exhaust the shared heap. */
    old->prev = NULL;
    old->next = ctx->deathmatch.free_waypoints;
    ctx->deathmatch.free_waypoints = old;
  }
  qa_bot_waypoint_bots_a *checkpoint = qa_bot_create_way_point_bots_a(ctx, name, position, area);
  if (checkpoint == NULL)
    return; /* BotCreateWayPoint already warned. */
  checkpoint->next = state->checkpoints;
  if (state->checkpoints != NULL)
    state->checkpoints->prev = checkpoint;
  state->checkpoints = checkpoint;
  if (qa_bot_addressed_to_bot_bots_a(ctx, state, match)) {
    qa_bot_fvec3 origin = checkpoint->goal.origin;
    qa_q3_game_format_arg args[] = {qa_q3_game_format_number(origin.x),
                                    qa_q3_game_format_number(origin.y),
                                    qa_q3_game_format_number(origin.z)};
    char *text = qa_q3_game_format("%1.0f %1.0f %1.0f", args, 3, 256);
    if (text == NULL)
      qa_range_error("Bot format allocation failed");
    const char *variables[] = {checkpoint->name, text};
    qa_bot_initial_chat_bots_a(ctx, state, "checkpoint_confirm", variables, 2);
    free(text);
    enter_chat(ctx, state, client, QA_CHAT_TELL);
  }
}

QA_BOT_MATCH_FN(qa_bot_match_formation_space_bots_a) {
  if (!team_game_addressed(ctx, state, match))
    return;
  char text[VAR_SIZE];
  double value = qa_game_atof(variable(ctx, match, VAR_NUMBER, text));
  double space = (match->subtype & QA_SUB_FEET) != 0
                     ? (float)((double)(float)((double)(float)0.3048 * 32) * value)
                     : (float)(32 * value);
  if (space < 48 || space > 500)
    space = 100;
  state->src->formation_dist = (float)space;
}

QA_BOT_MATCH_FN(qa_bot_match_dismiss_bots_a) {
  if (!team_game_addressed(ctx, state, match))
    return;
  int64_t client = netname_client_from_name(ctx, match);
  state->src->decisionmaker = (int32_t)client;
  state->src->ltg_type = 0;
  state->src->lead_time = 0;
  state->src->last_goal_ltg_type = 0;
  qa_bot_initial_chat_bots_a(ctx, state, "dismissed", NULL, 0);
  enter_chat(ctx, state, client, QA_CHAT_TELL);
}

QA_BOT_MATCH_FN(qa_bot_match_suicide_bots_a) {
  if (!team_game_addressed(ctx, state, match))
    return;
  qa_bot_action_command(&ctx->library->actions, state->src->client, "kill");
  qa_bot_voice_chat_bots_a(ctx, state, netname_client_from_name(ctx, match), "taunt");
  qa_bot_action_action(&ctx->library->actions, state->src->client, QA_BOT_ACTION_AFFIRMATIVE);
}

QA_BOT_MATCH_FN(qa_bot_match_start_team_leader_ship_bots_a) {
  if (ctx->deathmatch.gametype < GT_TEAM)
    return;
  char name[VAR_SIZE];
  if ((match->subtype & QA_SUB_I) != 0) {
    qa_bot_state_copy_team_leader_with_overflow(state, variable(ctx, match, VAR_NETNAME, name));
  } else {
    int64_t client = qa_find_client_by_name_bots_a(ctx, variable(ctx, match, VAR_TEAMMATE, name));
    if (client >= 0)
      qa_copy_client_name_to_team_leader_bots_a(ctx, state, client);
  }
}

QA_BOT_MATCH_FN(qa_bot_match_stop_team_leader_ship_bots_a) {
  if (ctx->deathmatch.gametype < GT_TEAM)
    return;
  char teammate[VAR_SIZE], netname[VAR_SIZE];
  variable(ctx, match, VAR_TEAMMATE, teammate);
  const char *wanted =
      (match->subtype & QA_SUB_I) != 0 ? variable(ctx, match, VAR_NETNAME, netname) : teammate;
  int64_t client = qa_find_client_by_name_bots_a(ctx, wanted);
  if (client >= 0) {
    char name[VAR_SIZE];
    const char * first = team_leader_text(state);
    const char * second = qa_client_name_bots_a(ctx, client, 256, name);
    if (qa_ai_fold_equal_bots_a(first,
                                second)) {
      qa_bot_state_clear_team_leader(state);
      ctx->command.not_leader[client] = true;
    }
  }
}

QA_BOT_MATCH_FN(qa_bot_match_who_is_team_leader_bots_a) {
  (void)match; /* TS _match */
  if (ctx->deathmatch.gametype < GT_TEAM)
    return;
  char name[VAR_SIZE];
  const char * first = qa_client_name_bots_a(ctx, state->src->client, 256, name);
  const char * second = team_leader_text(state);
  if (qa_ai_fold_equal_bots_a(first, second))
    qa_bot_action_say_team(&ctx->library->actions, state->src->client, "I'm the team leader\n");
}

/* goalName(number).slice(0, limit) into out[limit + 1]. */
static const char *goal_name_slice(qa_game_ai_context_bots_a *ctx, int64_t number, size_t limit,
                                   char *out) {
  snprintf(out, limit + 1, "%s", qa_bot_goal_goal_name(&ctx->library->goals, number));
  return out;
}

QA_BOT_MATCH_FN(qa_bot_match_what_are_you_doing_bots_a) {
  if (!qa_bot_addressed_to_bot_bots_a(ctx, state, match))
    return;
  char text[VAR_SIZE];
  bool missionpack = qa_ai_missionpack_bots_a(ctx);
  switch (state->src->ltg_type) {
  case QA_LTG_TEAMHELP:
    chat1(ctx, state, "helping", qa_easy_client_name_bots_a(ctx, state->src->teammate, 256, text));
    break;
  case QA_LTG_TEAMACCOMPANY:
    chat1(ctx, state, "accompanying",
          qa_easy_client_name_bots_a(ctx, state->src->teammate, 256, text));
    break;
  case QA_LTG_DEFENDKEYAREA:
    chat1(ctx, state, "defending", goal_name_slice(ctx, state->src->team_goal.number, 255, text));
    break;
  case QA_LTG_GETITEM:
    chat1(ctx, state, "gettingitem", goal_name_slice(ctx, state->src->team_goal.number, 255, text));
    break;
  case QA_LTG_KILL:
    chat1(ctx, state, "killing", qa_client_name_bots_a(ctx, state->src->team_goal.entity, 256, text));
    break;
  case QA_LTG_CAMP:
  case QA_LTG_CAMPORDER:
    qa_bot_initial_chat_bots_a(ctx, state, "camping", NULL, 0);
    break;
  case QA_LTG_PATROL:
    qa_bot_initial_chat_bots_a(ctx, state, "patrolling", NULL, 0);
    break;
  case QA_LTG_GETFLAG:
    qa_bot_initial_chat_bots_a(ctx, state, "capturingflag", NULL, 0);
    break;
  case QA_LTG_RUSHBASE:
    qa_bot_initial_chat_bots_a(ctx, state, "rushingbase", NULL, 0);
    break;
  case QA_LTG_RETURNFLAG:
    qa_bot_initial_chat_bots_a(ctx, state, "returningflag", NULL, 0);
    break;
  case QA_LTG_ATTACKENEMYBASE:
    qa_bot_initial_chat_bots_a(ctx, state, missionpack ? "attackingenemybase" : "roaming", NULL, 0);
    break;
  case QA_LTG_HARVEST:
    qa_bot_initial_chat_bots_a(ctx, state, missionpack ? "harvesting" : "roaming", NULL, 0);
    break;
  default:
    qa_bot_initial_chat_bots_a(ctx, state, "roaming", NULL, 0);
    break;
  }
  enter_chat(ctx, state, netname_client_from_name(ctx, match), QA_CHAT_TELL);
}

QA_BOT_MATCH_FN(qa_bot_match_what_is_my_command_bots_a) {
  (void)match; /* TS _match */
  char name[36];
  const char * first = qa_client_name_bots_a(ctx, state->src->client, 36, name);
  const char * second = team_leader_text(state);
  if (qa_ai_fold_equal_bots_a(first, second))
    state->src->force_orders = true;
}

double qa_bot_nearest_visible_item_bots_a(qa_game_ai_context_bots_a *ctx,
                                          qa_bot_state_bots_a *state, const char *name,
                                          qa_bot_goal_state_bots_a *goal) {
  double best_distance = 999999;
  int64_t index = -1;
  qa_bot_goal_state_bots_a candidate;
  qa_bot_goal_state_clear(&candidate);
  for (;;) {
    qa_bot_goal prior = qa_bot_goal_state_get(&candidate), item;
    if (!qa_bot_goal_get_level_item_goal(&ctx->library->goals, index, qa_goal_name_text(name), &prior,
                                         &item))
      break;
    qa_bot_goal_state_copy_from(&candidate, item);
    index = item.number;
    char goal_name[64];
    goal_name_slice(ctx, candidate.number, 63, goal_name);
    if (qa_ai_fold_equal_bots_a(goal_name, name)) {
      double distance = qa_length3(qa_sub3(qa_bot_v3(candidate.origin), qa_bot_v3(state->src->origin)));
      if (distance < best_distance) {
        qa_q3_base_server_trace_query query;
        memset(&query, 0, sizeof query);
        query.start = qa_bot_v3(state->src->eye);
        query.end = qa_bot_v3(candidate.origin);
        query.shape.kind = QA_Q3_BASE_SHAPE_POINT;
        query.pass_entity_num = (double)state->src->client;
        query.mask = 1 | 0x10000;
        qa_q3_base_server_trace_result trace = ctx->game->trace(ctx->game->ctx, query);
        if (trace.fraction >= 1) {
          best_distance = distance;
          *goal = candidate;
        }
      }
    }
    if (index <= 0)
      break;
  }
  return (float)best_distance;
}

QA_BOT_MATCH_FN(qa_bot_match_where_are_you_bots_a) {
  if (!team_game_addressed(ctx, state, match))
    return;
  static const char *const items[] = {
      "Shotgun", "Grenade Launcher", "Rocket Launcher", "Plasmagun", "Railgun", "Lightning Gun",
      "BFG10K", "Quad Damage", "Regeneration", "Battle Suit", "Speed", "Invisibility", "Flight",
      "Armor", "Heavy Armor", "Red Flag", "Blue Flag",
      /* missionpack additions */
      "Nailgun", "Prox Launcher", "Chaingun", "Scout", "Guard", "Doubler", "Ammo Regen",
      "Neutral Flag", "Red Obelisk", "Blue Obelisk", "Neutral Obelisk"};
  bool missionpack = qa_ai_missionpack_bots_a(ctx);
  size_t item_count = missionpack ? sizeof items / sizeof items[0] : 17;
  const char *nearest = NULL;
  double best_distance = 999999;
  qa_bot_goal_state_bots_a goal;
  qa_bot_goal_state_clear(&goal);
  for (size_t i = 0; i < item_count; i++) {
    double distance = qa_bot_nearest_visible_item_bots_a(ctx, state, items[i], &goal);
    if (distance < best_distance) {
      best_distance = distance;
      nearest = items[i];
    }
  }
  if (nearest == NULL)
    return;
  double red = 0, blue = 0;
  int64_t gt = ctx->deathmatch.gametype;
  bool flags = gt == GT_CTF || (missionpack && gt == GT_1FCTF);
  bool obelisks = missionpack && (gt == GT_OBELISK || gt == GT_HARVESTER);
  if (flags || obelisks) {
    if (!qa_ai_navigation_ready_bots_a(ctx))
      qa_range_error("BotMatch_WhereAreYou requires loaded AAS routing");
    const qa_game_ai_deathmatch_state_bots_a *goals = &ctx->deathmatch;
    qa_vec3 origin = qa_bot_v3(state->src->origin);
    red = qa_ai_area_travel_time_bots_a(ctx, state->src->area_num, origin,
                                        flags ? goals->ctf_red_flag.area : goals->red_obelisk.area);
    blue = qa_ai_area_travel_time_bots_a(ctx, state->src->area_num, origin,
                                         flags ? goals->ctf_blue_flag.area : goals->blue_obelisk.area);
  }
  double threshold = (float)((red + blue) * (double)(float)0.4);
  if (red < threshold) {
    const char *variables[] = {nearest, "red"};
    qa_bot_initial_chat_bots_a(ctx, state, "teamlocation", variables, 2);
  } else if (blue < threshold) {
    const char *variables[] = {nearest, "blue"};
    qa_bot_initial_chat_bots_a(ctx, state, "teamlocation", variables, 2);
  } else
    chat1(ctx, state, "location", nearest);
  enter_chat(ctx, state, netname_client_from_name(ctx, match), QA_CHAT_TELL);
}

QA_BOT_MATCH_FN(qa_bot_match_lead_the_way_bots_a) {
  if (!team_game_addressed(ctx, state, match))
    return;
  char netname[VAR_SIZE], teammate[VAR_SIZE];
  bool has_netname = false, has_teammate = false, other;
  int64_t client;
  if ((match->subtype & QA_SUB_SOMEONE) != 0) {
    variable(ctx, match, VAR_TEAMMATE, teammate);
    has_teammate = true;
    client = qa_find_client_by_name_bots_a(ctx, teammate);
    if (client == state->src->client)
      other = false;
    else if (!qa_bot_same_team_bots_a(ctx, state, client))
      return;
    else
      other = true;
  } else {
    variable(ctx, match, VAR_NETNAME, netname);
    has_netname = true;
    client = qa_client_from_name_bots_a(ctx, netname);
    other = false;
  }
  if (client < 0) {
    if (!has_netname)
      variable(ctx, match, VAR_NETNAME, netname);
    chat1(ctx, state, "whois", netname);
    enter_chat(ctx, state, state->src->client, QA_CHAT_TEAM);
    return;
  }
  locate(ctx, client, &state->src->lead_team_goal);
  /* Source checks teamgoal here, after writing lead_teamgoal. */
  if (state->src->team_goal.entity < 0) {
    if (other ? !has_teammate : !has_netname)
      qa_range_error("BotMatch_LeadTheWay reads an uninitialized source teammate name");
    chat1(ctx, state, other ? "whereis" : "whereareyou", other ? teammate : netname);
    enter_chat(ctx, state, state->src->client, QA_CHAT_TEAM);
    return;
  }
  state->src->lead_teammate = (int32_t)client;
  state->src->lead_time = (float)(ctx->time + QA_TEAM_LEAD_TIME);
  state->src->lead_visible_time = 0;
  state->src->lead_message_time = (float)(-message_time(ctx));
}

QA_BOT_MATCH_FN(qa_bot_match_kill_bots_a) {
  if (!team_game_addressed(ctx, state, match))
    return;
  char enemy[VAR_SIZE];
  variable(ctx, match, VAR_ENEMY, enemy);
  int64_t client = qa_find_enemy_by_name_bots_a(ctx, state, enemy);
  if (client < 0) {
    chat1(ctx, state, "whois", enemy);
    enter_chat(ctx, state, netname_client_from_name(ctx, match), QA_CHAT_TELL);
    return;
  }
  state->src->team_goal.entity = (int32_t)client;
  state->src->team_message_time = (float)message_time(ctx);
  state->src->ltg_type = QA_LTG_KILL;
  state->src->team_goal_time = (float)(ctx->time + QA_TEAM_KILL_SOMEONE);
  qa_bot_set_team_status_bots_a(ctx, state);
  debug_print_goal(ctx, state);
}

static int64_t netname36_client_from_name(qa_game_ai_context_bots_a *ctx,
                                          const qa_chat_match *match) {
  char name[VAR_SIZE];
  return qa_client_from_name_bots_a(ctx, variable_sized(ctx, match, VAR_NETNAME, 36, name));
}

QA_BOT_MATCH_FN(qa_bot_match_ctf_bots_a) {
  int64_t gt = ctx->deathmatch.gametype;
  if (gt == GT_CTF) {
    char flag[VAR_SIZE];
    variable_sized(ctx, match, VAR_FLAG, 128, flag);
    bool red_flag = qa_ai_fold_equal_bots_a(flag, "RED");
    if ((match->subtype & QA_SUB_GOTFLAG) != 0) {
      if (red_flag) {
        state->src->red_flag_status = 1;
        if (qa_bot_team_bots_a(ctx, state) == QA_Q3_BASE_TEAM_BLUE)
          state->src->flag_carrier = (int32_t)netname36_client_from_name(ctx, match);
      } else {
        state->src->blue_flag_status = 1;
        if (qa_bot_team_bots_a(ctx, state) == QA_Q3_BASE_TEAM_RED)
          state->src->flag_carrier = (int32_t)netname36_client_from_name(ctx, match);
      }
      state->src->flag_status_changed = true;
      state->src->last_flag_capture_time = (float)ctx->time;
    } else if ((match->subtype & QA_SUB_CAPTUREDFLAG) != 0) {
      state->src->red_flag_status = 0;
      state->src->blue_flag_status = 0;
      state->src->flag_carrier = 0;
      state->src->flag_status_changed = true;
    } else if ((match->subtype & QA_SUB_RETURNEDFLAG) != 0) {
      if (red_flag)
        state->src->red_flag_status = 0;
      else
        state->src->blue_flag_status = 0;
      state->src->flag_status_changed = true;
    }
  } else if (qa_ai_missionpack_bots_a(ctx) && gt == GT_1FCTF &&
             (match->subtype & QA_SUB_ONEFLAG_CTF_GOTFLAG) != 0) {
    state->src->flag_carrier = (int32_t)netname36_client_from_name(ctx, match);
  }
}

QA_BOT_MATCH_FN(qa_bot_match_enter_game_bots_a) {
  (void)state; /* TS _state */
  char name[VAR_SIZE];
  int64_t client =
      qa_find_client_by_name_bots_a(ctx, variable_sized(ctx, match, VAR_NETNAME, 36, name));
  if (client >= 0)
    ctx->command.not_leader[client] = false;
}

QA_BOT_MATCH_FN(qa_bot_match_new_leader_bots_a) {
  char name[VAR_SIZE];
  variable_sized(ctx, match, VAR_NETNAME, 36, name);
  if (!qa_bot_same_team_bots_a(ctx, state, qa_find_client_by_name_bots_a(ctx, name)))
    return;
  qa_bot_state_copy_team_leader(state, name);
}

bool qa_bot_match_message_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                 const char *message) {
  qa_chat_match m;
  if (!qa_bot_chat_library_find_match(&ctx->library->chat, message,
                                      QA_MATCH_MISC | QA_MATCH_INITIALTEAMCHAT | QA_MATCH_CTF, &m))
    return false;
  const qa_chat_match *match = &m;
  switch (match->type) {
  case QA_MSG_HELP:
  case QA_MSG_ACCOMPANY:
    qa_bot_match_help_accompany_bots_a(ctx, state, match);
    break;
  case QA_MSG_DEFENDKEYAREA:
    qa_bot_match_defend_key_area_bots_a(ctx, state, match);
    break;
  case QA_MSG_CAMP:
    qa_bot_match_camp_bots_a(ctx, state, match);
    break;
  case QA_MSG_PATROL:
    qa_bot_match_patrol_bots_a(ctx, state, match);
    break;
  case QA_MSG_GETFLAG:
    qa_bot_match_get_flag_bots_a(ctx, state, match);
    break;
  case QA_MSG_ATTACKENEMYBASE:
    if (qa_ai_missionpack_bots_a(ctx))
      qa_bot_match_attack_enemy_base_bots_a(ctx, state, match);
    else
      qa_ai_print_bots_a(ctx, "unknown match type\n");
    break;
  case QA_MSG_HARVEST:
    if (qa_ai_missionpack_bots_a(ctx))
      qa_bot_match_harvest_bots_a(ctx, state, match);
    else
      qa_ai_print_bots_a(ctx, "unknown match type\n");
    break;
  case QA_MSG_RUSHBASE:
    qa_bot_match_rush_base_bots_a(ctx, state, match);
    break;
  case QA_MSG_RETURNFLAG:
    qa_bot_match_return_flag_bots_a(ctx, state, match);
    break;
  case QA_MSG_TASKPREFERENCE:
    qa_bot_match_task_preference_bots_a(ctx, state, match);
    break;
  case QA_MSG_CTF:
    qa_bot_match_ctf_bots_a(ctx, state, match);
    break;
  case QA_MSG_GETITEM:
    qa_bot_match_get_item_bots_a(ctx, state, match);
    break;
  case QA_MSG_JOINSUBTEAM:
    qa_bot_match_join_subteam_bots_a(ctx, state, match);
    break;
  case QA_MSG_LEAVESUBTEAM:
    qa_bot_match_leave_subteam_bots_a(ctx, state, match);
    break;
  case QA_MSG_WHICHTEAM:
    qa_bot_match_which_team_bots_a(ctx, state, match);
    break;
  case QA_MSG_CHECKPOINT:
    qa_bot_match_check_point_bots_a(ctx, state, match);
    break;
  case QA_MSG_CREATENEWFORMATION:
  case QA_MSG_FORMATIONPOSITION:
    qa_bot_action_say_team(&ctx->library->actions, state->src->client,
                           "the part of my brain to create formations has been damaged");
    break;
  case QA_MSG_FORMATIONSPACE:
    qa_bot_match_formation_space_bots_a(ctx, state, match);
    break;
  case QA_MSG_DOFORMATION:
  case QA_MSG_WAIT:
    break;
  case QA_MSG_DISMISS:
    qa_bot_match_dismiss_bots_a(ctx, state, match);
    break;
  case QA_MSG_STARTTEAMLEADERSHIP:
    qa_bot_match_start_team_leader_ship_bots_a(ctx, state, match);
    break;
  case QA_MSG_STOPTEAMLEADERSHIP:
    qa_bot_match_stop_team_leader_ship_bots_a(ctx, state, match);
    break;
  case QA_MSG_WHOISTEAMLAEDER:
    qa_bot_match_who_is_team_leader_bots_a(ctx, state, match);
    break;
  case QA_MSG_WHATAREYOUDOING:
    qa_bot_match_what_are_you_doing_bots_a(ctx, state, match);
    break;
  case QA_MSG_WHATISMYCOMMAND:
    qa_bot_match_what_is_my_command_bots_a(ctx, state, match);
    break;
  case QA_MSG_WHEREAREYOU:
    qa_bot_match_where_are_you_bots_a(ctx, state, match);
    break;
  case QA_MSG_LEADTHEWAY:
    qa_bot_match_lead_the_way_bots_a(ctx, state, match);
    break;
  case QA_MSG_KILL:
    qa_bot_match_kill_bots_a(ctx, state, match);
    break;
  case QA_MSG_ENTERGAME:
    qa_bot_match_enter_game_bots_a(ctx, state, match);
    break;
  case QA_MSG_NEWLEADER:
    qa_bot_match_new_leader_bots_a(ctx, state, match);
    break;
  case QA_MSG_SUICIDE:
    qa_bot_match_suicide_bots_a(ctx, state, match);
    break;
  default:
    qa_ai_print_bots_a(ctx, "unknown match type\n");
    break;
  }
  return true;
}
