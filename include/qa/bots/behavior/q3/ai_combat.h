/* Port of src/bots/behavior/q3/ai-combat.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_AI_COMBAT_H
#define QA_BOTS_BEHAVIOR_Q3_AI_COMBAT_H
#include "qa/bots/behavior/q3/ai_context.h"
#include "qa/bots/behavior/q3/observations.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
double qa_bot_inventory_value_bots_a(qa_bot_state_bots_a *state, int64_t index);
qa_aas_entity_info_bots_a qa_bot_entity_info_bots_a(qa_game_ai_context_bots_a *ctx, int64_t entity);
bool qa_entity_is_dead_bots_a(qa_game_ai_context_bots_a *ctx, qa_aas_entity_info_bots_a info);
bool qa_entity_carries_flag_bots_a(qa_game_ai_context_bots_a *ctx, qa_aas_entity_info_bots_a info,
                                   bool missionpack);
bool qa_entity_is_invisible_bots_a(qa_game_ai_context_bots_a *ctx, qa_aas_entity_info_bots_a info,
                                   bool missionpack);
bool qa_entity_is_shooting_bots_a(qa_aas_entity_info_bots_a info);
bool qa_entity_is_chatting_bots_a(qa_aas_entity_info_bots_a info);
bool qa_entity_has_quad_bots_a(qa_aas_entity_info_bots_a info);
bool qa_entity_has_kamikaze_bots_a(qa_aas_entity_info_bots_a info);
bool qa_bot_is_dead_bots_a(qa_bot_state_bots_a *state);
bool qa_bot_is_observer_bots_a(qa_bot_state_bots_a *state);
bool qa_bot_intermission_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
void qa_bot_set_teleport_time_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                     int64_t eflags);
double qa_bot_feeling_bad_bots_a(qa_bot_state_bots_a *state, bool melee, double weakness);
bool qa_bot_wants_to_help_bots_a(void);
bool qa_bot_can_and_wants_to_rocket_jump_bots_a(qa_game_ai_context_bots_a *ctx,
                                                qa_bot_state_bots_a *state);
bool qa_bot_has_persistant_powerup_and_weapon_bots_a(qa_bot_state_bots_a *state, bool missionpack);
bool qa_in_field_of_vision_bots_a(qa_vec3 viewangles, double fov, qa_vec3 *angles);
void qa_update_q3_bot_weapon_inventory_bots_a(qa_bot_state_bots_a *state, int64_t weapons_bits,
                                              const int64_t *ammo, size_t ammo_count);
void qa_bot_go_for_powerups_bots_a(qa_bot_state_bots_a *state);
void qa_bot_deathmatch_ai_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                 double think_time,
                                 bool (*run_node)(void *hook_ctx, qa_game_ai_context_bots_a *c,
                                                  qa_bot_state_bots_a *s),
                                 void *hook_ctx);
#endif
