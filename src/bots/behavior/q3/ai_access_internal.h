/*
 * Private helpers shared by ai_combat.c, ai_decision.c and ai_navigation.c: the
 * TS expressions these files repeat through context.game, context.library and
 * context.navigation. Every host leaf receives its own table's ctx.
 * GPL-2.0-or-later.
 */
#ifndef QA_BOTS_BEHAVIOR_Q3_AI_ACCESS_INTERNAL_H
#define QA_BOTS_BEHAVIOR_Q3_AI_ACCESS_INTERNAL_H
#include "qa/bots/behavior/q3/ai_context.h"
#include "qa/core/game_numeric.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Math.fround in a double expression. */
#define AI_F(x) ((double)(float)(x))

static inline qa_vec3 ai_fv(qa_vec3 v) { return (qa_vec3){AI_F(v.x), AI_F(v.y), AI_F(v.z)}; }

/* TS BotGoalState.copyFrom into a cell held as qa_bot_goal: float32/int32 stores. */
static inline void ai_goal_copy(qa_bot_goal *dst, const qa_bot_goal *src) {
  dst->origin = ai_fv(src->origin);
  dst->area = (int32_t)src->area;
  dst->mins = ai_fv(src->mins);
  dst->maxs = ai_fv(src->maxs);
  dst->entity = (int32_t)src->entity;
  dst->number = (int32_t)src->number;
  dst->flags = (int32_t)src->flags;
  dst->item_info = (int32_t)src->item_info;
}

static inline int32_t ai_int32(double value) { return qa_int32(value); }

static inline qa_game_ai_deathmatch_state_bots_a *ai_deathmatch(qa_game_ai_context_bots_a *ctx) {
  return &ctx->deathmatch;
}
static inline qa_bot_goal_library *ai_goals(qa_game_ai_context_bots_a *ctx) { return &ctx->library->goals; }
static inline qa_bot_action_buffer *ai_actions(qa_game_ai_context_bots_a *ctx) { return &ctx->library->actions; }
static inline qa_bot_chat_library *ai_chat(qa_game_ai_context_bots_a *ctx) { return &ctx->library->chat; }
static inline qa_bot_character_library *ai_characters(qa_game_ai_context_bots_a *ctx) {
  return &ctx->library->characters;
}
static inline qa_bot_lib_vars *ai_variables(qa_game_ai_context_bots_a *ctx) { return &ctx->library->variables; }
static inline bool ai_debug_build(qa_game_ai_context_bots_a *ctx) { return qa_bot_library_debug_build(ctx->library); }
static inline bool ai_is_missionpack(qa_game_ai_context_bots_a *ctx) {
  return ctx->game->options.product == QA_Q3_BASE_PRODUCT_MISSIONPACK;
}
static inline qa_aas_bsp_entities *ai_bsp(qa_game_ai_context_bots_a *ctx) { return ctx->host->bsp_entities; }

static inline double ai_random(qa_game_ai_context_bots_a *ctx) { return qa_game_ai_context_random(ctx); }
static inline double ai_crandom(qa_game_ai_context_bots_a *ctx) { return ctx->game->random.crandom(ctx->game->random.ctx); }
static inline double ai_character_bounded(qa_game_ai_context_bots_a *ctx, int64_t character, int64_t index) {
  return qa_bot_character_bounded_float(&ctx->library->characters, character, index, 0, 1);
}
static inline qa_aas_entity_info_bots_a ai_observation(qa_game_ai_context_bots_a *ctx, int64_t entity) {
  return qa_bot_entity_observations_info(&ctx->observations, entity);
}

static inline void ai_print(qa_game_ai_context_bots_a *ctx, const char *text) {
  ctx->game->options.engine.print(ctx->game->options.engine.ctx, text);
}
/* Owned "a" + "b" + "c". */
static inline char *ai_concat3(const char *a, const char *b, const char *c) {
  size_t la = strlen(a), lb = strlen(b), lc = strlen(c);
  char *out = malloc(la + lb + lc + 1);
  if (out == NULL)
    qa_range_error("Out of memory building bot AI text");
  memcpy(out, a, la);
  memcpy(out + la, b, lb);
  memcpy(out + la + lb, c, lc + 1);
  return out;
}

