/* See header. Ported from id Software's game/ai_chat.c and
 * ai_main.c:BotAI_BotInitialChat. GPL-2.0-or-later.
 *
 * C leaves argument evaluation order unspecified. Where TS evaluates botInitialChat
 * arguments that draw random numbers or print, they are staged into locals in TS order. */
#include "qa/bots/behavior/q3/ai_chat.h"
#include "ai_orders_internal.h"
#include "qa/bots/behavior/library/actions.h"
#include "qa/bots/behavior/library/character.h"
#include "qa/bots/behavior/library/chat.h"
#include "qa/bots/behavior/q3/ai_combat.h"
#include "qa/bots/behavior/q3/ai_definitions.h"
#include "qa/bots/behavior/q3/ai_orders.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/core/cvars/index.h"
#include "qa/core/info_string.h"
#include "qa/core/game_numeric.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TIME_BETWEEN_CHATTING 25
#define INVALID_VARIABLE "[invalid var]"
#define GT_TEAM QA_Q3_BASE_GT_TEAM
#define GT_TOURNAMENT QA_Q3_BASE_GT_TOURNAMENT

static int64_t cached_max_clients(qa_game_ai_context_bots_a *ctx, const char *name) {
  return qa_ai_cached_max_clients_bots_a(ctx, ctx->chat.max_clients, &ctx->chat.max_client_count,
                                         sizeof ctx->chat.max_clients / sizeof ctx->chat.max_clients[0],
                                         name);
}

static bool active_player(qa_game_ai_context_bots_a *ctx, int64_t client) {
  char config[QA_AI_INFO_SIZE], name[QA_AI_INFO_SIZE], team[QA_AI_INFO_SIZE];
  qa_ai_configstring_bots_a(ctx, QA_AI_CS_PLAYERS + client, config);
  return config[0] != '\0' && qa_ai_info_value_bots_a(config, "n", name)[0] != '\0' &&
         qa_game_atoi(qa_ai_info_value_bots_a(config, "t", team)) != QA_Q3_BASE_TEAM_SPECTATOR;
}

int64_t qa_bot_num_active_players_bots_a(qa_game_ai_context_bots_a *ctx) {
  int64_t maximum = cached_max_clients(ctx, "BotNumActivePlayers");
  int64_t count = 0;
  for (int64_t client = 0; client < maximum; client++)
    if (active_player(ctx, client))
      count++;
  return count;
}

static bool in_rankings(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, bool first,
                        const char *name, const char *error) {
  int64_t maximum = cached_max_clients(ctx, name);
  double score = state->src->cur_ps.persistant[QA_Q3_BASE_PERS_SCORE];
  for (int64_t client = 0; client < maximum; client++) {
    if (!active_player(ctx, client))
      continue;
    double other;
    if (!qa_ai_client_score_bots_a(ctx, client, &other))
      qa_range_error("%s", error);
    if (first ? score < other : score > other)
      return false;
  }
  return true;
}

bool qa_bot_is_first_in_rankings_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  return in_rankings(ctx, state, true, "BotIsFirstInRankings",
                     "BotIsFirstInRankings: active player has no client state");
}

bool qa_bot_is_last_in_rankings_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  return in_rankings(ctx, state, false, "BotIsLastInRankings",
                     "BotIsLastInRankings: active player has no client state");
}

static char *ranked_client(qa_game_ai_context_bots_a *ctx, bool first, char *out) {
  int64_t maximum =
      cached_max_clients(ctx, first ? "BotFirstClientInRankings" : "BotLastClientInRankings");
  double score = first ? -999999 : 999999;
  int64_t selected = 0;
  for (int64_t client = 0; client < maximum; client++) {
    if (!active_player(ctx, client))
      continue;
    double current;
    if (!qa_ai_client_score_bots_a(ctx, client, &current))
      qa_range_error("BotClientInRankings: active player has no client state");
    if (first ? current > score : current < score) {
      score = current;
      selected = client;
    }
  }
  return qa_easy_client_name_bots_a(ctx, selected, 32, out);
}

char *qa_bot_first_client_in_rankings_bots_a(qa_game_ai_context_bots_a *ctx, char *out) {
  return ranked_client(ctx, true, out);
}
char *qa_bot_last_client_in_rankings_bots_a(qa_game_ai_context_bots_a *ctx, char *out) {
  return ranked_client(ctx, false, out);
}

