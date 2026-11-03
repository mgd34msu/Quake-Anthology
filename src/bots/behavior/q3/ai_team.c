/* See header. Ported from id Software's code/game/ai_team.c. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/ai_team.h"
#include "ai_orders_internal.h"
#include "qa/bots/behavior/library/actions.h"
#include "qa/bots/behavior/library/chat.h"
#include "qa/bots/behavior/q3/ai_chat.h"
#include "qa/bots/behavior/q3/ai_command.h"
#include "qa/bots/behavior/q3/ai_definitions.h"
#include "qa/bots/behavior/q3/ai_navigation.h"
#include "qa/bots/behavior/q3/ai_orders.h"
#include "qa/bots/behavior/q3/ai_voice.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/core/game_numeric.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TS element(): reading past the populated entries is a RangeError. */
static int64_t element(const int64_t *values, int64_t count, int64_t index) {
  if (index < 0 || index >= count)
    qa_range_error("Bot team source array index %lld is outside %lld populated entries",
                   (long long)index, (long long)count);
  return values[index];
}

static const char *leader_text(const qa_bot_state_bots_a *state) {
  return qa_bot_state_text(state, state->src->team_leader, "teamLeader");
}

static int64_t team_mates(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                          int64_t maximum, int64_t *teammates) {
  int64_t count = 0;
  for (int64_t i = 0; i < maximum && i < QA_AI_MAX_CLIENTS; i++) {
    char info[QA_AI_INFO_SIZE], name[QA_AI_INFO_SIZE], team[QA_AI_INFO_SIZE];
    qa_ai_configstring_bots_a(ctx, QA_AI_CS_PLAYERS + i, info);
    if (info[0] == '\0' || qa_ai_info_value_bots_a(info, "n", name)[0] == '\0' ||
        qa_game_atoi(qa_ai_info_value_bots_a(info, "t", team)) == QA_Q3_BASE_TEAM_SPECTATOR)
      continue;
    if (qa_bot_same_team_bots_a(ctx, state, i))
      teammates[count++] = i;
  }
  return count;
}

bool qa_bot_valid_team_leader_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  const char *leader = leader_text(state);
  return leader[0] != '\0' && qa_client_from_name_bots_a(ctx, leader) != -1;
}

int64_t qa_bot_num_team_mates_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (ctx->team.num_team_mates_max_clients == 0)
    ctx->team.num_team_mates_max_clients = qa_ai_sv_maxclients_bots_a(ctx);
  int64_t teammates[QA_BOT_TEAMMATE_CAPACITY];
  return team_mates(ctx, state, ctx->team.num_team_mates_max_clients, teammates);
}

double qa_bot_client_travel_time_to_goal_bots_a(qa_game_ai_context_bots_a *ctx, int64_t client,
                                                const qa_bot_goal *goal) {
  qa_vec3 origin;
  if (!qa_ai_client_origin_bots_a(ctx, client, &origin))
    qa_range_error("BotClientTravelTimeToGoal would read an uninitialized player state for client %lld",
                   (long long)client);
  int64_t area = qa_bot_point_area_num_bots_a(ctx, origin);
  if (area == 0)
    return 1;
  if (!qa_ai_navigation_ready_bots_a(ctx))
    return 0;
  return qa_ai_area_travel_time_bots_a(ctx, area, origin, goal->area);
}

int64_t qa_bot_sort_team_mates_by_base_travel_time_bots_a(qa_game_ai_context_bots_a *ctx,
                                                          qa_bot_state_bots_a *state,
                                                          int64_t *teammates, int64_t maximum) {
  bool red = qa_bot_team_bots_a(ctx, state) == QA_Q3_BASE_TEAM_RED;
  int64_t gt = ctx->deathmatch.gametype;
  bool flags = gt == QA_Q3_BASE_GT_CTF || gt == QA_Q3_BASE_GT_1FCTF;
  const qa_game_ai_deathmatch_state_bots_a *dm = &ctx->deathmatch;
  const qa_bot_goal *goal = flags ? (red ? &dm->ctf_red_flag : &dm->ctf_blue_flag)
                                  : (red ? &dm->red_obelisk : &dm->blue_obelisk);
  if (state->product != QA_Q3_BASE_PRODUCT_MISSIONPACK && !flags)
    qa_range_error("BotSortTeamMatesByBaseTravelTime has no source base goal in this base-game mode");
  if (ctx->team.sort_team_mates_max_clients == 0)
    ctx->team.sort_team_mates_max_clients = qa_ai_sv_maxclients_bots_a(ctx);
  int64_t candidates[QA_BOT_TEAMMATE_CAPACITY];
  int64_t candidate_count =
      team_mates(ctx, state, ctx->team.sort_team_mates_max_clients, candidates);
  double times[QA_BOT_TEAMMATE_CAPACITY];
  int64_t count = 0;
  for (int64_t c = 0; c < candidate_count; c++) {
    int64_t client = candidates[c];
    double time = qa_bot_client_travel_time_to_goal_bots_a(ctx, client, goal);
    int64_t at = 0;
    while (at < count && time >= times[at])
      at++;
    for (int64_t index = count; index > at; index--) {
      times[index] = times[index - 1];
      teammates[index] = teammates[index - 1];
    }
    times[at] = time;
    teammates[at] = client;
    count++;
    if (count >= maximum)
      break;
  }
  return count;
}