static inline qa_bot_observed_entity_bots_a ai_entity(qa_game_ai_context_bots_a *ctx, int64_t number) {
  return ctx->game->entity(ctx->game->ctx, number);
}
static inline int64_t ai_entity_count(qa_game_ai_context_bots_a *ctx) { return ctx->game->entity_count(ctx->game->ctx); }
static inline int64_t ai_game_game_type(qa_game_ai_context_bots_a *ctx) { return ctx->game->game_type(ctx->game->ctx); }
static inline int64_t ai_game_model_index(qa_game_ai_context_bots_a *ctx, const char *name) {
  return ctx->game->model_index(ctx->game->ctx, name);
}
static inline double ai_clock_intermission_time(qa_game_ai_context_bots_a *ctx) {
  return ctx->game->clock_intermission_time(ctx->game->ctx);
}
static inline qa_q3_base_server_trace_result ai_world_trace(qa_game_ai_context_bots_a *ctx,
                                                            const qa_q3_base_server_trace_query *query) {
  return ctx->game->trace(ctx->game->ctx, *query);
}
static inline int64_t ai_point_contents(qa_game_ai_context_bots_a *ctx, qa_vec3 point, int64_t pass_entity) {
  return ctx->game->point_contents(ctx->game->ctx, point, pass_entity);
}
static inline int64_t ai_host_point_contents(qa_game_ai_context_bots_a *ctx, qa_vec3 point) {
  return ctx->host->point_contents(ctx->host->ctx, point);
}
/* configstrings.get(index).slice(0, size - 1) */
static inline void ai_configstring(qa_game_ai_context_bots_a *ctx, int64_t index, char *out, size_t size) {
  qa_q3_config_string_store *store = ctx->game->options.configstrings;
  snprintf(out, size, "%s", store->get(store->ctx, (double)index));
}
static inline void ai_engine_get_userinfo(qa_game_ai_context_bots_a *ctx, int64_t client, char *out, size_t size) {
  snprintf(out, size, "%s", ctx->game->options.engine.get_userinfo(ctx->game->options.engine.ctx, client));
}
static inline void ai_engine_set_userinfo(qa_game_ai_context_bots_a *ctx, int64_t client, const char *userinfo) {
  ctx->game->options.engine.set_userinfo(ctx->game->options.engine.ctx, client, userinfo);
}

static inline int64_t ai_cvar_integer(qa_game_ai_context_bots_a *ctx, const char *name) {
  return qa_game_ai_context_cvar(ctx, name)->integer_value;
}
static inline void ai_cvar_update(qa_game_ai_context_bots_a *ctx, const char *name) {
  qa_bot_cvar_update_bots_a(qa_game_ai_context_cvar(ctx, name));
}
static inline void ai_register_cvar(qa_game_ai_context_bots_a *ctx, const char *name, const char *def) {
  qa_game_ai_context_register_cvar(ctx, name, def, 0);
}
/* cvars.find(name): false is TS undefined. */
static inline bool ai_registry_find_integer(qa_game_ai_context_bots_a *ctx, const char *name, int64_t *out) {
  qa_cvar_read read;
  if (!qa_cvar_registry_find(ctx->game->options.cvars, name, &read))
    return false;
  *out = (int64_t)read.integer_value;
  qa_cvar_read_free(&read);
  return true;
}

static inline void ai_reset_state(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  qa_game_ai_context_reset_state(ctx, state);
}
static inline void ai_entity_state_or_blank(qa_game_ai_context_bots_a *ctx, int64_t number,
                                            qa_q3_base_entity_state *out) {
  if (!qa_game_ai_context_get_entity_state(ctx, number, out))
    qa_q3_entity_state_init(out);
}
static inline int64_t ai_snapshot_entity(qa_game_ai_context_bots_a *ctx, int64_t client, int64_t sequence,
                                         qa_q3_base_entity_state *out) {
  qa_bot_snapshot_entity_bots_a snapshot = qa_game_ai_context_get_snapshot_entity(ctx, client, sequence);
  *out = snapshot.state;
  return snapshot.next_sequence;
}
static inline qa_vec3 ai_entity_pos_base(const qa_q3_base_entity_state *entity) { return entity->pos.base; }