char *qa_bot_random_opponent_name_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                         char *out) {
  int64_t maximum = cached_max_clients(ctx, "BotRandomOpponentName");
  int64_t opponents[QA_AI_MAX_CLIENTS];
  int64_t total = 0;
  for (int64_t client = 0; client < maximum; client++) {
    if (client != state->src->client && active_player(ctx, client) &&
        !qa_bot_same_team_bots_a(ctx, state, client))
      opponents[total++] = client;
  }
  int64_t count = (int64_t)trunc((float)(qa_game_ai_context_random(ctx) * (double)total));
  for (int64_t i = 0; i < total; i++)
    if (--count <= 0)
      return qa_easy_client_name_bots_a(ctx, opponents[i], 32, out);
  /* The source initializes opponents[0] to zero even when there are no opponents. */
  return qa_easy_client_name_bots_a(ctx, total == 0 ? 0 : opponents[0], 32, out);
}

char *qa_bot_map_title_bots_a(qa_game_ai_context_bots_a *ctx, char *out) {
  char *info = qa_cvar_registry_info_string(ctx->game->options.cvars, QA_CVAR_FLAG_SERVER_INFO, -1);
  if (info == NULL)
    qa_range_error("Bot serverinfo allocation failed");
  char *value = qa_info_value_for_key(info, "mapname");
  free(info);
  if (value == NULL)
    qa_range_error("Bot map title allocation failed");
  snprintf(out, 128, "%s", value);
  free(value);
  return out;
}

const char *qa_bot_weapon_name_for_means_of_death_bots_a(qa_game_ai_context_bots_a *ctx,
                                                         int64_t method) {
  bool missionpack = qa_ai_missionpack_bots_a(ctx);
  switch (method) {
  case 1:
    return "Shotgun";
  case 2:
    return "Gauntlet";
  case 3:
    return "Machinegun";
  case 4:
  case 5:
    return "Grenade Launcher";
  case 6:
  case 7:
    return "Rocket Launcher";
  case 8:
  case 9:
    return "Plasmagun";
  case 10:
    return "Railgun";
  case 11:
    return "Lightning Gun";
  case 12:
  case 13:
    return "BFG10K";
  case 23:
    return missionpack ? "Nailgun" : "Grapple";
  case 24:
    if (missionpack)
      return "Chaingun";
    break;
  case 25:
    if (missionpack)
      return "Proximity Launcher";
    break;
  case 26:
    if (missionpack)
      return "Kamikaze";
    break;
  case 27:
    if (missionpack)
      return "Prox mine";
    break;
  case 28:
    if (missionpack)
      return "Grapple";
    break;
  default:
    break;
  }
  return "[unknown weapon]";
}

const char *qa_bot_random_weapon_name_bots_a(qa_game_ai_context_bots_a *ctx) {
  bool missionpack = qa_ai_missionpack_bots_a(ctx);
  int64_t selected = (int64_t)trunc(
      (float)(qa_game_ai_context_random(ctx) * (double)(missionpack ? (float)11.9 : (float)8.9)));
  switch (selected) {
  case 0:
    return "Gauntlet";
  case 1:
    return "Shotgun";
  case 2:
    return "Machinegun";
  case 3:
    return "Grenade Launcher";
  case 4:
    return "Rocket Launcher";
  case 5:
    return "Plasmagun";
  case 6:
    return "Railgun";
  case 7:
    return "Lightning Gun";
  case 8:
    if (missionpack)
      return "Nailgun";
    break;
  case 9:
    if (missionpack)
      return "Chaingun";
    break;
  case 10:
    if (missionpack)
      return "Proximity Launcher";
    break;
  default:
    break;
  }
  return "BFG10K";
}

bool qa_bot_visible_enemies_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  for (int64_t client = 0; client < QA_AI_MAX_CLIENTS; client++) {
    if (client == state->src->client)
      continue;
    qa_aas_entity_info_bots_a info = qa_bot_entity_info_bots_a(ctx, client);
    if (!info.valid || qa_entity_is_dead_bots_a(ctx, &info) || info.number == state->src->entity_num)
      continue;
    if (qa_entity_is_invisible_bots_a(ctx, &info) && !qa_entity_is_shooting_bots_a(&info))
      continue;
    if (qa_bot_same_team_bots_a(ctx, state, client))
      continue;
    if (qa_bot_entity_visible_bots_a(ctx, state->src->entity_num, qa_bot_v3(state->src->eye),
                                     qa_bot_v3(state->src->viewangles), 360, client) > 0)
      return true;
  }
  return false;
}