static qa_game_ai_task_preference_bots_a *preference_entry(qa_game_ai_context_bots_a *ctx,
                                                           int64_t teammate) {
  if (teammate < 0 || teammate >= QA_AI_MAX_CLIENTS)
    qa_range_error("Bot team source array index %lld is outside %d populated entries",
                   (long long)teammate, QA_AI_MAX_CLIENTS);
  return &ctx->team.task_preferences[teammate];
}

void qa_bot_set_team_mate_task_preference_bots_a(qa_game_ai_context_bots_a *ctx,
                                                 qa_bot_state_bots_a *state, int64_t teammate,
                                                 int64_t preference) {
  (void)state; /* TS _state */
  qa_game_ai_task_preference_bots_a *entry = preference_entry(ctx, teammate);
  entry->preference = preference;
  qa_client_name_bots_a(ctx, teammate, 36, entry->name);
}

int64_t qa_bot_get_team_mate_task_preference_bots_a(qa_game_ai_context_bots_a *ctx,
                                                    qa_bot_state_bots_a *state, int64_t teammate) {
  (void)state; /* TS _state */
  qa_game_ai_task_preference_bots_a *entry = preference_entry(ctx, teammate);
  if (entry->preference == 0)
    return 0;
  char name[36];
  qa_client_name_bots_a(ctx, teammate, 36, name);
  return qa_ai_fold_equal_bots_a(name, entry->name) ? entry->preference : 0;
}

int64_t qa_bot_sort_team_mates_by_task_preference_bots_a(qa_game_ai_context_bots_a *ctx,
                                                         qa_bot_state_bots_a *state,
                                                         int64_t *teammates, int64_t count) {
  int64_t defenders[QA_BOT_TEAMMATE_CAPACITY], attackers[QA_BOT_TEAMMATE_CAPACITY],
      roamers[QA_BOT_TEAMMATE_CAPACITY];
  int64_t nd = 0, na = 0, nr = 0;
  for (int64_t i = 0; i < count; i++) {
    int64_t teammate = element(teammates, count, i);
    int64_t preference = qa_bot_get_team_mate_task_preference_bots_a(ctx, state, teammate);
    if ((preference & QA_TASK_DEFENDER) != 0)
      defenders[nd++] = teammate;
    else if ((preference & QA_TASK_ATTACKER) != 0)
      attackers[na++] = teammate;
    else
      roamers[nr++] = teammate;
  }
  int64_t next = 0;
  for (int64_t i = 0; i < nd; i++)
    teammates[next++] = defenders[i];
  for (int64_t i = 0; i < nr; i++)
    teammates[next++] = roamers[i];
  for (int64_t i = 0; i < na; i++)
    teammates[next++] = attackers[i];
  return next;
}

void qa_bot_say_team_order_always_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                         int64_t client) {
  if (state->src->client == client) {
    char *text = qa_bot_chat_library_get_chat_message(&ctx->library->chat, state->src->cs, 256);
    char name[36], message[512];
    qa_client_name_bots_a(ctx, state->src->client, 36, name);
    int length = snprintf(message, sizeof message, "\x19(%s\x19)\x19: %s", name, text);
    free(text);
    if (length >= 256) {
      char report[64];
      snprintf(report, sizeof report, "Com_sprintf: overflow of %d in 256\n", length);
      qa_ai_print_bots_a(ctx, report);
      message[255] = '\0';
    }
    qa_bot_chat_library_queue_console_message(&ctx->library->chat, state->src->cs, 1,
                                              qa_chat_text(message));
  } else
    qa_bot_chat_library_enter_chat(&ctx->library->chat, state->src->cs, client, QA_CHAT_TELL, false,
                                   0);
}

