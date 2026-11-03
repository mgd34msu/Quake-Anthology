/*
 * Port of src/bots/behavior/q3/ai-orders.ts (game/ai_dmq3.c team, identity and
 * objective AI). GPL-2.0-or-later.
 *
 * C accommodations:
 *  - TS functions returning strings write `out` and return it. `out` holds at
 *    least `size` bytes, and at least 22 bytes for the "[client out of range]"
 *    result that TS returns whatever the size.
 *  - botVisibleTeamMatesAndEnemies' result object is two out parameters.
 *  - botEnemyFlag/botTeamFlag return the live deathmatch goal cell, as TS does.
 */
#ifndef QA_BOTS_BEHAVIOR_Q3_AI_ORDERS_H
#define QA_BOTS_BEHAVIOR_Q3_AI_ORDERS_H
#include "qa/bots/behavior/q3/ai_state.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct qa_game_ai_context_bots_a qa_game_ai_context_bots_a;
/* out_len 0 writes nothing; the output stops at out_len-1 bytes. strlen(text)+1 always fits. */
void qa_bot_clean_string_bots_a(const char *text, char *out, size_t out_len);
char *qa_client_name_bots_a(qa_game_ai_context_bots_a *ctx, int64_t client, int64_t size, char *out);
void qa_copy_client_name_to_team_leader_bots_a(qa_game_ai_context_bots_a *ctx,
                                               qa_bot_state_bots_a *state, int64_t client);
char *qa_client_skin_bots_a(qa_game_ai_context_bots_a *ctx, int64_t client, int64_t size, char *out);
int64_t qa_client_from_name_bots_a(qa_game_ai_context_bots_a *ctx, const char *name);
int64_t qa_client_on_same_team_from_name_bots_a(qa_game_ai_context_bots_a *ctx,
                                                qa_bot_state_bots_a *state, const char *name);
const char *qa_stristr_bots_a(const char *text, const char *part);
char *qa_easy_client_name_bots_a(qa_game_ai_context_bots_a *ctx, int64_t client, int64_t size,
                                 char *out);
void qa_bot_set_user_info_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                 const char *key, const char *value);
bool qa_team_play_is_on_bots_a(qa_game_ai_context_bots_a *ctx);
int64_t qa_bot_team_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
int64_t qa_bot_opposite_team_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_bot_same_team_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                             int64_t entity);
const qa_bot_goal *qa_bot_enemy_flag_bots_a(qa_game_ai_context_bots_a *ctx,
                                            qa_bot_state_bots_a *state);
const qa_bot_goal *qa_bot_team_flag_bots_a(qa_game_ai_context_bots_a *ctx,
                                           qa_bot_state_bots_a *state);
int64_t qa_bot_ctf_carrying_flag_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_bot_1fctf_carrying_flag_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_bot_harvester_carrying_cubes_bots_a(qa_game_ai_context_bots_a *ctx,
                                            qa_bot_state_bots_a *state);
int64_t qa_bot_synonym_context_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
int64_t qa_bot_team_flag_carrier_visible_bots_a(qa_game_ai_context_bots_a *ctx,
                                                qa_bot_state_bots_a *state);
int64_t qa_bot_team_flag_carrier_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
int64_t qa_bot_enemy_flag_carrier_visible_bots_a(qa_game_ai_context_bots_a *ctx,
                                                 qa_bot_state_bots_a *state);
int64_t qa_bot_team_cube_carrier_visible_bots_a(qa_game_ai_context_bots_a *ctx,
                                                qa_bot_state_bots_a *state);
int64_t qa_bot_enemy_cube_carrier_visible_bots_a(qa_game_ai_context_bots_a *ctx,
                                                 qa_bot_state_bots_a *state);
void qa_bot_visible_team_mates_and_enemies_bots_a(qa_game_ai_context_bots_a *ctx,
                                                  qa_bot_state_bots_a *state, double range,
                                                  int64_t *teammates, int64_t *enemies);
void qa_bot_remember_last_ordered_task_bots_a(qa_game_ai_context_bots_a *ctx,
                                              qa_bot_state_bots_a *state);
void qa_bot_set_team_status_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_bot_set_last_ordered_task_bots_a(qa_game_ai_context_bots_a *ctx,
                                         qa_bot_state_bots_a *state);
void qa_bot_refuse_order_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
void qa_bot_ctf_seek_goals_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
void qa_bot_ctf_retreat_goals_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
void qa_bot_1fctf_seek_goals_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
void qa_bot_1fctf_retreat_goals_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
void qa_bot_obelisk_seek_goals_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
void qa_bot_go_harvest_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
void qa_bot_obelisk_retreat_goals_bots_a(qa_game_ai_context_bots_a *ctx,
                                         qa_bot_state_bots_a *state);
void qa_bot_harvester_seek_goals_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
void qa_bot_harvester_retreat_goals_bots_a(qa_game_ai_context_bots_a *ctx,
                                           qa_bot_state_bots_a *state);
void qa_bot_team_goals_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                              bool retreat);
#endif
