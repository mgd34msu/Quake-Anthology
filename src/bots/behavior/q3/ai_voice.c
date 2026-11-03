/* See header. Ported from id Software's game/ai_vcmd.c. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/ai_voice.h"
#include "ai_orders_internal.h"
#include "qa/bots/behavior/library/actions.h"
#include "qa/bots/behavior/library/chat.h"
#include "qa/bots/behavior/q3/ai_chat.h"
#include "qa/bots/behavior/q3/ai_combat.h"
#include "qa/bots/behavior/q3/ai_command.h"
#include "qa/bots/behavior/q3/ai_definitions.h"
#include "qa/bots/behavior/q3/ai_navigation.h"
#include "qa/bots/behavior/q3/ai_orders.h"
#include "qa/bots/behavior/q3/ai_team.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/core/game_numeric.h"
#include "qa/foundation/support.h"
#include <stdbool.h>
#include <string.h>

static void debug_print_goal(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (qa_bot_library_debug_build(ctx->library))
    qa_bot_print_team_goal_bots_a(ctx, state);
}

static void enter_chat(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, int64_t client,
                       qa_chat_destination destination) {
  qa_bot_chat_library_enter_chat(&ctx->library->chat, state->src->cs, client, destination, false, 0);
}

static void ordered(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, int64_t client,
                    int64_t type, double duration) {
  state->src->decisionmaker = (int32_t)client;
  state->src->ordered = true;
  state->src->order_time = (float)ctx->time;
  state->src->team_message_time =
      (float)(ctx->time + (float)(2 * qa_game_ai_context_random(ctx)));
  state->src->ltg_type = (int32_t)type;
  state->src->team_goal_time = (float)(ctx->time + duration);
}

QA_BOT_VOICE_FN(qa_bot_voice_chat_get_flag_bots_a) {
  (void)mode; /* TS _mode */
  const qa_game_ai_deathmatch_state_bots_a *goals = &ctx->deathmatch;
  int64_t gt = ctx->deathmatch.gametype;
  if (gt == QA_Q3_BASE_GT_CTF) {
    if (goals->ctf_red_flag.area == 0 || goals->ctf_blue_flag.area == 0)
      return;
  } else if (qa_ai_missionpack_bots_a(ctx) && gt == QA_Q3_BASE_GT_1FCTF) {
    if (goals->ctf_neutral_flag.area == 0 || goals->ctf_red_flag.area == 0 ||
        goals->ctf_blue_flag.area == 0)
      return;
  } else
    return;
  ordered(ctx, state, client, QA_LTG_GETFLAG, QA_CTF_GETFLAG_TIME);
  if (gt == QA_Q3_BASE_GT_CTF)
    qa_bot_get_alternate_route_goal_bots_a(ctx, state, qa_bot_opposite_team_bots_a(ctx, state));
  qa_bot_set_team_status_bots_a(ctx, state);
  qa_bot_remember_last_ordered_task_bots_a(ctx, state);
  debug_print_goal(ctx, state);
}

QA_BOT_VOICE_FN(qa_bot_voice_chat_offense_bots_a) {
  int64_t gt = ctx->deathmatch.gametype;
  if (gt == QA_Q3_BASE_GT_CTF || (qa_ai_missionpack_bots_a(ctx) && gt == QA_Q3_BASE_GT_1FCTF)) {
    qa_bot_voice_chat_get_flag_bots_a(ctx, state, client, mode);
    return;
  }
  if (qa_ai_missionpack_bots_a(ctx) && gt == QA_Q3_BASE_GT_HARVESTER) {
    ordered(ctx, state, client, QA_LTG_HARVEST, QA_TEAM_HARVEST_TIME);
    state->src->harvest_away_time = 0;
  } else {
    ordered(ctx, state, client, QA_LTG_ATTACKENEMYBASE, QA_TEAM_ATTACKENEMYBASE_TIME);
    state->src->attack_away_time = 0;
  }
  qa_bot_set_team_status_bots_a(ctx, state);
  qa_bot_remember_last_ordered_task_bots_a(ctx, state);
  debug_print_goal(ctx, state);
}