void qa_bot_say_team_order_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                  int64_t client) {
  if (state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK)
    free(qa_bot_chat_library_get_chat_message(&ctx->library->chat, state->src->cs, 256));
  else
    qa_bot_say_team_order_always_bots_a(ctx, state, client);
}

static void voice_command(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, int64_t client,
                          const char *team_verb, const char *tell_verb, const char *voice) {
  if (state->product != QA_Q3_BASE_PRODUCT_MISSIONPACK)
    return;
  size_t size = strlen(voice) + 64;
  char *command = malloc(size);
  if (command == NULL)
    qa_range_error("Bot voice command allocation failed");
  if (client == -1)
    snprintf(command, size, "%s %s", team_verb, voice);
  else
    snprintf(command, size, "%s %lld %s", tell_verb, (long long)client, voice);
  qa_bot_action_command(&ctx->library->actions, state->src->client, command);
  free(command);
}

void qa_bot_voice_chat_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                              int64_t client, const char *voice) {
  voice_command(ctx, state, client, "vsay_team", "vtell", voice);
}

void qa_bot_voice_chat_only_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                   int64_t client, const char *voice) {
  voice_command(ctx, state, client, "vosay_team", "votell", voice);
}

void qa_bot_say_voice_team_order_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                        int64_t client, const char *voice) {
  if (state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK)
    qa_bot_voice_chat_bots_a(ctx, state, client, voice);
}

static int64_t sorted_team_mates(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                 int64_t *teammates) {
  int64_t count = qa_bot_sort_team_mates_by_base_travel_time_bots_a(ctx, state, teammates,
                                                                    QA_AI_MAX_CLIENTS * 4);
  qa_bot_sort_team_mates_by_task_preference_bots_a(ctx, state, teammates, count);
  return count;
}

/* TS issue() with voiceClient and voiceFirst explicit. */
static void issue_full(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, int64_t teammate,
                       const char *chat, const char *voice, int64_t voice_client, bool voice_first) {
  char name[36];
  const char *variables[] = {qa_client_name_bots_a(ctx, teammate, 36, name)};
  qa_bot_initial_chat_bots_a(ctx, state, chat, variables, 1);
  if (voice_first)
    qa_bot_say_voice_team_order_bots_a(ctx, state, voice_client, voice);
  qa_bot_say_team_order_bots_a(ctx, state, teammate);
  if (!voice_first)
    qa_bot_say_voice_team_order_bots_a(ctx, state, voice_client, voice);
}

static void issue(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, int64_t teammate,
                  const char *chat, const char *voice) {
  issue_full(ctx, state, teammate, chat, voice, teammate, false);
}

static void follow_carrier(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                           int64_t teammate, const char *carrier_name) {
  char name[36];
  qa_client_name_bots_a(ctx, teammate, 36, name);
  if (state->src->flag_carrier == state->src->client) {
    const char *variables[] = {name};
    qa_bot_initial_chat_bots_a(ctx, state, "cmd_accompanyme", variables, 1);
    qa_bot_say_voice_team_order_bots_a(ctx, state, teammate, "followme");
  } else {
    const char *variables[] = {name, carrier_name};
    qa_bot_initial_chat_bots_a(ctx, state, "cmd_accompany", variables, 2);
    qa_bot_say_voice_team_order_bots_a(ctx, state, teammate, "followflagcarrier");
  }
  qa_bot_say_team_order_bots_a(ctx, state, teammate);
}

static int64_t count_role(int64_t count, double fraction, int64_t maximum) {
  int64_t role = (int64_t)trunc((double)count * fraction + 0.5);
  return role < maximum ? role : maximum;
}

static bool aggressive(const qa_bot_state_bots_a *state) {
  return (state->src->ctf_strategy & QA_CTF_STRAT_AGRESSIVE) != 0;
}

#define EL(i) element(teammates, count, (i))

