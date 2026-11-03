/*
 * Port of src/bots/behavior/q3/ai-combat.ts (game/ai_dmq3.c). GPL-2.0-or-later.
 *
 * C accommodations:
 *  - TS closures `owned`/`ammo` in updateQ3BotWeaponInventory are fn + one ctx.
 *  - inFieldOfVision writes the normalized x/y back into `angles` as the TS
 *    Object.assign does, in axis order, stopping at the first failing axis.
 *  - `export { vectorToAngles }` is qa_vector_to_angles in qa/core/math.h.
 */
#ifndef QA_BOTS_BEHAVIOR_Q3_AI_COMBAT_H
#define QA_BOTS_BEHAVIOR_Q3_AI_COMBAT_H
#include "qa/bots/behavior/library/goals.h"
#include "qa/bots/behavior/q3/ai_state.h"
#include "qa/bots/behavior/q3/observations.h"
#include "qa/content/q3/base/shared/entity_state.h"
#include "qa/content/q3/base/world.h"
#include "qa/contracts/math.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct qa_game_ai_context_bots_a qa_game_ai_context_bots_a;

double qa_bot_inventory_value_bots_a(qa_bot_state_bots_a *state, int64_t index);
/* bounds NULL is the TS null: a point trace. */
qa_q3_base_server_trace_result qa_bot_ai_trace_bots_a(qa_game_ai_context_bots_a *ctx, qa_vec3 start,
                                                      qa_vec3 end, int64_t pass_entity,
                                                      int64_t mask, const qa_bounds *bounds);
qa_aas_entity_info_bots_a qa_bot_entity_info_bots_a(qa_game_ai_context_bots_a *ctx, int64_t entity);
bool qa_entity_is_dead_bots_a(qa_game_ai_context_bots_a *ctx, const qa_aas_entity_info_bots_a *info);
bool qa_entity_carries_flag_bots_a(qa_game_ai_context_bots_a *ctx,
                                   const qa_aas_entity_info_bots_a *info);
bool qa_entity_is_invisible_bots_a(qa_game_ai_context_bots_a *ctx,
                                   const qa_aas_entity_info_bots_a *info);
bool qa_entity_is_shooting_bots_a(const qa_aas_entity_info_bots_a *info);
bool qa_entity_is_chatting_bots_a(const qa_aas_entity_info_bots_a *info);
bool qa_entity_has_quad_bots_a(const qa_aas_entity_info_bots_a *info);
bool qa_entity_has_kamikaze_bots_a(const qa_aas_entity_info_bots_a *info);
bool qa_entity_carries_cubes_bots_a(qa_game_ai_context_bots_a *ctx,
                                    const qa_aas_entity_info_bots_a *info);
void qa_bot_choose_weapon_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
/* old_inventory has QA_Q3_BASE_MAX_ITEMS cells. */
void qa_bot_check_item_pickup_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                     const int32_t *old_inventory);
void qa_update_q3_bot_inventory_bots_a(qa_bot_state_bots_a *state);
void qa_update_q3_bot_weapon_inventory_bots_a(qa_bot_state_bots_a *state,
                                              bool (*owned)(void *ctx, int64_t weapon),
                                              double (*ammo)(void *ctx, int64_t weapon), void *ctx);
void qa_update_q3_bot_item_inventory_bots_a(qa_bot_state_bots_a *state);
void qa_bot_update_inventory_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
void qa_bot_update_battle_inventory_bots_a(qa_game_ai_context_bots_a *ctx,
                                           qa_bot_state_bots_a *state, int64_t enemy);
void qa_bot_use_kamikaze_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
void qa_bot_use_invulnerability_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
void qa_bot_battle_use_items_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
void qa_bot_set_teleport_time_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_bot_is_dead_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_bot_is_observer_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_bot_intermission_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_bot_in_lava_or_slime_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
double qa_bot_aggression_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
double qa_bot_feeling_bad_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_bot_wants_to_retreat_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_bot_wants_to_chase_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_bot_wants_to_help_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_bot_can_and_wants_to_rocket_jump_bots_a(qa_game_ai_context_bots_a *ctx,
                                                qa_bot_state_bots_a *state);
bool qa_bot_has_persistant_powerup_and_weapon_bots_a(qa_game_ai_context_bots_a *ctx,
                                                     qa_bot_state_bots_a *state);
void qa_bot_go_camp_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                           const qa_bot_goal *goal);
bool qa_bot_wants_to_camp_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
void qa_bot_dont_avoid_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                              const char *item_name);
void qa_bot_go_for_powerups_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_in_field_of_vision_bots_a(qa_vec3 viewangles, double fov, qa_vec3 *angles);
double qa_bot_entity_visible_bots_a(qa_game_ai_context_bots_a *ctx, int64_t viewer, qa_vec3 eye,
                                    qa_vec3 viewangles, double fov, int64_t entity);
bool qa_bot_find_enemy_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                              int64_t current_enemy);
void qa_bot_aim_at_enemy_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
void qa_bot_check_attack_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
void qa_bot_check_console_messages_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
void qa_bot_check_for_grenades_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                      const qa_q3_base_entity_state *entity);
void qa_bot_check_for_prox_mines_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                        const qa_q3_base_entity_state *entity);
void qa_bot_check_for_kamikaze_body_bots_a(qa_game_ai_context_bots_a *ctx,
                                           qa_bot_state_bots_a *state,
                                           const qa_q3_base_entity_state *entity);
void qa_bot_check_events_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                const qa_q3_base_entity_state *entity);
void qa_bot_check_snapshot_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
void qa_bot_check_air_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
void qa_bot_deathmatch_ai_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                 double think_time);
#endif