bool qa_bot_valid_chat_position_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (qa_bot_is_dead_bots_a(ctx, state))
    return true;
  static const int slots[] = {QA_INV_QUAD, QA_INV_HASTE, QA_INV_INVISIBILITY, QA_INV_REGEN,
                              QA_INV_FLIGHT};
  for (size_t i = 0; i < sizeof slots / sizeof slots[0]; i++)
    if (state->src->inventory[slots[i]] != 0)
      return false;
  qa_vec3 origin = qa_bot_v3(state->src->origin);
  qa_source_bot_game_bots_a *game = ctx->game;
  qa_vec3 below = {origin.x, origin.y, (float)(origin.z - 24)};
  if ((game->point_contents(game->ctx, below, state->src->entity_num) & (8 | 16)) != 0)
    return false;
  qa_vec3 above = {origin.x, origin.y, (float)(origin.z + 32)};
  if ((game->point_contents(game->ctx, above, state->src->entity_num) & (8 | 16 | 32)) != 0)
    return false;
  qa_bot_navigation_bots_a *navigation = qa_game_ai_context_navigation(ctx);
  qa_bounds bounds = navigation->presence_bounds(navigation->ctx, 4);
  qa_q3_base_server_trace_query query;
  memset(&query, 0, sizeof query);
  query.start = (qa_vec3){origin.x, origin.y, (float)(origin.z + 1)};
  query.end = (qa_vec3){origin.x, origin.y, (float)(origin.z - 10)};
  query.shape.kind = QA_Q3_BASE_SHAPE_BOX;
  query.shape.mins = bounds.min;
  query.shape.maxs = bounds.max;
  query.pass_entity_num = (double)state->src->client;
  query.mask = 1;
  qa_q3_base_server_trace_result trace = game->trace(game->ctx, query);
  return trace.entity_num == QA_Q3_BASE_ENTITYNUM_WORLD;
}

void qa_bot_initial_chat_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                const char *type, const char *const *variables, size_t count) {
  qa_chat_text_source values[8];
  memset(values, 0, sizeof values);
  for (size_t index = 0; index < 8 && index < count; index++) {
    if (variables[index] == NULL)
      break;
    values[index] = qa_chat_text(variables[index]);
  }
  qa_bot_chat_library_initial_chat(&ctx->library->chat, state->src->cs, qa_chat_text(type),
                                   qa_bot_synonym_context_bots_a(ctx, state), values);
}

static bool chat_too_recent(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  return state->src->last_chat_time > (double)(float)(ctx->time - TIME_BETWEEN_CHATTING);
}
static bool no_chat(qa_game_ai_context_bots_a *ctx) {
  return qa_game_ai_context_cvar(ctx, "bot_nochat")->integer_value != 0;
}
static bool unavailable(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  return no_chat(ctx) || chat_too_recent(ctx, state);
}
static double characteristic(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                             int64_t index) {
  return qa_bot_character_bounded_float(&ctx->library->characters, state->src->character, index, 0,
                                        1);
}
static bool random_refused(qa_game_ai_context_bots_a *ctx, double probability) {
  return qa_game_ai_context_cvar(ctx, "bot_fastchat")->integer_value == 0 &&
         qa_game_ai_context_random(ctx) > probability;
}
static bool all_chat(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  state->src->last_chat_time = (float)ctx->time;
  state->src->chat_to = QA_CHAT_ALL;
  return true;
}
static void command_vtaunt(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  qa_bot_action_command(&ctx->library->actions, state->src->client, "vtaunt");
}
static bool team_game(qa_game_ai_context_bots_a *ctx) { return ctx->deathmatch.gametype >= GT_TEAM; }
static bool tournament(qa_game_ai_context_bots_a *ctx) {
  return ctx->deathmatch.gametype == GT_TOURNAMENT;
}

/* The shared game_enter/game_exit variables, in TS evaluation order. */
static void enter_exit_chat(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                            const char *type) {
  char self[32], opponent[32], title[128];
  qa_easy_client_name_bots_a(ctx, state->src->client, 32, self);
  qa_bot_random_opponent_name_bots_a(ctx, state, opponent);
  qa_bot_map_title_bots_a(ctx, title);
  const char *variables[] = {self, opponent, INVALID_VARIABLE, INVALID_VARIABLE, title};
  qa_bot_initial_chat_bots_a(ctx, state, type, variables, 5);
}