QA_BOT_VOICE_FN(qa_bot_voice_chat_defend_bots_a) {
  (void)mode; /* TS _mode */
  const qa_game_ai_deathmatch_state_bots_a *goals = &ctx->deathmatch;
  int64_t team = qa_bot_team_bots_a(ctx, state), gt = ctx->deathmatch.gametype;
  if (team != QA_Q3_BASE_TEAM_RED && team != QA_Q3_BASE_TEAM_BLUE)
    return;
  bool red = team == QA_Q3_BASE_TEAM_RED;
  if (qa_ai_missionpack_bots_a(ctx) &&
      (gt == QA_Q3_BASE_GT_OBELISK || gt == QA_Q3_BASE_GT_HARVESTER))
    qa_bot_goal_state_copy_from(&state->src->team_goal,
                                red ? goals->red_obelisk : goals->blue_obelisk);
  else if (gt == QA_Q3_BASE_GT_CTF || (qa_ai_missionpack_bots_a(ctx) && gt == QA_Q3_BASE_GT_1FCTF))
    qa_bot_goal_state_copy_from(&state->src->team_goal,
                                red ? goals->ctf_red_flag : goals->ctf_blue_flag);
  else
    return;
  ordered(ctx, state, client, QA_LTG_DEFENDKEYAREA, QA_TEAM_DEFENDKEYAREA_TIME);
  state->src->defend_away_time = 0;
  qa_bot_set_team_status_bots_a(ctx, state);
  qa_bot_remember_last_ordered_task_bots_a(ctx, state);
  debug_print_goal(ctx, state);
}

QA_BOT_VOICE_FN(qa_bot_voice_chat_defend_flag_bots_a) {
  qa_bot_voice_chat_defend_bots_a(ctx, state, client, mode);
}

QA_BOT_VOICE_FN(qa_bot_voice_chat_patrol_bots_a) {
  (void)mode; /* TS _mode */
  state->src->decisionmaker = (int32_t)client;
  state->src->ltg_type = QA_LTG_NONE;
  state->src->lead_time = 0;
  state->src->last_goal_ltg_type = 0;
  qa_bot_initial_chat_bots_a(ctx, state, "dismissed", NULL, 0);
  enter_chat(ctx, state, client, QA_CHAT_TELL);
  qa_bot_voice_chat_only_bots_a(ctx, state, -1, "onpatrol");
  qa_bot_set_team_status_bots_a(ctx, state);
  debug_print_goal(ctx, state);
}

static bool locate_requester(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                             int64_t client) {
  state->src->team_goal.entity = -1;
  qa_aas_entity_info_bots_a info = qa_bot_entity_info_bots_a(ctx, client);
  if (info.valid) {
    int64_t area = qa_bot_point_area_num_bots_a(ctx, info.origin);
    if (area != 0) {
      state->src->team_goal.entity = (int32_t)client;
      state->src->team_goal.area = (int32_t)area;
      state->src->team_goal.origin = qa_bot_fv3(info.origin);
      state->src->team_goal.mins = (qa_bot_fvec3){-8, -8, -8};
      state->src->team_goal.maxs = (qa_bot_fvec3){8, 8, 8};
    }
  }
  if (state->src->team_goal.entity >= 0)
    return true;
  char name[36];
  const char *variables[] = {qa_easy_client_name_bots_a(ctx, client, 36, name)};
  qa_bot_initial_chat_bots_a(ctx, state, "whereareyou", variables, 1);
  enter_chat(ctx, state, client, QA_CHAT_TELL);
  return false;
}

QA_BOT_VOICE_FN(qa_bot_voice_chat_camp_bots_a) {
  (void)mode; /* TS _mode */
  if (!locate_requester(ctx, state, client))
    return;
  ordered(ctx, state, client, QA_LTG_CAMPORDER, QA_TEAM_CAMP_TIME);
  state->src->teammate = (int32_t)client;
  state->src->arrive_time = 0;
  qa_bot_set_team_status_bots_a(ctx, state);
  qa_bot_remember_last_ordered_task_bots_a(ctx, state);
  debug_print_goal(ctx, state);
}