void qa_bot_ctf_orders_both_flags_not_at_base_bots_a(qa_game_ai_context_bots_a *ctx,
                                                     qa_bot_state_bots_a *state) {
  int64_t teammates[QA_BOT_TEAMMATE_CAPACITY];
  int64_t count = sorted_team_mates(ctx, state, teammates);
  int64_t carrier = state->src->flag_carrier;
  if (state->src->num_teammates == 1)
    return;
  if (state->src->num_teammates == 2) {
    int64_t other = EL(EL(0) != carrier ? 0 : 1);
    issue(ctx, state, other, "cmd_getflag", "getflag");
    return;
  }
  if (state->src->num_teammates == 3) {
    int64_t first = EL(EL(0) != carrier ? 0 : 1);
    if (carrier != -1) {
      char carrier_name[36];
      qa_client_name_bots_a(ctx, carrier, 36, carrier_name);
      follow_carrier(ctx, state, first, carrier_name);
    } else
      issue_full(ctx, state, first, "cmd_getflag", "getflag", first, true);
    int64_t last = EL(EL(2) != state->src->flag_carrier ? 2 : 1);
    issue(ctx, state, last, "cmd_getflag", "returnflag");
    return;
  }
  int64_t defenders = count_role(count, 0.4, 4), attackers = count_role(count, 0.5, 5);
  char carrier_name[36] = "";
  if (carrier != -1)
    qa_client_name_bots_a(ctx, carrier, 36, carrier_name);
  for (int64_t i = 0; i < defenders; i++) {
    int64_t teammate = EL(i);
    if (teammate == state->src->flag_carrier)
      continue;
    if (state->src->flag_carrier != -1)
      follow_carrier(ctx, state, teammate, carrier_name);
    else
      issue_full(ctx, state, teammate, "cmd_getflag", "getflag", teammate, true);
  }
  for (int64_t i = 0; i < attackers; i++) {
    int64_t teammate = EL(count - i - 1);
    if (teammate != state->src->flag_carrier)
      issue(ctx, state, teammate, "cmd_getflag", "returnflag");
  }
}

void qa_bot_ctf_orders_flag_not_at_base_bots_a(qa_game_ai_context_bots_a *ctx,
                                               qa_bot_state_bots_a *state) {
  int64_t teammates[QA_BOT_TEAMMATE_CAPACITY];
  int64_t count = sorted_team_mates(ctx, state, teammates);
  bool attack = aggressive(state);
  if (state->src->num_teammates == 1)
    return;
  if (state->src->num_teammates == 2) {
    issue(ctx, state, EL(0), attack ? "cmd_getflag" : "cmd_defendbase", "getflag");
    issue(ctx, state, EL(1), "cmd_getflag", "getflag");
    return;
  }
  if (state->src->num_teammates == 3) {
    issue(ctx, state, EL(0), "cmd_defendbase", attack ? "getflag" : "defend");
    issue(ctx, state, EL(1), "cmd_getflag", "getflag");
    issue(ctx, state, EL(2), "cmd_getflag", "getflag");
    return;
  }
  int64_t defenders = count_role(count, attack ? 0.2 : 0.3, attack ? 2 : 3);
  int64_t attackers = count_role(count, 0.7, attack ? 7 : 6);
  for (int64_t i = 0; i < defenders; i++)
    issue(ctx, state, EL(i), "cmd_defendbase", "defend");
  for (int64_t i = 0; i < attackers; i++) {
    int64_t teammate = EL(count - i - 1);
    issue_full(ctx, state, teammate, "cmd_getflag", "getflag", attack ? teammate : EL(0), false);
  }
}

void qa_bot_ctf_orders_enemy_flag_not_at_base_bots_a(qa_game_ai_context_bots_a *ctx,
                                                     qa_bot_state_bots_a *state) {
  int64_t teammates[QA_BOT_TEAMMATE_CAPACITY];
  int64_t count = sorted_team_mates(ctx, state, teammates);
  if (count == 1)
    return;
  if (count == 2 || count == 3) {
    issue(ctx, state, EL(EL(0) == state->src->flag_carrier ? 1 : 0), "cmd_defendbase", "defend");
    if (count == 3)
      issue(ctx, state, EL(EL(2) != state->src->flag_carrier ? 2 : 1), "cmd_defendbase", "defend");
    return;
  }
  int64_t defenders = count_role(count, 0.6, 6), attackers = count_role(count, 0.3, 3);
  for (int64_t i = 0; i < defenders; i++) {
    int64_t teammate = EL(i);
    if (teammate != state->src->flag_carrier)
      issue(ctx, state, teammate, "cmd_defendbase", "defend");
  }
  char carrier_name[36] = "";
  if (state->src->flag_carrier != -1)
    qa_client_name_bots_a(ctx, state->src->flag_carrier, 36, carrier_name);
  for (int64_t i = 0; i < attackers; i++) {
    int64_t teammate = EL(count - i - 1);
    if (teammate == state->src->flag_carrier)
      continue;
    if (state->src->flag_carrier != -1)
      follow_carrier(ctx, state, teammate, carrier_name);
    else
      issue_full(ctx, state, teammate, "cmd_getflag", "getflag", teammate, true);
  }
}