bool qa_bot_chat_enter_game_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (unavailable(ctx, state) || team_game(ctx) || tournament(ctx))
    return false;
  if (random_refused(ctx, characteristic(ctx, state, QA_BCHAR_CHAT_ENTEREXITGAME)))
    return false;
  if (qa_bot_num_active_players_bots_a(ctx) <= 1 || !qa_bot_valid_chat_position_bots_a(ctx, state))
    return false;
  enter_exit_chat(ctx, state, "game_enter");
  return all_chat(ctx, state);
}

bool qa_bot_chat_exit_game_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (unavailable(ctx, state) || team_game(ctx) || tournament(ctx))
    return false;
  if (random_refused(ctx, characteristic(ctx, state, QA_BCHAR_CHAT_ENTEREXITGAME)))
    return false;
  if (qa_bot_num_active_players_bots_a(ctx) <= 1)
    return false;
  enter_exit_chat(ctx, state, "game_exit");
  return all_chat(ctx, state);
}

bool qa_bot_chat_start_level_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (no_chat(ctx) || qa_bot_is_observer_bots_a(ctx, state) || chat_too_recent(ctx, state))
    return false;
  if (team_game(ctx)) {
    command_vtaunt(ctx, state);
    return false;
  }
  if (tournament(ctx))
    return false;
  if (random_refused(ctx, characteristic(ctx, state, QA_BCHAR_CHAT_STARTENDLEVEL)) ||
      qa_bot_num_active_players_bots_a(ctx) <= 1)
    return false;
  char self[32];
  const char *variables[] = {qa_easy_client_name_bots_a(ctx, state->src->client, 32, self)};
  qa_bot_initial_chat_bots_a(ctx, state, "level_start", variables, 1);
  return all_chat(ctx, state);
}

bool qa_bot_chat_end_level_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (no_chat(ctx) || qa_bot_is_observer_bots_a(ctx, state) || chat_too_recent(ctx, state))
    return false;
  if (team_game(ctx)) {
    if (qa_bot_is_first_in_rankings_bots_a(ctx, state))
      command_vtaunt(ctx, state);
    return true;
  }
  if (tournament(ctx))
    return false;
  if (random_refused(ctx, characteristic(ctx, state, QA_BCHAR_CHAT_STARTENDLEVEL)) ||
      qa_bot_num_active_players_bots_a(ctx) <= 1)
    return false;
  bool first = qa_bot_is_first_in_rankings_bots_a(ctx, state);
  bool last = !first && qa_bot_is_last_in_rankings_bots_a(ctx, state);
  char self[32], opponent[32], top[32], bottom[32], title[128];
  qa_easy_client_name_bots_a(ctx, state->src->client, 32, self);
  qa_bot_random_opponent_name_bots_a(ctx, state, opponent);
  if (!first)
    qa_bot_first_client_in_rankings_bots_a(ctx, top);
  if (!last)
    qa_bot_last_client_in_rankings_bots_a(ctx, bottom);
  qa_bot_map_title_bots_a(ctx, title);
  const char *variables[] = {self, opponent, first ? INVALID_VARIABLE : top,
                             last ? INVALID_VARIABLE : bottom, title};
  qa_bot_initial_chat_bots_a(ctx, state,
                             first  ? "level_end_victory"
                             : last ? "level_end_lose"
                                    : "level_end",
                             variables, 5);
  return all_chat(ctx, state);
}

static void chat_random_opponent(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                 const char *type) {
  char opponent[32];
  const char *variables[] = {qa_bot_random_opponent_name_bots_a(ctx, state, opponent)};
  qa_bot_initial_chat_bots_a(ctx, state, type, variables, 1);
}