/* Navigation. */
static inline bool ai_nav_ready(qa_game_ai_context_bots_a *ctx) {
  qa_bot_navigation_bots_a *nav = ctx->host->navigation;
  return nav->ready(nav->ctx);
}
static inline int64_t ai_nav_point_area(qa_game_ai_context_bots_a *ctx, qa_vec3 origin) {
  qa_bot_navigation_bots_a *nav = ctx->host->navigation;
  return nav->point_area(nav->ctx, origin);
}
static inline qa_bot_navigation_area ai_nav_area(qa_game_ai_context_bots_a *ctx, int64_t number) {
  qa_bot_navigation_bots_a *nav = ctx->host->navigation;
  return nav->get_area(nav->ctx, number);
}
/* traceAreas copied into out[maximum]. */
static inline size_t ai_nav_trace_areas(qa_game_ai_context_bots_a *ctx, qa_vec3 start, qa_vec3 end, int64_t maximum,
                                        qa_bot_area_crossing *out) {
  qa_bot_navigation_bots_a *nav = ctx->host->navigation;
  size_t count = 0;
  qa_bot_area_crossing *crossings = nav->trace_areas(nav->ctx, start, end, maximum, &count);
  if (count > (size_t)maximum)
    qa_range_error("traceAreas returned %zu crossings for maximum %lld", count, (long long)maximum);
  if (count != 0)
    memcpy(out, crossings, count * sizeof *out);
  free(crossings);
  return count;
}
/* bboxAreas(bounds).slice(0, capacity) */
static inline size_t ai_nav_bbox_areas(qa_game_ai_context_bots_a *ctx, qa_bounds bounds, size_t capacity, int64_t *out) {
  qa_bot_navigation_bots_a *nav = ctx->host->navigation;
  size_t count = 0;
  int64_t *areas = nav->bbox_areas(nav->ctx, bounds, &count);
  if (count > capacity)
    count = capacity;
  if (count != 0)
    memcpy(out, areas, count * sizeof *out);
  free(areas);
  return count;
}
static inline void ai_nav_set_area_enabled(qa_game_ai_context_bots_a *ctx, int64_t area, bool enabled) {
  qa_bot_navigation_bots_a *nav = ctx->host->navigation;
  nav->set_area_enabled(nav->ctx, area, enabled);
}
static inline double ai_nav_travel_time(qa_game_ai_context_bots_a *ctx, int64_t area, qa_vec3 origin, int64_t goal_area,
                                        int64_t travel_flags) {
  qa_bot_navigation_bots_a *nav = ctx->host->navigation;
  qa_area_travel_time_query query = {area, true, origin, goal_area, travel_flags};
  return nav->area_travel_time_to_goal(nav->ctx, query);
}
static inline qa_predicted_route ai_nav_predict_route(qa_game_ai_context_bots_a *ctx, int64_t area, qa_vec3 origin,
                                                      int64_t goal_area, int64_t travel_flags, int64_t maximum_areas,
                                                      int64_t maximum_time, int64_t stop_event, int64_t stop_contents,
                                                      int64_t stop_travel_flags, int64_t stop_area) {
  qa_bot_navigation_bots_a *nav = ctx->host->navigation;
  qa_predict_route_query query = {{area, origin, goal_area, travel_flags}, maximum_areas, maximum_time, stop_event,
                                  stop_contents, stop_travel_flags, stop_area};
  return nav->predict_route(nav->ctx, query);
}
/* alternativeRouteGoals copied into out[capacity]. */
static inline size_t ai_nav_alternative_route_goals(qa_game_ai_context_bots_a *ctx,
                                                    const qa_alternative_route_query *query, qa_alternative_goal *out,
                                                    size_t capacity) {
  qa_bot_navigation_bots_a *nav = ctx->host->navigation;
  size_t count = 0;
  qa_alternative_goal *goals = nav->alternative_route_goals(nav->ctx, *query, &count);
  if (count > capacity)
    qa_range_error("alternativeRouteGoals returned %zu goals for %zu cells", count, capacity);
  if (count != 0)
    memcpy(out, goals, count * sizeof *out);
  free(goals);
  return count;
}
static inline void ai_nav_move_to_goal(qa_game_ai_context_bots_a *ctx, qa_bot_move_result *result, int64_t move_state,
                                       const qa_bot_goal *goal, int64_t travel_flags) {
  qa_bot_navigation_bots_a *nav = ctx->host->navigation;
  nav->move_to_goal(nav->ctx, result, move_state, *goal, travel_flags);
}
static inline bool ai_nav_move_in_direction(qa_game_ai_context_bots_a *ctx, int64_t move_state, qa_vec3 direction,
                                            double speed, int64_t move_type) {
  qa_bot_navigation_bots_a *nav = ctx->host->navigation;
  return nav->move_in_direction(nav->ctx, move_state, direction, speed, move_type);
}
static inline bool ai_nav_movement_view_target(qa_game_ai_context_bots_a *ctx, int64_t move_state,
                                               const qa_bot_goal *goal, int64_t travel_flags, double look_ahead,
                                               qa_vec3 *out) {
  qa_bot_navigation_bots_a *nav = ctx->host->navigation;
  return nav->movement_view_target(nav->ctx, move_state, *goal, travel_flags, look_ahead, out);
}
static inline bool ai_nav_predict_visible_position(qa_game_ai_context_bots_a *ctx, qa_vec3 origin, int64_t area,
                                                   const qa_bot_goal *goal, int64_t travel_flags, qa_vec3 *out) {
  qa_bot_navigation_bots_a *nav = ctx->host->navigation;
  return nav->predict_visible_position(nav->ctx, origin, area, *goal, travel_flags, out);
}
static inline bool ai_nav_swimming(qa_game_ai_context_bots_a *ctx, qa_vec3 origin) {
  qa_bot_navigation_bots_a *nav = ctx->host->navigation;
  return nav->swimming(nav->ctx, origin);
}
static inline qa_vec3 ai_nav_predict_client_movement(qa_game_ai_context_bots_a *ctx,
                                                     const qa_bot_movement_prediction *query) {
  qa_bot_navigation_bots_a *nav = ctx->host->navigation;
  return nav->predict_client_movement_end(nav->ctx, *query);
}
static inline qa_bounds ai_nav_presence_bounds(qa_game_ai_context_bots_a *ctx, int64_t presence) {
  qa_bot_navigation_bots_a *nav = ctx->host->navigation;
  return nav->presence_bounds(nav->ctx, presence);
}