void qa_bot_ctf_orders_both_flags_at_base_bots_a(qa_game_ai_context_bots_a *ctx,
                                                 qa_bot_state_bots_a *state) {
  int64_t teammates[QA_BOT_TEAMMATE_CAPACITY];
  int64_t count = sorted_team_mates(ctx, state, teammates);
  bool attack = aggressive(state);
  if (count == 1)
    return;
  if (count == 2 || count == 3) {
    issue(ctx, state, EL(0), "cmd_defendbase", "defend");
    if (count == 3)
      issue(ctx, state, EL(1), attack ? "cmd_getflag" : "cmd_defendbase",
            attack ? "getflag" : "defend");
    issue(ctx, state, EL(count - 1), "cmd_getflag", "getflag");
    return;
  }
  int64_t defenders = count_role(count, attack ? 0.4 : 0.5, attack ? 4 : 5);
  int64_t attackers = count_role(count, attack ? 0.5 : 0.4, attack ? 5 : 4);
  for (int64_t i = 0; i < defenders; i++)
    issue(ctx, state, EL(i), "cmd_defendbase", "defend");
  for (int64_t i = 0; i < attackers; i++)
    issue(ctx, state, EL(count - i - 1), "cmd_getflag", "getflag");
}

void qa_bot_ctf_orders_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  int64_t status = qa_bot_team_bots_a(ctx, state) == QA_Q3_BASE_TEAM_RED
                       ? (int64_t)state->src->red_flag_status * 2 + state->src->blue_flag_status
                       : (int64_t)state->src->blue_flag_status * 2 + state->src->red_flag_status;
  switch (status) {
  case 0:
    qa_bot_ctf_orders_both_flags_at_base_bots_a(ctx, state);
    break;
  case 1:
    qa_bot_ctf_orders_enemy_flag_not_at_base_bots_a(ctx, state);
    break;
  case 2:
    qa_bot_ctf_orders_flag_not_at_base_bots_a(ctx, state);
    break;
  case 3:
    qa_bot_ctf_orders_both_flags_not_at_base_bots_a(ctx, state);
    break;
  default:
    break;
  }
}

void qa_bot_create_group_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                const int64_t *teammates, int64_t count, int64_t group_size) {
  int64_t leader = EL(0);
  char leader_name[36];
  qa_client_name_bots_a(ctx, leader, 36, leader_name);
  for (int64_t i = 1; i < group_size; i++) {
    int64_t teammate = EL(i);
    char name[36];
    qa_client_name_bots_a(ctx, teammate, 36, name);
    if (leader == state->src->client) {
      const char *variables[] = {name};
      qa_bot_initial_chat_bots_a(ctx, state, "cmd_accompanyme", variables, 1);
    } else {
      const char *variables[] = {name, leader_name};
      qa_bot_initial_chat_bots_a(ctx, state, "cmd_accompany", variables, 2);
    }
    qa_bot_say_team_order_always_bots_a(ctx, state, teammate);
  }
}

void qa_bot_team_orders_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (ctx->team.team_orders_max_clients == 0)
    ctx->team.team_orders_max_clients = qa_ai_sv_maxclients_bots_a(ctx);
  int64_t teammates[QA_BOT_TEAMMATE_CAPACITY];
  int64_t count = team_mates(ctx, state, ctx->team.team_orders_max_clients, teammates);
  switch (count) {
  case 1:
  case 2:
    break;
  case 3:
    qa_bot_create_group_bots_a(ctx, state, teammates, count, 2);
    break;
  case 4:
    qa_bot_create_group_bots_a(ctx, state, teammates, count, 2);
    qa_bot_create_group_bots_a(ctx, state, teammates + 2, count - 2, 2);
    break;
  case 5:
    qa_bot_create_group_bots_a(ctx, state, teammates, count, 2);
    qa_bot_create_group_bots_a(ctx, state, teammates + 2, count - 2, 3);
    break;
  default:
    if (count <= 10)
      for (int64_t i = 0; i < count / 2; i++)
        qa_bot_create_group_bots_a(ctx, state, teammates + i * 2, count - i * 2, 2);
    break;
  }
}