bool qa_bot_chat_death_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (unavailable(ctx, state))
    return false;
  double chance = characteristic(ctx, state, QA_BCHAR_CHAT_DEATH);
  if (tournament(ctx) || random_refused(ctx, chance) || qa_bot_num_active_players_bots_a(ctx) <= 1)
    return false;
  int64_t killer = state->src->last_killed_by;
  char name[32] = "[world]";
  if (killer >= 0 && killer < QA_AI_MAX_CLIENTS)
    qa_easy_client_name_bots_a(ctx, killer, 32, name);
  const char *name_variable[] = {name};
  if (team_game(ctx) && qa_bot_same_team_bots_a(ctx, state, killer)) {
    if (killer == state->src->client)
      return false;
    qa_bot_initial_chat_bots_a(ctx, state, "death_teammate", name_variable, 1);
    state->src->chat_to = QA_CHAT_TEAM;
  } else {
    if (team_game(ctx)) {
      command_vtaunt(ctx, state);
      return true;
    }
    int64_t method = state->src->bot_death_type;
    if (method == 14)
      chat_random_opponent(ctx, state, "death_drown");
    else if (method == 15)
      chat_random_opponent(ctx, state, "death_slime");
    else if (method == 16)
      chat_random_opponent(ctx, state, "death_lava");
    else if (method == 19)
      chat_random_opponent(ctx, state, "death_cratered");
    else if (state->src->bot_suicide || method == 17 || method == 20 || method == 21 ||
             method == 22 || method == 0)
      chat_random_opponent(ctx, state, "death_suicide");
    else if (method == 18)
      qa_bot_initial_chat_bots_a(ctx, state, "death_telefrag", name_variable, 1);
    else if (qa_ai_missionpack_bots_a(ctx) && method == 26 &&
             qa_bot_chat_library_num_initial_chats(&ctx->library->chat, state->src->cs,
                                                   qa_chat_text("death_kamikaze")) != 0)
      qa_bot_initial_chat_bots_a(ctx, state, "death_kamikaze", name_variable, 1);
    else {
      const char *type;
      if ((method == 2 || method == 10 || method == 12 || method == 13) &&
          qa_game_ai_context_random(ctx) < 0.5)
        type = method == 2 ? "death_gauntlet" : method == 10 ? "death_rail" : "death_bfg";
      else {
        double draw = qa_game_ai_context_random(ctx);
        type = draw < characteristic(ctx, state, QA_BCHAR_CHAT_INSULT) ? "death_insult"
                                                                       : "death_praise";
      }
      const char *variables[] = {name, qa_bot_weapon_name_for_means_of_death_bots_a(ctx, method)};
      qa_bot_initial_chat_bots_a(ctx, state, type, variables, 2);
    }
    state->src->chat_to = QA_CHAT_ALL;
  }
  state->src->last_chat_time = (float)ctx->time;
  return true;
}

bool qa_bot_chat_kill_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (unavailable(ctx, state))
    return false;
  double chance = characteristic(ctx, state, QA_BCHAR_CHAT_KILL);
  if (tournament(ctx) || random_refused(ctx, chance) ||
      state->src->last_killed_player == state->src->client)
    return false;
  if (qa_bot_num_active_players_bots_a(ctx) <= 1 || !qa_bot_valid_chat_position_bots_a(ctx, state) ||
      qa_bot_visible_enemies_bots_a(ctx, state))
    return false;
  char name[32];
  const char *variables[] = {
      qa_easy_client_name_bots_a(ctx, state->src->last_killed_player, 32, name)};
  state->src->chat_to = QA_CHAT_ALL;
  if (team_game(ctx) && qa_bot_same_team_bots_a(ctx, state, state->src->last_killed_player)) {
    qa_bot_initial_chat_bots_a(ctx, state, "kill_teammate", variables, 1);
    state->src->chat_to = QA_CHAT_TEAM;
  } else {
    if (team_game(ctx)) {
      command_vtaunt(ctx, state);
      return false;
    }
    int64_t method = state->src->enemy_death_type;
    const char *type;
    if (method == 2)
      type = "kill_gauntlet";
    else if (method == 10)
      type = "kill_rail";
    else if (method == 18)
      type = "kill_telefrag";
    /* The source tests botdeathtype here rather than enemydeathtype. */
    else if (qa_ai_missionpack_bots_a(ctx) && state->src->bot_death_type == 26 &&
             qa_bot_chat_library_num_initial_chats(&ctx->library->chat, state->src->cs,
                                                   qa_chat_text("kill_kamikaze")) != 0)
      type = "kill_kamikaze";
    else {
      double draw = qa_game_ai_context_random(ctx);
      type = draw < characteristic(ctx, state, QA_BCHAR_CHAT_INSULT) ? "kill_insult" : "kill_praise";
    }
    qa_bot_initial_chat_bots_a(ctx, state, type, variables, 1);
  }
  state->src->last_chat_time = (float)ctx->time;
  return true;
}