/* Movement state store. */
static inline void ai_move_initialize(qa_game_ai_context_bots_a *ctx, int64_t handle, const qa_bot_init_move *input) {
  qa_bot_move_state_store_initialize(&ctx->library->move_states, handle, *input);
}
static inline void ai_move_reset(qa_game_ai_context_bots_a *ctx, int64_t handle) {
  qa_bot_move_state_store_reset(&ctx->library->move_states, handle);
}
static inline void ai_move_reset_avoid_reach(qa_game_ai_context_bots_a *ctx, int64_t handle) {
  qa_bot_move_state_store_reset_avoid_reach(&ctx->library->move_states, handle);
}
static inline void ai_move_reset_last_avoid_reach(qa_game_ai_context_bots_a *ctx, int64_t handle) {
  qa_bot_move_state_store_reset_last_avoid_reach(&ctx->library->move_states, handle);
}
static inline void ai_move_add_avoid_spot(qa_game_ai_context_bots_a *ctx, int64_t handle, qa_vec3 origin, double radius,
                                          int64_t type) {
  qa_bot_move_state_store_add_avoid_spot(&ctx->library->move_states, handle, origin, radius, type);
}

/* Arsenal knowledge. */
static inline int64_t ai_knowledge_choose_weapon(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  const qa_bot_arsenal_interface_bots_a *k = ctx->game->knowledge;
  return k->choose_weapon(k->ctx, ctx->library, state);
}
static inline int64_t ai_knowledge_activation_weapon(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  const qa_bot_arsenal_interface_bots_a *k = ctx->game->knowledge;
  return k->activation_weapon(k->ctx, ctx->library, state);
}
static inline qa_bot_weapon_tactics_bots_a ai_knowledge_tactics(qa_game_ai_context_bots_a *ctx, int64_t weapon) {
  const qa_bot_arsenal_interface_bots_a *k = ctx->game->knowledge;
  return k->tactics(k->ctx, weapon);
}
static inline double ai_knowledge_aggression(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  const qa_bot_arsenal_interface_bots_a *k = ctx->game->knowledge;
  return k->aggression(k->ctx, state);
}
static inline void ai_knowledge_update_inventory(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  const qa_bot_arsenal_interface_bots_a *k = ctx->game->knowledge;
  k->update_inventory(k->ctx, state);
}
static inline bool ai_knowledge_weapon_info(qa_game_ai_context_bots_a *ctx, int64_t handle, int64_t weapon,
                                            qa_weapon_info_bots_a *out) {
  const qa_bot_arsenal_interface_bots_a *k = ctx->game->knowledge;
  return k->weapon_info(k->ctx, ctx->library, handle, weapon, out);
}
static inline bool ai_knowledge_has_weapon(qa_game_ai_context_bots_a *ctx, int64_t handle, int64_t weapon) {
  qa_weapon_info_bots_a info;
  return ai_knowledge_weapon_info(ctx, handle, weapon, &info);
}

/* TS state.inventory passed as WeightInventory. */
static inline double ai_inventory_read(void *ctx, int64_t index) {
  const qa_bot_state_bots_a *state = ctx;
  if (index < 0 || index >= QA_Q3_BASE_MAX_ITEMS)
    qa_range_error("inventory index %lld is outside %d entries", (long long)index, QA_Q3_BASE_MAX_ITEMS);
  return state->src->inventory[index];
}
static inline qa_weight_inventory ai_inventory(qa_bot_state_bots_a *state) {
  return (qa_weight_inventory){NULL, QA_Q3_BASE_MAX_ITEMS, ai_inventory_read, state};
}
#endif