void qa_bot_1fctf_orders_flag_at_center_bots_a(qa_game_ai_context_bots_a *ctx,
                                               qa_bot_state_bots_a *state) {
  int64_t teammates[QA_BOT_TEAMMATE_CAPACITY];
  int64_t count = sorted_team_mates(ctx, state, teammates);
  bool attack = aggressive(state);
  if (count == 1)
    return;
  if (count == 2 || count == 3) {
    issue(ctx, state, EL(0), "cmd_defendbase", "defend");
    if (count == 3)
      issue_full(ctx, state, EL(1), attack ? "cmd_getflag" : "cmd_defendbase",
                 attack ? "getflag" : "defend", EL(attack ? 1 : 0), false);
    issue(ctx, state, EL(count - 1), "cmd_getflag", "getflag");
    return;
  }
  int64_t defenders = count_role(count, attack ? 0.3 : 0.5, attack ? 3 : 5);
  int64_t attackers = count_role(count, attack ? 0.6 : 0.4, attack ? 6 : 4);
  for (int64_t i = 0; i < defenders; i++)
    issue(ctx, state, EL(i), "cmd_defendbase", "defend");
  for (int64_t i = 0; i < attackers; i++)
    issue(ctx, state, EL(count - i - 1), "cmd_getflag", "getflag");
}

void qa_bot_1fctf_orders_team_has_flag_bots_a(qa_game_ai_context_bots_a *ctx,
                                              qa_bot_state_bots_a *state) {
  int64_t teammates[QA_BOT_TEAMMATE_CAPACITY];
  int64_t count = sorted_team_mates(ctx, state, teammates);
  bool attack = aggressive(state);
  if (count == 1)
    return;
  if (count == 2) {
    int64_t other = EL(EL(0) == state->src->flag_carrier ? 1 : 0);
    issue(ctx, state, other, attack ? "cmd_defendbase" : "cmd_attackenemybase",
          attack ? "defend" : "offense");
    return;
  }
  if (count == 3) {
    issue(ctx, state, EL(EL(0) != state->src->flag_carrier ? 0 : 1), "cmd_defendbase", "defend");
    int64_t other = EL(EL(2) != state->src->flag_carrier ? 2 : 1);
    if (attack || state->src->flag_carrier != -1) {
      char carrier_name[36];
      qa_client_name_bots_a(ctx, state->src->flag_carrier, 36, carrier_name);
      follow_carrier(ctx, state, other, carrier_name);
    } else
      issue_full(ctx, state, other, "cmd_getflag", "getflag", other, true);
    return;
  }
  int64_t defenders = count_role(count, attack ? 0.2 : 0.3, attack ? 2 : 3);
  int64_t attackers = count_role(count, attack ? 0.8 : 0.7, attack ? 8 : 7);
  for (int64_t i = 0; i < defenders; i++) {
    int64_t teammate = EL(i);
    if (teammate != state->src->flag_carrier)
      issue(ctx, state, teammate, "cmd_defendbase", "defend");
  }
  char carrier_name[36] = "";
  if (attack || state->src->flag_carrier != -1)
    qa_client_name_bots_a(ctx, state->src->flag_carrier, 36, carrier_name);
  for (int64_t i = 0; i < attackers; i++) {
    int64_t teammate = EL(count - i - 1);
    if (teammate == state->src->flag_carrier)
      continue;
    if (attack || state->src->flag_carrier != -1)
      follow_carrier(ctx, state, teammate, carrier_name);
    else
      issue(ctx, state, teammate, "cmd_getflag", "getflag");
  }
}

void qa_bot_1fctf_orders_enemy_has_flag_bots_a(qa_game_ai_context_bots_a *ctx,
                                               qa_bot_state_bots_a *state) {
  int64_t teammates[QA_BOT_TEAMMATE_CAPACITY];
  int64_t count = sorted_team_mates(ctx, state, teammates);
  bool attack = aggressive(state);
  if (count == 1)
    return;
  if (count == 2 || count == 3) {
    issue(ctx, state, EL(0), "cmd_defendbase", "defend");
    issue(ctx, state, EL(1), "cmd_defendbase", "defend");
    if (count == 3)
      issue(ctx, state, EL(2), attack ? "cmd_returnflag" : "cmd_defendbase",
            attack ? "getflag" : "defend");
    return;
  }
  int64_t defenders = count_role(count, attack ? 0.7 : 0.8, 8);
  int64_t attackers = count_role(count, attack ? 0.2 : 0.1, 2);
  for (int64_t i = 0; i < defenders; i++)
    issue(ctx, state, EL(i), "cmd_defendbase", "defend");
  for (int64_t i = 0; i < attackers; i++)
    issue(ctx, state, EL(count - i - 1), "cmd_returnflag", "getflag");
}