bool qa_bot_chat_enemy_suicide_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (unavailable(ctx, state) || qa_bot_num_active_players_bots_a(ctx) <= 1)
    return false;
  double chance = characteristic(ctx, state, QA_BCHAR_CHAT_KILL);
  if (team_game(ctx) || tournament(ctx) || random_refused(ctx, chance))
    return false;
  if (!qa_bot_valid_chat_position_bots_a(ctx, state) || qa_bot_visible_enemies_bots_a(ctx, state))
    return false;
  char name[32] = "";
  if (state->src->enemy >= 0)
    qa_easy_client_name_bots_a(ctx, state->src->enemy, 32, name);
  const char *variables[] = {name};
  qa_bot_initial_chat_bots_a(ctx, state, "enemy_suicide", variables, 1);
  return all_chat(ctx, state);
}

static qa_bot_observed_entity_bots_a game_entity(qa_game_ai_context_bots_a *ctx, int64_t number) {
  return ctx->game->entity(ctx->game->ctx, number);
}

bool qa_bot_chat_hit_talking_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (unavailable(ctx, state) || qa_bot_num_active_players_bots_a(ctx) <= 1)
    return false;
  qa_bot_observed_entity_bots_a client = game_entity(ctx, state->src->client);
  if (!client.has_player)
    qa_range_error("BotChat_HitTalking requires a client");
  int64_t attacker = client.last_hurt_client;
  if (attacker == 0 || attacker == state->src->client || attacker < 0 ||
      attacker >= QA_AI_MAX_CLIENTS)
    return false;
  double chance = characteristic(ctx, state, QA_BCHAR_CHAT_HITTALKING);
  if (team_game(ctx) || tournament(ctx) || random_refused(ctx, chance * 0.5))
    return false;
  if (!qa_bot_valid_chat_position_bots_a(ctx, state))
    return false;
  /* Source passes lasthurt_client, not lasthurt_mod, in this one branch. */
  char name[32];
  qa_client_name_bots_a(ctx, attacker, 32, name);
  const char *variables[] = {name, qa_bot_weapon_name_for_means_of_death_bots_a(ctx, attacker)};
  qa_bot_initial_chat_bots_a(ctx, state, "hit_talking", variables, 2);
  return all_chat(ctx, state);
}

bool qa_bot_chat_hit_no_death_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  qa_bot_observed_entity_bots_a client = game_entity(ctx, state->src->client);
  if (!client.has_player)
    qa_range_error("BotChat_HitNoDeath requires a client");
  int64_t attacker = client.last_hurt_client, hurt_mod = client.last_hurt_mod;
  if (attacker == 0 || attacker == state->src->client || attacker < 0 ||
      attacker >= QA_AI_MAX_CLIENTS)
    return false;
  if (unavailable(ctx, state) || qa_bot_num_active_players_bots_a(ctx) <= 1)
    return false;
  double chance = characteristic(ctx, state, QA_BCHAR_CHAT_HITNODEATH);
  if (team_game(ctx) || tournament(ctx) || random_refused(ctx, chance * 0.5))
    return false;
  if (!qa_bot_valid_chat_position_bots_a(ctx, state) || qa_bot_visible_enemies_bots_a(ctx, state))
    return false;
  qa_aas_entity_info_bots_a enemy = qa_bot_entity_info_bots_a(ctx, state->src->enemy);
  if (qa_entity_is_shooting_bots_a(&enemy))
    return false;
  char name[32];
  qa_client_name_bots_a(ctx, attacker, 32, name);
  const char *variables[] = {name, qa_bot_weapon_name_for_means_of_death_bots_a(ctx, hurt_mod)};
  qa_bot_initial_chat_bots_a(ctx, state, "hit_nodeath", variables, 2);
  return all_chat(ctx, state);
}

