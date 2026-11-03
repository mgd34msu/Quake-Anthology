/*
 * Port of src/bots/behavior/q3/ai-chat.ts (game/ai_chat.c, ai_main.c
 * BotAI_BotInitialChat). GPL-2.0-or-later.
 *
 * C accommodations:
 *  - TS functions returning strings write `out`, which holds at least `size`
 *    bytes, and return it.
 *  - botInitialChat's rest parameter is `variables[count]`: the first NULL ends
 *    the list as a TS null does, and only eight are read. C argument evaluation
 *    order is unspecified, so a caller whose arguments draw random numbers or
 *    print must evaluate them into locals in TS order first.
 * The context embeds this state, so the context is only forward-declared here.
 */
#ifndef QA_BOTS_BEHAVIOR_Q3_AI_CHAT_H
#define QA_BOTS_BEHAVIOR_Q3_AI_CHAT_H
#include "qa/bots/behavior/q3/ai_state.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct qa_game_ai_context_bots_a qa_game_ai_context_bots_a;
#ifndef QA_GAME_AI_MAX_CLIENTS_CELL_DEFINED
#define QA_GAME_AI_MAX_CLIENTS_CELL_DEFINED
/* One entry of the TS Map<string, number> of function-static maxclients caches;
 * entries keep the Map's insertion order. */
typedef struct qa_game_ai_max_clients_cell_bots_a {
  const char *name; /* static string */
  int64_t value;
} qa_game_ai_max_clients_cell_bots_a;
#endif
/* ai_chat.c's separate function-static maxclients cells. */
typedef struct qa_game_ai_chat_state_bots_a {
  qa_game_ai_max_clients_cell_bots_a max_clients[6];
  size_t max_client_count;
} qa_game_ai_chat_state_bots_a;
int64_t qa_bot_num_active_players_bots_a(qa_game_ai_context_bots_a *ctx);
bool qa_bot_is_first_in_rankings_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_bot_is_last_in_rankings_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
char *qa_bot_first_client_in_rankings_bots_a(qa_game_ai_context_bots_a *ctx, char *out);
char *qa_bot_last_client_in_rankings_bots_a(qa_game_ai_context_bots_a *ctx, char *out);
/* The ranking and opponent names use size 32. */
char *qa_bot_random_opponent_name_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                         char *out);
/* out holds 128 bytes. */
char *qa_bot_map_title_bots_a(qa_game_ai_context_bots_a *ctx, char *out);
const char *qa_bot_weapon_name_for_means_of_death_bots_a(qa_game_ai_context_bots_a *ctx,
                                                         int64_t method);
const char *qa_bot_random_weapon_name_bots_a(qa_game_ai_context_bots_a *ctx);
bool qa_bot_visible_enemies_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_bot_valid_chat_position_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
void qa_bot_initial_chat_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                const char *type, const char *const *variables, size_t count);
bool qa_bot_chat_enter_game_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_bot_chat_exit_game_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_bot_chat_start_level_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_bot_chat_end_level_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_bot_chat_death_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_bot_chat_kill_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_bot_chat_enemy_suicide_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_bot_chat_hit_talking_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_bot_chat_hit_no_death_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_bot_chat_hit_no_kill_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_bot_chat_random_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
double qa_bot_chat_time_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
void qa_bot_chat_test_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
#endif