void qa_bot_1fctf_orders_enemy_dropped_flag_bots_a(qa_game_ai_context_bots_a *ctx,
                                                   qa_bot_state_bots_a *state) {
  int64_t teammates[QA_BOT_TEAMMATE_CAPACITY];
  int64_t count = sorted_team_mates(ctx, state, teammates);
  bool attack = aggressive(state);
  if (count == 1)
    return;
  if (count == 2 || count == 3) {
    issue(ctx, state, EL(0), "cmd_defendbase", "defend");
    if (count == 3)
      issue(ctx, state, EL(1), attack ? "cmd_getflag" : "cmd_defendbase",
            attack ? "getflag" : "defend");
    issue(ctx, state, EL(count - 1), "cmd_getflag", "getflag");
    return;
  }
  int64_t defenders = count_role(count, attack ? 0.3 : 0.5, attack ? 3 : 5);
  int64_t attackers = count_role(count, attack ? 0.6 : 0.4, attack ? 6 : 4);
  for (int64_t i = 0; i < defenders; i++)
    issue(ctx, state, EL(i), "cmd_defendbase", "defend");
  for (int64_t i = 0; i < attackers; i++)
    issue(ctx, state, EL(count - i - 1), "cmd_getflag", attack ? "defend" : "getflag");
}

void qa_bot_1fctf_orders_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  switch (state->src->neutral_flag_status) {
  case 0:
    qa_bot_1fctf_orders_flag_at_center_bots_a(ctx, state);
    break;
  case 1:
    qa_bot_1fctf_orders_team_has_flag_bots_a(ctx, state);
    break;
  case 2:
    qa_bot_1fctf_orders_enemy_has_flag_bots_a(ctx, state);
    break;
  case 3:
    qa_bot_1fctf_orders_enemy_dropped_flag_bots_a(ctx, state);
    break;
  default:
    break;
  }
}

static void obelisk_or_harvester_orders(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                        const char *attack_command) {
  int64_t teammates[QA_BOT_TEAMMATE_CAPACITY];
  int64_t count = sorted_team_mates(ctx, state, teammates);
  bool attack = aggressive(state);
  if (count == 1)
    return;
  if (count == 2 || count == 3) {
    issue(ctx, state, EL(0), "cmd_defendbase", "defend");
    if (count == 3)
      issue(ctx, state, EL(1), attack ? attack_command : "cmd_defendbase",
            attack ? "offense" : "defend");
    issue(ctx, state, EL(count - 1), attack_command, "offense");
    return;
  }
  int64_t defenders = count_role(count, attack ? 0.3 : 0.5, attack ? 3 : 5);
  int64_t attackers = count_role(count, attack ? 0.7 : 0.4, attack ? 7 : 4);
  for (int64_t i = 0; i < defenders; i++)
    issue(ctx, state, EL(i), "cmd_defendbase", "defend");
  for (int64_t i = 0; i < attackers; i++)
    issue(ctx, state, EL(count - i - 1), attack_command, "offense");
}

void qa_bot_obelisk_orders_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  obelisk_or_harvester_orders(ctx, state, "cmd_attackenemybase");
}
void qa_bot_harvester_orders_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  obelisk_or_harvester_orders(ctx, state, "cmd_harvest");
}

bool qa_find_human_team_leader_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  for (int64_t i = 0; i < QA_AI_MAX_CLIENTS; i++) {
    qa_bot_observed_entity_bots_a entity = ctx->game->entity(ctx->game->ctx, i);
    if (entity.present && !entity.bot && !ctx->command.not_leader[i] &&
        qa_bot_same_team_bots_a(ctx, state, i)) {
      qa_copy_client_name_to_team_leader_bots_a(ctx, state, i);
      if (!qa_bot_set_last_ordered_task_bots_a(ctx, state))
        qa_bot_voice_chat_defend_bots_a(ctx, state, i, 2);
      return true;
    }
  }
  return false;
}

static void team_chat(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, const char *type) {
  qa_bot_initial_chat_bots_a(ctx, state, type, NULL, 0);
  qa_bot_chat_library_enter_chat(&ctx->library->chat, state->src->cs, 0, QA_CHAT_TEAM, false, 0);
}