bool qa_bot_chat_hit_no_kill_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (unavailable(ctx, state) || qa_bot_num_active_players_bots_a(ctx) <= 1)
    return false;
  double chance = characteristic(ctx, state, QA_BCHAR_CHAT_HITNOKILL);
  if (team_game(ctx) || tournament(ctx) || random_refused(ctx, chance * 0.5))
    return false;
  if (!qa_bot_valid_chat_position_bots_a(ctx, state) || qa_bot_visible_enemies_bots_a(ctx, state))
    return false;
  qa_aas_entity_info_bots_a info = qa_bot_entity_info_bots_a(ctx, state->src->enemy);
  if (qa_entity_is_shooting_bots_a(&info))
    return false;
  qa_bot_observed_entity_bots_a enemy = game_entity(ctx, state->src->enemy);
  if (!enemy.has_player)
    qa_range_error("BotChat_HitNoKill requires an enemy client");
  int64_t hurt_mod = enemy.last_hurt_mod;
  char name[32];
  qa_client_name_bots_a(ctx, state->src->enemy, 32, name);
  const char *variables[] = {name, qa_bot_weapon_name_for_means_of_death_bots_a(ctx, hurt_mod)};
  qa_bot_initial_chat_bots_a(ctx, state, "hit_nokill", variables, 2);
  return all_chat(ctx, state);
}

bool qa_bot_chat_random_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (no_chat(ctx) || qa_bot_is_observer_bots_a(ctx, state) || chat_too_recent(ctx, state))
    return false;
  int64_t ltg = state->src->ltg_type;
  if (tournament(ctx) || ltg == QA_LTG_TEAMHELP || ltg == QA_LTG_TEAMACCOMPANY ||
      ltg == QA_LTG_RUSHBASE)
    return false;
  double chance = characteristic(ctx, state, QA_BCHAR_CHAT_RANDOM);
  if (qa_game_ai_context_random(ctx) > (double)(float)(state->src->think_time * (double)(float)0.1))
    return false;
  if (qa_game_ai_context_cvar(ctx, "bot_fastchat")->integer_value == 0 &&
      (qa_game_ai_context_random(ctx) > chance || qa_game_ai_context_random(ctx) > 0.25))
    return false;
  if (qa_bot_num_active_players_bots_a(ctx) <= 1 || !qa_bot_valid_chat_position_bots_a(ctx, state) ||
      qa_bot_visible_enemies_bots_a(ctx, state))
    return false;
  char name[32], opponent[32], title[128];
  if (state->src->last_killed_player == state->src->client)
    qa_bot_random_opponent_name_bots_a(ctx, state, name);
  else
    qa_easy_client_name_bots_a(ctx, state->src->last_killed_player, 32, name);
  if (team_game(ctx)) {
    command_vtaunt(ctx, state);
    return false;
  }
  double draw = qa_game_ai_context_random(ctx);
  const char *type =
      draw < characteristic(ctx, state, QA_BCHAR_CHAT_MISC) ? "random_misc" : "random_insult";
  qa_bot_random_opponent_name_bots_a(ctx, state, opponent);
  qa_bot_map_title_bots_a(ctx, title);
  const char *weapon = qa_bot_random_weapon_name_bots_a(ctx);
  const char *variables[] = {opponent, name, INVALID_VARIABLE, INVALID_VARIABLE, title, weapon};
  qa_bot_initial_chat_bots_a(ctx, state, type, variables, 6);
  return all_chat(ctx, state);
}

double qa_bot_chat_time_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  qa_bot_character_bounded_integer(&ctx->library->characters, state->src->character,
                                   QA_BCHAR_CHAT_CPM, 1, 4000);
  return 2;
}

typedef enum chat_test_variables {
  TEST_GAME,      /* self, opponent, invalid, invalid, title */
  TEST_LEVEL_END, /* self, opponent, first, last, title */
  TEST_SELF,      /* self */
  TEST_FIXED,     /* the fixed name and optional weapon */
  TEST_RANDOM     /* opponent, name, invalid, invalid, title, weapon */
} chat_test_variables;