QA_BOT_VOICE_FN(qa_bot_voice_chat_follow_me_bots_a) {
  (void)mode; /* TS _mode */
  if (!locate_requester(ctx, state, client))
    return;
  state->src->decisionmaker = (int32_t)client;
  state->src->ordered = true;
  state->src->order_time = (float)ctx->time;
  state->src->teammate = (int32_t)client;
  state->src->teammate_visible_time = (float)ctx->time;
  state->src->team_message_time =
      (float)(ctx->time + (float)(2 * qa_game_ai_context_random(ctx)));
  state->src->team_goal_time = (float)(ctx->time + QA_TEAM_ACCOMPANY_TIME);
  state->src->ltg_type = QA_LTG_TEAMACCOMPANY;
  state->src->formation_dist = 112;
  state->src->arrive_time = 0;
  qa_bot_set_team_status_bots_a(ctx, state);
  qa_bot_remember_last_ordered_task_bots_a(ctx, state);
  debug_print_goal(ctx, state);
}

QA_BOT_VOICE_FN(qa_bot_voice_chat_follow_flag_carrier_bots_a) {
  (void)client; /* TS _client */
  int64_t carrier = qa_bot_team_flag_carrier_bots_a(ctx, state);
  if (carrier >= 0)
    qa_bot_voice_chat_follow_me_bots_a(ctx, state, carrier, mode);
  debug_print_goal(ctx, state);
}

QA_BOT_VOICE_FN(qa_bot_voice_chat_return_flag_bots_a) {
  (void)mode; /* TS _mode */
  int64_t gt = ctx->deathmatch.gametype;
  if (gt != QA_Q3_BASE_GT_CTF && !(qa_ai_missionpack_bots_a(ctx) && gt == QA_Q3_BASE_GT_1FCTF))
    return;
  ordered(ctx, state, client, QA_LTG_RETURNFLAG, QA_CTF_RETURNFLAG_TIME);
  state->src->rush_base_away_time = 0;
  qa_bot_set_team_status_bots_a(ctx, state);
  debug_print_goal(ctx, state);
}

QA_BOT_VOICE_FN(qa_bot_voice_chat_start_leader_bots_a) {
  (void)mode; /* TS _mode */
  qa_copy_client_name_to_team_leader_bots_a(ctx, state, client);
}

QA_BOT_VOICE_FN(qa_bot_voice_chat_stop_leader_bots_a) {
  (void)mode; /* TS _mode */
  char name[256];
  qa_client_name_bots_a(ctx, client, 256, name);
  if (!qa_ai_fold_equal_bots_a(qa_bot_state_text(state, state->src->team_leader, "teamLeader"), name))
    return;
  qa_bot_state_clear_team_leader(state);
  if (client < 0 || client >= QA_AI_MAX_CLIENTS)
    qa_range_error("BotVoiceChat_StopLeader: client outside notleader array");
  ctx->command.not_leader[client] = true;
}

QA_BOT_VOICE_FN(qa_bot_voice_chat_who_is_leader_bots_a) {
  (void)client; /* TS _client */
  (void)mode;   /* TS _mode */
  if (ctx->deathmatch.gametype < QA_Q3_BASE_GT_TEAM)
    return;
  char name[256];
  qa_client_name_bots_a(ctx, state->src->client, 256, name);
  if (!qa_ai_fold_equal_bots_a(name, qa_bot_state_text(state, state->src->team_leader, "teamLeader")))
    return;
  qa_bot_initial_chat_bots_a(ctx, state, "iamteamleader", NULL, 0);
  enter_chat(ctx, state, 0, QA_CHAT_TEAM);
  qa_bot_voice_chat_only_bots_a(ctx, state, -1, "startleader");
}

