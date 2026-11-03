/* Port of src/bots/behavior/q3/ai-decision.ts (game/ai_dmnet.c). GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_AI_DECISION_H
#define QA_BOTS_BEHAVIOR_Q3_AI_DECISION_H
#include "qa/bots/behavior/library/goals.h"
#include "qa/bots/behavior/q3/ai_state.h"
#include "qa/bots/behavior/q3/movement_state.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct qa_game_ai_context_bots_a qa_game_ai_context_bots_a;

void qa_bot_reset_node_switches_bots_a(qa_game_ai_context_bots_a *ctx);
void qa_bot_record_node_switch_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                      const char *node, const char *detail, const char *reason);
void qa_bot_dump_node_switches_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_bot_get_air_goal_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                qa_bot_goal *goal);
/* long_term_goal NULL is the TS null. */
bool qa_bot_go_for_air_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                              int64_t flags, const qa_bot_goal *long_term_goal, double range);
bool qa_bot_nearby_goal_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                               int64_t flags, const qa_bot_goal *long_term_goal, double range);
bool qa_bot_reached_goal_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                const qa_bot_goal *goal);
bool qa_bot_get_item_long_term_goal_bots_a(qa_game_ai_context_bots_a *ctx,
                                           qa_bot_state_bots_a *state, int64_t flags,
                                           qa_bot_goal *goal);
bool qa_bot_get_long_term_goal_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                      int64_t flags, bool retreat, qa_bot_goal *goal);
bool qa_bot_long_term_goal_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                  int64_t flags, bool retreat, qa_bot_goal *goal);
void qa_bot_clear_path_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                              qa_bot_move_result *result);
void qa_ai_enter_intermission_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                     const char *reason);
void qa_ai_enter_observer_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                 const char *reason);
void qa_ai_enter_stand_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                              const char *reason);
void qa_ai_enter_respawn_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                const char *reason);
void qa_ai_enter_seek_activate_entity_bots_a(qa_game_ai_context_bots_a *ctx,
                                             qa_bot_state_bots_a *state, const char *reason);
void qa_ai_enter_seek_nbg_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                 const char *reason);
void qa_ai_enter_seek_ltg_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                 const char *reason);
void qa_ai_enter_battle_fight_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                     const char *reason);
void qa_ai_enter_battle_suicidal_fight_bots_a(qa_game_ai_context_bots_a *ctx,
                                              qa_bot_state_bots_a *state, const char *reason);
void qa_ai_enter_battle_chase_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                     const char *reason);
void qa_ai_enter_battle_retreat_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                       const char *reason);
void qa_ai_enter_battle_nbg_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                   const char *reason);
bool qa_ai_node_intermission_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_ai_node_observer_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_ai_node_stand_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_ai_node_respawn_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_ai_node_seek_activate_entity_bots_a(qa_game_ai_context_bots_a *ctx,
                                            qa_bot_state_bots_a *state);
bool qa_ai_node_seek_nbg_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_ai_node_seek_ltg_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_ai_node_battle_fight_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_ai_node_battle_chase_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_ai_node_battle_retreat_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_ai_node_battle_nbg_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
/* Source ainode function-pointer dispatch; false runs the next node in the same think. */
bool qa_run_ai_node_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
#endif