/* TS emit(): the variables thunk runs for each initial chat, drawing fresh values. */
static void emit(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, const char *type,
                 chat_test_variables kind, const char *name, const char *weapon) {
  int64_t count =
      qa_bot_chat_library_num_initial_chats(&ctx->library->chat, state->src->cs, qa_chat_text(type));
  for (int64_t index = 0; index < count; index++) {
    char self[32], opponent[32], top[32], bottom[32], title[128];
    const char *variables[8] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
    switch (kind) {
    case TEST_GAME:
      variables[0] = qa_easy_client_name_bots_a(ctx, state->src->client, 32, self);
      variables[1] = qa_bot_random_opponent_name_bots_a(ctx, state, opponent);
      variables[2] = INVALID_VARIABLE;
      variables[3] = INVALID_VARIABLE;
      variables[4] = qa_bot_map_title_bots_a(ctx, title);
      break;
    case TEST_LEVEL_END:
      variables[0] = qa_easy_client_name_bots_a(ctx, state->src->client, 32, self);
      variables[1] = qa_bot_random_opponent_name_bots_a(ctx, state, opponent);
      variables[2] = qa_bot_first_client_in_rankings_bots_a(ctx, top);
      variables[3] = qa_bot_last_client_in_rankings_bots_a(ctx, bottom);
      variables[4] = qa_bot_map_title_bots_a(ctx, title);
      break;
    case TEST_SELF:
      variables[0] = qa_easy_client_name_bots_a(ctx, state->src->client, 32, self);
      break;
    case TEST_FIXED:
      variables[0] = name;
      variables[1] = weapon;
      break;
    case TEST_RANDOM:
      variables[0] = qa_bot_random_opponent_name_bots_a(ctx, state, opponent);
      variables[1] = name;
      variables[2] = INVALID_VARIABLE;
      variables[3] = INVALID_VARIABLE;
      variables[4] = qa_bot_map_title_bots_a(ctx, title);
      variables[5] = qa_bot_random_weapon_name_bots_a(ctx);
      break;
    }
    qa_bot_initial_chat_bots_a(ctx, state, type, variables, 8);
    qa_bot_chat_library_enter_chat(&ctx->library->chat, state->src->cs, 0, QA_CHAT_ALL, false, 0);
  }
}

void qa_bot_chat_test_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  emit(ctx, state, "game_enter", TEST_GAME, NULL, NULL);
  emit(ctx, state, "game_exit", TEST_GAME, NULL, NULL);
  emit(ctx, state, "level_start", TEST_SELF, NULL, NULL);
  emit(ctx, state, "level_end_victory", TEST_LEVEL_END, NULL, NULL);
  emit(ctx, state, "level_end_lose", TEST_LEVEL_END, NULL, NULL);
  emit(ctx, state, "level_end", TEST_LEVEL_END, NULL, NULL);
  char name[32];
  qa_easy_client_name_bots_a(ctx, state->src->last_killed_by, 32, name);
  static const char *const death_plain[] = {"death_drown",    "death_slime",   "death_lava",
                                            "death_cratered", "death_suicide", "death_telefrag"};
  for (size_t i = 0; i < sizeof death_plain / sizeof death_plain[0]; i++)
    emit(ctx, state, death_plain[i], TEST_FIXED, name, NULL);
  static const char *const death_weapon[] = {"death_gauntlet", "death_rail", "death_bfg",
                                             "death_insult", "death_praise"};
  for (size_t i = 0; i < sizeof death_weapon / sizeof death_weapon[0]; i++)
    emit(ctx, state, death_weapon[i], TEST_FIXED, name,
         qa_bot_weapon_name_for_means_of_death_bots_a(ctx, state->src->bot_death_type));
  qa_easy_client_name_bots_a(ctx, state->src->last_killed_player, 32, name);
  static const char *const kills[] = {"kill_gauntlet", "kill_rail",   "kill_telefrag",
                                      "kill_insult",   "kill_praise", "enemy_suicide"};
  for (size_t i = 0; i < sizeof kills / sizeof kills[0]; i++)
    emit(ctx, state, kills[i], TEST_FIXED, name, NULL);
  qa_bot_observed_entity_bots_a client = game_entity(ctx, state->src->client);
  if (!client.has_player)
    qa_range_error("BotChatTest requires a client");
  int64_t hurt_client = client.last_hurt_client;
  char hurt_name[32];
  qa_client_name_bots_a(ctx, hurt_client, 32, hurt_name);
  const char *weapon = qa_bot_weapon_name_for_means_of_death_bots_a(ctx, hurt_client);
  static const char *const hits[] = {"hit_talking", "hit_nodeath", "hit_nokill"};
  for (size_t i = 0; i < sizeof hits / sizeof hits[0]; i++)
    emit(ctx, state, hits[i], TEST_FIXED, hurt_name, weapon);
  if (state->src->last_killed_player == state->src->client)
    qa_bot_random_opponent_name_bots_a(ctx, state, name);
  else
    qa_easy_client_name_bots_a(ctx, state->src->last_killed_player, 32, name);
  emit(ctx, state, "random_misc", TEST_RANDOM, name, NULL);
  emit(ctx, state, "random_insult", TEST_RANDOM, name, NULL);
}