static void task_preference(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                            int64_t client, int64_t desired, int64_t removed) {
  int64_t preference =
      (qa_bot_get_team_mate_task_preference_bots_a(ctx, state, client) & ~removed) | desired;
  qa_bot_set_team_mate_task_preference_bots_a(ctx, state, client, preference);
  char name[36];
  const char *variables[] = {qa_easy_client_name_bots_a(ctx, client, 36, name)};
  qa_bot_initial_chat_bots_a(ctx, state, "keepinmind", variables, 1);
  enter_chat(ctx, state, client, QA_CHAT_TELL);
  qa_bot_voice_chat_only_bots_a(ctx, state, client, "yes");
  qa_bot_action_action(&ctx->library->actions, state->src->client, QA_BOT_ACTION_AFFIRMATIVE);
}

QA_BOT_VOICE_FN(qa_bot_voice_chat_want_on_defense_bots_a) {
  (void)mode; /* TS _mode */
  task_preference(ctx, state, client, QA_TASK_DEFENDER, QA_TASK_ATTACKER);
}

QA_BOT_VOICE_FN(qa_bot_voice_chat_want_on_offense_bots_a) {
  (void)mode; /* TS _mode */
  task_preference(ctx, state, client, QA_TASK_ATTACKER, QA_TASK_DEFENDER);
}

typedef QA_BOT_VOICE_FN((*voice_handler));
static const struct {
  const char *name;
  voice_handler handler;
} voice_commands[] = {
    {"getflag", qa_bot_voice_chat_get_flag_bots_a},
    {"offense", qa_bot_voice_chat_offense_bots_a},
    {"defend", qa_bot_voice_chat_defend_bots_a},
    {"defendflag", qa_bot_voice_chat_defend_flag_bots_a},
    {"patrol", qa_bot_voice_chat_patrol_bots_a},
    {"camp", qa_bot_voice_chat_camp_bots_a},
    {"followme", qa_bot_voice_chat_follow_me_bots_a},
    {"followflagcarrier", qa_bot_voice_chat_follow_flag_carrier_bots_a},
    {"returnflag", qa_bot_voice_chat_return_flag_bots_a},
    {"startleader", qa_bot_voice_chat_start_leader_bots_a},
    {"stopleader", qa_bot_voice_chat_stop_leader_bots_a},
    {"whoisleader", qa_bot_voice_chat_who_is_leader_bots_a},
    {"wantondefense", qa_bot_voice_chat_want_on_defense_bots_a},
    {"wantonoffense", qa_bot_voice_chat_want_on_offense_bots_a},
};

/* Source signed-char whitespace skip: bytes above 127 are negative, so they end tokens. */
static int signed_byte(char c) { return (int8_t)c; }

/* TS token(): writes the token, leaves the cursor after the following separators. */
static const char *next_token(const char *cursor, char *out) {
  size_t n = 0;
  while (*cursor && signed_byte(*cursor) > 32)
    out[n++] = *cursor++;
  out[n] = '\0';
  while (*cursor && signed_byte(*cursor) <= 32)
    cursor++;
  return cursor;
}

bool qa_bot_voice_chat_command_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                      int64_t mode, const char *voice_chat) {
  if (ctx->deathmatch.gametype < QA_Q3_BASE_GT_TEAM || mode == 0)
    return false;
  char text[256], token[256];
  size_t length = strlen(voice_chat);
  if (length > 255)
    length = 255;
  memcpy(text, voice_chat, length);
  text[length] = '\0';
  const char *cursor = next_token(text, token);
  cursor = next_token(cursor, token);
  int32_t client = qa_game_atoi(token);
  cursor = next_token(cursor, token);
  if (!qa_bot_same_team_bots_a(ctx, state, client))
    return false;
  for (size_t i = 0; i < sizeof voice_commands / sizeof voice_commands[0]; i++) {
    if (qa_ai_fold_equal_bots_a(voice_commands[i].name, cursor)) {
      voice_commands[i].handler(ctx, state, client, mode);
      return true;
    }
  }
  return false;
}