void qa_bot_team_ai_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  int64_t gt = ctx->deathmatch.gametype;
  double time = ctx->time;
  if (gt < QA_Q3_BASE_GT_TEAM)
    return;
  if (!qa_bot_valid_team_leader_bots_a(ctx, state) && !qa_find_human_team_leader_bots_a(ctx, state)) {
    if (state->src->ask_team_leader_time == 0 && state->src->become_team_leader_time == 0) {
      if ((float)(state->src->enter_game_time + 10.0) > time)
        state->src->ask_team_leader_time =
            (float)((float)(time + 5) + (float)(qa_game_ai_context_random(ctx) * 10));
      else
        state->src->become_team_leader_time =
            (float)((float)(time + 5) + (float)(qa_game_ai_context_random(ctx) * 10));
    }
    if (state->src->ask_team_leader_time != 0 && state->src->ask_team_leader_time < time) {
      team_chat(ctx, state, "whoisteamleader");
      state->src->ask_team_leader_time = 0;
      state->src->become_team_leader_time =
          (float)((float)(time + 8) + (float)(qa_game_ai_context_random(ctx) * 10));
    }
    if (state->src->become_team_leader_time != 0 && state->src->become_team_leader_time < time) {
      team_chat(ctx, state, "iamteamleader");
      qa_bot_say_voice_team_order_bots_a(ctx, state, -1, "startleader");
      char name[36];
      qa_bot_state_copy_team_leader_with_overflow(
          state, qa_client_name_bots_a(ctx, state->src->client, 36, name));
      state->src->become_team_leader_time = 0;
    }
    return;
  }
  state->src->ask_team_leader_time = 0;
  state->src->become_team_leader_time = 0;
  char own_name[36];
  qa_client_name_bots_a(ctx, state->src->client, 36, own_name);
  if (!qa_ai_fold_equal_bots_a(own_name, leader_text(state)))
    return;
  int64_t count = qa_bot_num_team_mates_bots_a(ctx, state);
  switch (gt) {
  case QA_Q3_BASE_GT_TEAM:
    if (state->src->num_teammates != count || state->src->force_orders) {
      state->src->team_give_orders_time = (float)time;
      state->src->num_teammates = (int32_t)count;
      state->src->force_orders = false;
    }
    if (state->src->team_give_orders_time != 0 &&
        state->src->team_give_orders_time < (double)(float)(time - 5)) {
      qa_bot_team_orders_bots_a(ctx, state);
      state->src->team_give_orders_time = (float)(time + 120);
    }
    break;
  case QA_Q3_BASE_GT_CTF:
  case QA_Q3_BASE_GT_1FCTF:
    if (gt == QA_Q3_BASE_GT_1FCTF && state->product != QA_Q3_BASE_PRODUCT_MISSIONPACK)
      break;
    if (state->src->num_teammates != count || state->src->flag_status_changed ||
        state->src->force_orders) {
      state->src->team_give_orders_time = (float)time;
      state->src->num_teammates = (int32_t)count;
      state->src->flag_status_changed = false;
      state->src->force_orders = false;
    }
    if (state->src->last_flag_capture_time < (double)(float)(time - 240)) {
      state->src->last_flag_capture_time = (float)time;
      if (qa_game_ai_context_random(ctx) < 0.4) {
        state->src->ctf_strategy ^= QA_CTF_STRAT_AGRESSIVE;
        state->src->team_give_orders_time = (float)time;
      }
    }
    if (state->src->team_give_orders_time != 0 &&
        state->src->team_give_orders_time <
            (double)(float)(time - (gt == QA_Q3_BASE_GT_CTF ? 3 : 2))) {
      if (gt == QA_Q3_BASE_GT_CTF)
        qa_bot_ctf_orders_bots_a(ctx, state);
      else
        qa_bot_1fctf_orders_bots_a(ctx, state);
      state->src->team_give_orders_time = 0;
    }
    break;
  case QA_Q3_BASE_GT_OBELISK:
  case QA_Q3_BASE_GT_HARVESTER:
    if (state->product != QA_Q3_BASE_PRODUCT_MISSIONPACK)
      break;
    if (state->src->num_teammates != count || state->src->force_orders) {
      state->src->team_give_orders_time = (float)time;
      state->src->num_teammates = (int32_t)count;
      state->src->force_orders = false;
    }
    if (state->src->team_give_orders_time != 0 &&
        state->src->team_give_orders_time < (double)(float)(time - 5)) {
      if (gt == QA_Q3_BASE_GT_OBELISK)
        qa_bot_obelisk_orders_bots_a(ctx, state);
      else
        qa_bot_harvester_orders_bots_a(ctx, state);
      state->src->team_give_orders_time = (float)(time + 30);
    }
    break;
  default:
    break;
  }
}
