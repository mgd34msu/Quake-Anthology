/*
 * Private helpers shared by ai_orders.c, ai_team.c, ai_chat.c, ai_command.c,
 * ai_voice.c and ai_main.c: the TS expressions these files repeat through
 * context.game.options. GPL-2.0-or-later.
 */
#ifndef QA_BOTS_BEHAVIOR_Q3_AI_ORDERS_INTERNAL_H
#define QA_BOTS_BEHAVIOR_Q3_AI_ORDERS_INTERNAL_H
#include "qa/bots/behavior/q3/ai_chat.h"
#include "qa/bots/behavior/q3/ai_context.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define QA_AI_CS_PLAYERS 544
#define QA_AI_CS_BOTINFO 25
#define QA_AI_MAX_CLIENTS 64
#define QA_AI_INFO_SIZE 1024
/* configstrings.get(index).slice(0, 1023) into buffer[QA_AI_INFO_SIZE]. */
const char *qa_ai_configstring_bots_a(qa_game_ai_context_bots_a *ctx, int64_t index, char *buffer);
/* infoValueForKey into out; values fit QA_AI_INFO_SIZE. */
char *qa_ai_info_value_bots_a(const char *info, const char *key, char *out);
void qa_ai_print_bots_a(qa_game_ai_context_bots_a *ctx, const char *text);
/* cvars.find("sv_maxclients")?.integerValue ?? 0 */
int64_t qa_ai_sv_maxclients_bots_a(qa_game_ai_context_bots_a *ctx);
bool qa_ai_missionpack_bots_a(qa_game_ai_context_bots_a *ctx);
/* The shared `get ?? cvar; set; min(MAX_CLIENTS)` of the ai_chat.c/ai_cmd.c statics. */
int64_t qa_ai_cached_max_clients_bots_a(qa_game_ai_context_bots_a *ctx,
                                        qa_game_ai_max_clients_cell_bots_a *cells, size_t *count,
                                        size_t capacity, const char *name);
/* ASCII case-insensitive equality: the TS folded() and toLowerCase() comparisons. */
bool qa_ai_fold_equal_bots_a(const char *first, const char *second);
/* navigation.areaTravelTimeToGoal with TravelFlags.DEFAULT. */
double qa_ai_area_travel_time_bots_a(qa_game_ai_context_bots_a *ctx, int64_t area, qa_vec3 origin,
                                     int64_t goal_area);
bool qa_ai_navigation_ready_bots_a(qa_game_ai_context_bots_a *ctx);
/* context.getClientState(client): false is TS null; only the origin is read from the copy. */
bool qa_ai_client_origin_bots_a(qa_game_ai_context_bots_a *ctx, int64_t client, qa_vec3 *origin);
/* getClientState(client).persistant.get(PERS_SCORE); false is TS null. */
bool qa_ai_client_score_bots_a(qa_game_ai_context_bots_a *ctx, int64_t client, double *score);
#endif
