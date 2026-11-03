/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/ai_navigation.h"
#include "ai_access_internal.h"
#include "qa/bots/behavior/q3/ai_combat.h"
#include "qa/bots/behavior/q3/ai_decision.h"
#include "qa/bots/behavior/q3/ai_orders.h"
#include "qa/content/q3/base/game/format.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/core/game_numeric.h"
#include "qa/core/info_string.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  CONTENTS_SOLID = 1,
  CONTENTS_LAVA = 8,
  CONTENTS_SLIME = 16,
  CONTENTS_TRIGGER = 0x40000000,
  MASK_SHOT = CONTENTS_SOLID | 0x02000000 | 0x04000000,
  AREA_CONTENTS_MOVER = 1024,
  BSP_MODEL_SIZE = 128,
  BSP_LONG_SIZE = 1024,
  MAX_TRACE_CROSSINGS = 10
};

static bool same_vector(qa_vec3 a, qa_vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

static int upper_byte(const char *text, size_t index, size_t length) {
  int value = index < length ? (unsigned char)text[index] : 0;
  return value >= 97 && value <= 122 ? value - 32 : value;
}

/* TS sameName: ASCII case folding over the full strings. */
static bool same_name(const char *first, const char *second) {
  size_t first_length = strlen(first), second_length = strlen(second);
  for (size_t index = 0;; index++) {
    int left = upper_byte(first, index, first_length), right = upper_byte(second, index, second_length);
    if (left != right)
      return false;
    if (left == 0)
      return true;
  }
}

static qa_vec3 multiply_add(qa_vec3 origin, double scale, qa_vec3 direction) {
  return qa_add3(origin, qa_scale3(direction, scale));
}

static bool reachable(qa_game_ai_context_bots_a *ctx, int64_t area) {
  return ai_nav_area(ctx, area).reachable_area_count != 0;
}

/* Output bytes up to the first NUL, as the TS text loop reads them. */
static bool bsp_value(qa_game_ai_context_bots_a *ctx, int64_t entity, const char *key, char *out, size_t size) {
  memset(out, 0, size);
  return qa_aas_bsp_entities_value(ai_bsp(ctx), entity, key, (uint8_t *)out, size);
}

static bool bsp_value_equals(qa_game_ai_context_bots_a *ctx, int64_t entity, const char *key, const char *wanted) {
  char value[BSP_MODEL_SIZE];
  return bsp_value(ctx, entity, key, value, sizeof value) && strcmp(value, wanted) == 0;
}

static void warning(qa_game_ai_context_bots_a *ctx, const char *text) {
  char *line = ai_concat3("^3Warning: ", text, "");
  ai_print(ctx, line);
  free(line);
}

static void error_text(qa_game_ai_context_bots_a *ctx, const char *text) {
  char *line = ai_concat3("^1Error: ", text, "");
  ai_print(ctx, line);
  free(line);
}

static double travel_time(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, int64_t goal_area) {
  return ai_nav_travel_time(ctx, state->src->area_num, qa_bot_v3(state->src->origin), goal_area, state->src->tfl);
}

int64_t qa_bot_point_area_num_bots_a(qa_game_ai_context_bots_a *ctx, qa_vec3 origin) {
  int64_t area = ai_nav_point_area(ctx, origin);
  if (area != 0)
    return area;
  if (!ai_nav_ready(ctx))
    return 0;
  qa_bot_area_crossing crossings[MAX_TRACE_CROSSINGS];
  size_t count = ai_nav_trace_areas(ctx, origin, qa_math_vec3(origin.x, origin.y, origin.z + 10),
                                    MAX_TRACE_CROSSINGS, crossings);
  return count == 0 ? 0 : crossings[0].area;
}

void qa_bot_setup_for_movement_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  const qa_bot_player_state_source *player = &state->src->cur_ps;
  int64_t flags = 0;
  int64_t pm_flags = player->pm_flags;
  if (player->ground_entity_num != QA_Q3_BASE_ENTITYNUM_NONE)
    flags |= QA_BOT_MF_ONGROUND;
  if ((pm_flags & QA_Q3_FLAG_TIME_KNOCKBACK) != 0 && player->pm_time > 0)
    flags |= QA_BOT_MF_TELEPORTED;
  if ((pm_flags & QA_Q3_FLAG_TIME_WATERJUMP) != 0 && player->pm_time > 0)
    flags |= QA_BOT_MF_WATERJUMP;
  if (state->src->walker > 0.5)
    flags |= QA_BOT_MF_WALK;
  qa_bot_init_move input = {0};
  input.origin = qa_bot_v3(player->origin);
  input.velocity = qa_bot_v3(player->velocity);
  input.view_offset = qa_math_vec3(0, 0, player->viewheight);
  input.entity_num = state->src->entity_num;
  input.client = state->src->client;
  input.think_time = state->src->think_time;
  input.presence_type = (pm_flags & QA_Q3_FLAG_DUCKED) != 0 ? 4 : 2;
  input.view_angles = qa_bot_v3(state->src->viewangles);
  input.or_move_flags = flags;
  ai_move_initialize(ctx, state->src->ms, &input);
}

qa_bot_waypoint_bots_a *qa_bot_create_way_point_bots_a(qa_game_ai_context_bots_a *ctx, const char *name,
                                                       qa_vec3 origin, int64_t area) {
  qa_game_ai_deathmatch_state_bots_a *globals = ai_deathmatch(ctx);
  qa_bot_waypoint_bots_a *waypoint = globals->free_waypoints;
  if (waypoint == NULL) {
    warning(ctx, "BotCreateWayPoint: Out of waypoints\n");
    return NULL;
  }
  globals->free_waypoints = waypoint->next;
  /* A C string already stops at its first NUL; slice(0, 31) keeps 31 bytes. */
  snprintf(waypoint->name, sizeof waypoint->name < 32 ? sizeof waypoint->name : 32, "%s", name);
  waypoint->goal.origin = qa_bot_fv3(origin);
  waypoint->goal.mins = qa_bot_fv3(qa_math_vec3(-8, -8, -8));
  waypoint->goal.maxs = qa_bot_fv3(qa_math_vec3(8, 8, 8));
  waypoint->goal.area = (int32_t)area;
  waypoint->next = NULL;
  waypoint->prev = NULL;
  return waypoint;
}

qa_bot_waypoint_bots_a *qa_bot_find_way_point_bots_a(qa_bot_waypoint_bots_a *waypoints, const char *name) {
  for (qa_bot_waypoint_bots_a *waypoint = waypoints; waypoint != NULL; waypoint = waypoint->next)
    if (same_name(waypoint->name, name))
      return waypoint;
  return NULL;
}

void qa_bot_free_waypoints_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_waypoint_bots_a *first) {
  qa_game_ai_deathmatch_state_bots_a *globals = ai_deathmatch(ctx);
  qa_bot_waypoint_bots_a *waypoint = first;
  while (waypoint != NULL) {
    qa_bot_waypoint_bots_a *next = waypoint->next;
    waypoint->next = globals->free_waypoints;
    globals->free_waypoints = waypoint;
    waypoint = next;
  }
}

void qa_bot_init_waypoints_bots_a(qa_game_ai_context_bots_a *ctx) {
  qa_game_ai_deathmatch_state_bots_a *globals = ai_deathmatch(ctx);
  globals->free_waypoints = NULL;
  for (size_t index = 0; index < QA_MAX_WAYPOINTS_BOTS_A; index++) {
    globals->waypoints[index].next = globals->free_waypoints;
    globals->free_waypoints = &globals->waypoints[index];
  }
}

qa_vec3 qa_bot_roam_goal_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  qa_vec3 best = qa_math_vec3(0, 0, 0);
  for (int attempt = 0; attempt < 10; attempt++) {
    best = qa_math_vec3(state->src->origin.x, state->src->origin.y, state->src->origin.z);
    double random = ai_random(ctx);
    if (random > 0.25) {
      double sign = ai_random(ctx) < 0.5 ? -1 : 1;
      double step = AI_F(AI_F(800 * ai_random(ctx)) + 100);
      best = qa_math_vec3(best.x + sign * step, best.y, best.z);
    }
    if (random < 0.75) {
      double sign = ai_random(ctx) < 0.5 ? -1 : 1;
      double step = AI_F(AI_F(800 * ai_random(ctx)) + 100);
      best = qa_math_vec3(best.x, best.y + sign * step, best.z);
    }
    best = qa_math_vec3(best.x, best.y, best.z + AI_F(96 * ai_crandom(ctx)));
    qa_q3_base_server_trace_result trace =
        qa_bot_ai_trace_bots_a(ctx, qa_bot_v3(state->src->origin), best, state->src->entity_num, CONTENTS_SOLID, NULL);
    qa_vec3 direction = qa_sub3(trace.end, qa_bot_v3(state->src->origin));
    double distance = qa_length3(direction);
    if (distance > 200) {
      best = multiply_add(qa_bot_v3(state->src->origin), AI_F(AI_F(distance * trace.fraction) - 40), qa_normalize3(direction));
      qa_vec3 below = qa_math_vec3(best.x, best.y, best.z - 800);
      qa_q3_base_server_trace_result floor_trace =
          qa_bot_ai_trace_bots_a(ctx, best, below, state->src->entity_num, CONTENTS_SOLID, NULL);
      if (floor_trace.solidity == QA_Q3_BASE_SOLID_CLEAR) {
        int64_t contents = ai_point_contents(
            ctx, qa_math_vec3(floor_trace.end.x, floor_trace.end.y, floor_trace.end.z + 1), state->src->entity_num);
        if ((contents & (CONTENTS_LAVA | CONTENTS_SLIME)) == 0)
          return best;
      }
    }
  }
  return best;
}

qa_bot_move_result qa_bot_attack_move_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                             int64_t travel_flags) {
  qa_bot_move_result result = {0};
  if (state->src->attack_chase_time > ctx->time) {
    qa_bot_goal goal = {0};
    goal.entity = state->src->enemy;
    goal.area = state->src->last_enemy_area_num;
    goal.origin = ai_fv(qa_bot_v3(state->src->last_enemy_origin));
    goal.mins = qa_math_vec3(-8, -8, -8);
    goal.maxs = qa_math_vec3(8, 8, 8);
    qa_bot_setup_for_movement_bots_a(ctx, state);
    ai_nav_move_to_goal(ctx, &result, state->src->ms, &goal, travel_flags);
    return result;
  }
  double skill = ai_character_bounded(ctx, state->src->character, QA_BCHAR_ATTACK_SKILL);
  double jumper = ai_character_bounded(ctx, state->src->character, QA_BCHAR_JUMPER);
  double croucher = ai_character_bounded(ctx, state->src->character, QA_BCHAR_CROUCHER);
  if (skill < AI_F(0.2))
    return result;
  qa_bot_setup_for_movement_bots_a(ctx, state);
  qa_aas_entity_info_bots_a enemy = qa_bot_entity_info_bots_a(ctx, state->src->enemy);
  qa_vec3 toward = qa_sub3(enemy.origin, qa_bot_v3(state->src->origin));
  double distance = qa_length3(toward);
  qa_vec3 forward = qa_normalize3(toward), backward = qa_scale3(forward, -1);
  int64_t move_type = QA_BOT_MOVE_WALK;
  if (state->src->attack_crouch_time < AI_F(ctx->time - 1)) {
    if (ai_random(ctx) < jumper)
      move_type = QA_BOT_MOVE_JUMP;
    else if (state->src->attack_crouch_time < AI_F(ctx->time - 1) && ai_random(ctx) < croucher)
      state->src->attack_crouch_time = (float)(ctx->time + AI_F(croucher * 5));
  }
  if (state->src->attack_crouch_time > ctx->time)
    move_type = QA_BOT_MOVE_CROUCH;
  if (move_type == QA_BOT_MOVE_JUMP) {
    if (state->src->attack_jump_time > ctx->time)
      move_type = QA_BOT_MOVE_WALK;
    else
      state->src->attack_jump_time = (float)(ctx->time + 1);
  }
  bool has_weapon = ai_knowledge_has_weapon(ctx, state->src->ws, qa_bot_state_weapon_checked(state));
  bool melee = has_weapon && ai_knowledge_tactics(ctx, qa_bot_state_weapon_checked(state)).melee;
  double attack_distance = melee ? 0 : 140;
  double attack_range = melee ? 0 : 40;
  if (skill <= AI_F(0.4)) {
    if (distance > attack_distance + attack_range &&
        ai_nav_move_in_direction(ctx, state->src->ms, forward, 400, move_type))
      return result;
    if (distance < attack_distance - attack_range &&
        ai_nav_move_in_direction(ctx, state->src->ms, backward, 400, move_type))
      return result;
    return result;
  }
  state->src->attack_strafe_time = (float)(state->src->attack_strafe_time + state->src->think_time);
  double change_time = AI_F(AI_F(0.4) + AI_F(AI_F(1 - skill) * AI_F(0.2)));
  if (skill > AI_F(0.7))
    change_time = AI_F(change_time + AI_F(ai_crandom(ctx) * AI_F(0.2)));
  if (state->src->attack_strafe_time > change_time && ai_random(ctx) > AI_F(0.935)) {
    state->src->flags ^= QA_BOT_FLAG_STRAFERIGHT;
    state->src->attack_strafe_time = 0;
  }
  for (int attempt = 0; attempt < 2; attempt++) {
    qa_vec3 horizontal = qa_normalize3(qa_math_vec3(forward.x, forward.y, 0));
    qa_vec3 sideward = qa_cross3(horizontal, qa_math_vec3(0, 0, 1));
    if ((state->src->flags & QA_BOT_FLAG_STRAFERIGHT) != 0)
      sideward = qa_scale3(sideward, -1);
    if (ai_random(ctx) > AI_F(0.9))
      sideward = qa_add3(sideward, backward);
    else if (distance > attack_distance + attack_range)
      sideward = qa_add3(sideward, forward);
    else if (distance < attack_distance - attack_range)
      sideward = qa_add3(sideward, backward);
    if (ai_nav_move_in_direction(ctx, state->src->ms, sideward, 400, move_type))
      return result;
    state->src->flags ^= QA_BOT_FLAG_STRAFERIGHT;
    state->src->attack_strafe_time = 0;
  }
  return result;
}

void qa_bot_set_movedir_bots_a(qa_vec3 angles, qa_vec3 *movedir) {
  if (same_vector(angles, qa_math_vec3(0, -1, 0)))
    *movedir = qa_math_vec3(0, 0, 1);
  else if (same_vector(angles, qa_math_vec3(0, -2, 0)))
    *movedir = qa_math_vec3(0, 0, -1);
  else
    *movedir = qa_qvm_angle_vectors(angles).forward;
}

int64_t qa_bot_model_mins_maxs_bots_a(qa_game_ai_context_bots_a *ctx, int64_t model_index, int64_t entity_type,
                                      int64_t contents, qa_vec3 *mins, qa_vec3 *maxs) {
  int64_t count = ai_entity_count(ctx);
  for (int64_t index = 0; index < count; index++) {
    qa_bot_observed_entity_bots_a entity = ai_entity(ctx, index);
    if (!entity.present || (entity_type != 0 && entity.state.e_type != entity_type) ||
        (contents != 0 && entity.contents != contents))
      continue;
    if (entity.state.modelindex != model_index)
      continue;
    if (mins != NULL)
      *mins = qa_add3(entity.origin, entity.bounds.min);
    if (maxs != NULL)
      *maxs = qa_add3(entity.origin, entity.bounds.max);
    return index;
  }
  if (mins != NULL)
    *mins = qa_math_vec3(0, 0, 0);
  if (maxs != NULL)
    *maxs = qa_math_vec3(0, 0, 0);
  return 0;
}

static void activation_entity(qa_bot_goal_state_bots_a *goal, int64_t entity) {
  goal->entity = (int32_t)entity;
  goal->number = 0;
  goal->flags = 0;
}

static void stand_and_shoot(qa_bot_state_bots_a *state, qa_bot_activate_goal_bots_a *activation, int64_t entity) {
  activation_entity(&activation->src->goal, entity);
  activation->src->goal.origin = qa_bot_fv3(qa_bot_v3(state->src->origin));
  activation->src->goal.area = state->src->area_num;
  activation->src->goal.mins = qa_bot_fv3(qa_math_vec3(-8, -8, -8));
  activation->src->goal.maxs = qa_bot_fv3(qa_math_vec3(8, 8, 8));
}

static void expand_button_goal(qa_bot_goal_state_bots_a *goal, qa_vec3 direction) {
  goal->mins = qa_bot_fv3(qa_math_vec3((double)goal->mins.x + (direction.x < 0 ? 0 : fabs(direction.x)),
                                       (double)goal->mins.y + (direction.y < 0 ? 0 : fabs(direction.y)),
                                       (double)goal->mins.z + (direction.z < 0 ? 0 : fabs(direction.z))));
  goal->maxs = qa_bot_fv3(qa_math_vec3((double)goal->maxs.x + (direction.x < 0 ? fabs(direction.x) : 0),
                                       (double)goal->maxs.y + (direction.y < 0 ? fabs(direction.y) : 0),
                                       (double)goal->maxs.z + (direction.z < 0 ? fabs(direction.z) : 0)));
}

bool qa_bot_func_button_activate_goal_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                             int64_t bsp_entity, qa_bot_activate_goal_bots_a *activation) {
  activation->src->shoot = 0;
  activation->src->target = qa_bot_fv3(qa_math_vec3(0, 0, 0));
  char model[BSP_MODEL_SIZE];
  if (!bsp_value(ctx, bsp_entity, "model", model, sizeof model) || model[0] == 0)
    return false;
  int64_t model_index = qa_game_atoi(model + 1);
  if (model_index == 0)
    return false;
  qa_vec3 mins = qa_math_vec3(0, 0, 0), maxs = qa_math_vec3(0, 0, 0);
  int64_t entity = qa_bot_model_mins_maxs_bots_a(ctx, model_index, QA_Q3_BASE_ET_MOVER, 0, &mins, &maxs);
  qa_aas_bsp_entities *bsp = ai_bsp(ctx);
  (void)qa_aas_bsp_entities_float(bsp, bsp_entity, "lip"); /* The source reads lip and never uses it. */
  double angle = qa_aas_bsp_entities_float(bsp, bsp_entity, "angle").value;
  qa_vec3 direction = qa_math_vec3(0, 0, 0);
  qa_bot_set_movedir_bots_a(qa_math_vec3(0, angle, 0), &direction);
  qa_vec3 size = qa_sub3(maxs, mins), origin = qa_scale3(qa_add3(mins, maxs), 0.5);
  double distance =
      qa_dot3(qa_math_vec3(fabs(direction.x), fabs(direction.y), fabs(direction.z)), size);
  distance = AI_F(distance * 0.5);
  double health = qa_aas_bsp_entities_float(bsp, bsp_entity, "health").value;
  if (health != 0) {
    qa_vec3 target = multiply_add(origin, -distance, direction);
    activation->src->target = qa_bot_fv3(target);
    activation->src->shoot = 1;
    qa_q3_base_server_trace_result trace =
        qa_bot_ai_trace_bots_a(ctx, qa_bot_v3(state->src->eye), target, state->src->entity_num, MASK_SHOT, NULL);
    if (trace.fraction >= 1 || trace.entity_num == entity) {
      stand_and_shoot(state, activation, entity);
      return true;
    }
  }
  qa_bounds bounds = ai_nav_presence_bounds(ctx, 4);
  distance = AI_F(distance + AI_F(fabs(direction.x) * fabs(direction.x < 0 ? bounds.max.x : bounds.min.x)));
  distance = AI_F(distance + AI_F(fabs(direction.y) * fabs(direction.y < 0 ? bounds.max.y : bounds.min.y)));
  distance = AI_F(distance + AI_F(fabs(direction.z) * fabs(direction.z < 0 ? bounds.max.z : bounds.min.z)));
  qa_vec3 goal_origin = multiply_add(origin, -distance, direction);
  qa_vec3 start = qa_math_vec3(goal_origin.x, goal_origin.y, goal_origin.z + 24);
  qa_bot_area_crossing crossings[MAX_TRACE_CROSSINGS];
  size_t crossing_count = ai_nav_trace_areas(
      ctx, start, qa_math_vec3(start.x, start.y, start.z - (health != 0 ? 512 : 100)), MAX_TRACE_CROSSINGS,
      crossings);
  if (health != 0) {
    for (size_t index = crossing_count; index-- > 0;) {
      const qa_bot_area_crossing *crossing = &crossings[index];
      if (!reachable(ctx, crossing->area))
        continue;
      activation->src->goal.origin = qa_bot_fv3(crossing->point);
      activation->src->goal.area = (int32_t)crossing->area;
      activation->src->goal.mins = qa_bot_fv3(qa_math_vec3(8, 8, 8));
      activation->src->goal.maxs = qa_bot_fv3(qa_math_vec3(-8, -8, -8));
      expand_button_goal(&activation->src->goal, direction);
      activation_entity(&activation->src->goal, entity);
      return true;
    }
  } else {
    for (size_t index = 0; index < crossing_count; index++) {
      const qa_bot_area_crossing *crossing = &crossings[index];
      if (!reachable(ctx, crossing->area))
        continue;
      activation->src->goal.origin = qa_bot_fv3(origin);
      activation->src->goal.area = (int32_t)crossing->area;
      activation->src->goal.mins = qa_bot_fv3(qa_sub3(mins, origin));
      activation->src->goal.maxs = qa_bot_fv3(qa_sub3(maxs, origin));
      expand_button_goal(&activation->src->goal, direction);
      activation_entity(&activation->src->goal, entity);
      return true;
    }
  }
  return false;
}

bool qa_bot_func_door_activate_goal_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                           int64_t bsp_entity, qa_bot_activate_goal_bots_a *activation) {
  char model[BSP_LONG_SIZE];
  if (!bsp_value(ctx, bsp_entity, "model", model, sizeof model) || model[0] == 0)
    return false;
  int64_t model_index = qa_game_atoi(model + 1);
  if (model_index == 0)
    return false;
  qa_vec3 mins = qa_math_vec3(0, 0, 0), maxs = qa_math_vec3(0, 0, 0);
  int64_t entity = qa_bot_model_mins_maxs_bots_a(ctx, model_index, QA_Q3_BASE_ET_MOVER, 0, &mins, &maxs);
  activation->src->target = qa_bot_fv3(qa_scale3(qa_add3(mins, maxs), 0.5));
  activation->src->shoot = 1;
  stand_and_shoot(state, activation, entity);
  return true;
}

bool qa_bot_trigger_multiple_activate_goal_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                                  int64_t bsp_entity, qa_bot_activate_goal_bots_a *activation) {
  (void)state; /* TS _state: the source signature carries it unused. */
  activation->src->shoot = 0;
  activation->src->target = qa_bot_fv3(qa_math_vec3(0, 0, 0));
  char model[BSP_MODEL_SIZE];
  if (!bsp_value(ctx, bsp_entity, "model", model, sizeof model) || model[0] == 0)
    return false;
  int64_t model_index = qa_game_atoi(model + 1);
  if (model_index == 0)
    return false;
  qa_vec3 mins = qa_math_vec3(0, 0, 0), maxs = qa_math_vec3(0, 0, 0);
  int64_t entity = qa_bot_model_mins_maxs_bots_a(ctx, model_index, 0, CONTENTS_TRIGGER, &mins, &maxs);
  qa_vec3 origin = qa_scale3(qa_add3(mins, maxs), 0.5);
  qa_vec3 start = qa_math_vec3(origin.x, origin.y, origin.z + 24);
  qa_bot_area_crossing crossings[MAX_TRACE_CROSSINGS];
  size_t crossing_count = ai_nav_trace_areas(ctx, start, qa_math_vec3(start.x, start.y, start.z - 100),
                                             MAX_TRACE_CROSSINGS, crossings);
  for (size_t index = 0; index < crossing_count; index++) {
    if (!reachable(ctx, crossings[index].area))
      continue;
    activation->src->goal.origin = qa_bot_fv3(origin);
    activation->src->goal.area = (int32_t)crossings[index].area;
    activation->src->goal.mins = qa_bot_fv3(qa_sub3(mins, origin));
    activation->src->goal.maxs = qa_bot_fv3(qa_sub3(maxs, origin));
    activation_entity(&activation->src->goal, entity);
    return true;
  }
  return false;
}

void qa_bot_enable_activate_goal_areas_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_activate_goal_bots_a *activation,
                                              bool enable) {
  if ((bool)activation->src->areas_disabled == !enable)
    return;
  for (int64_t index = 0; index < activation->src->num_areas; index++) {
    if (index >= QA_MAX_ACTIVATEAREAS)
      qa_range_error("Bot navigation source index %lld outside %d cells", (long long)index, QA_MAX_ACTIVATEAREAS);
    ai_nav_set_area_enabled(ctx, activation->src->areas[index], enable);
  }
  activation->src->areas_disabled = !enable;
}

bool qa_bot_pop_from_activate_goal_stack_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  qa_bot_activate_goal_bots_a *activation = state->activate_stack;
  if (activation == NULL)
    return false;
  qa_bot_enable_activate_goal_areas_bots_a(ctx, activation, true);
  activation->src->inuse = 0;
  activation->src->just_used_time = (float)ctx->time;
  state->activate_stack = activation->next;
  return true;
}

bool qa_bot_push_onto_activate_goal_stack_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                                 const qa_bot_activate_goal_bots_a *activation) {
  qa_bot_activate_goal_bots_a *best = NULL;
  double best_time = AI_F(ctx->time + 9999);
  for (size_t index = 0; index < QA_MAX_ACTIVATESTACK; index++) {
    qa_bot_activate_goal_bots_a *candidate = &state->activate_goal_heap[index];
    if (!candidate->src->inuse && candidate->src->just_used_time < best_time) {
      best_time = candidate->src->just_used_time;
      best = candidate;
    }
  }
  if (best == NULL)
    return false;
  qa_bot_activate_goal_copy_from(best, activation);
  best->src->inuse = 1;
  best->next = state->activate_stack;
  state->activate_stack = best;
  return true;
}

void qa_bot_clear_activate_goal_stack_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  while (state->activate_stack != NULL)
    qa_bot_pop_from_activate_goal_stack_bots_a(ctx, state);
}

bool qa_bot_is_going_to_activate_entity_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                               int64_t entity) {
  for (qa_bot_activate_goal_bots_a *activation = state->activate_stack; activation != NULL;
       activation = activation->next) {
    if (activation->src->time < ctx->time)
      continue;
    if (activation->src->goal.entity == entity)
      return true;
  }
  for (size_t index = 0; index < QA_MAX_ACTIVATESTACK; index++) {
    const qa_bot_activate_goal_bots_a *activation = &state->activate_goal_heap[index];
    if (!activation->src->inuse && activation->src->goal.entity == entity && activation->src->just_used_time > AI_F(ctx->time - 2))
      return true;
  }
  return false;
}

/* infoValueForKey(cvars.infoString(ServerInfo, 1024), "mapname").slice(0, 127). */
static void server_map_name(qa_game_ai_context_bots_a *ctx, char *out, size_t size) {
  char *info = qa_cvar_registry_info_string(ctx->game->options.cvars, QA_CVAR_FLAG_SERVER_INFO, 1024);
  if (info == NULL)
    qa_range_error("Out of memory reading the server info string");
  char *value = qa_info_value_for_key(info, "mapname");
  if (value == NULL)
    qa_range_error("Out of memory reading mapname");
  snprintf(out, size, "%s", value);
  free(value);
  free(info);
}

static bool below_crusher(qa_vec3 origin) {
  return origin.x > 700 && origin.x < 964 && origin.y > 204 && origin.y < 468 && origin.z < 672;
}

void qa_bot_map_scripts_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  char map_name[128];
  server_map_name(ctx, map_name, sizeof map_name);
  if (same_name(map_name, "q3tourney6")) {
    state->src->tfl &= ~QA_TFL_FUNCBOB;
    if (below_crusher(qa_bot_v3(state->src->origin)))
      return;
    bool shoot_button = false;
    int64_t max_clients = ai_deathmatch(ctx)->maxclients;
    for (int64_t client = 0; client < max_clients && client < QA_Q3_MAX_CLIENTS; client++) {
      if (client == state->src->client)
        continue;
      qa_aas_entity_info_bots_a entity = qa_bot_entity_info_bots_a(ctx, client);
      if (!entity.valid || qa_entity_is_dead_bots_a(ctx, &entity) || entity.number == state->src->entity_num)
        continue;
      if (below_crusher(entity.origin)) {
        if (qa_bot_same_team_bots_a(ctx, state, client)) {
          shoot_button = false;
          break;
        }
        shoot_button = true;
      }
    }
    if (shoot_button) {
      state->src->flags |= QA_BOT_FLAG_IDEALVIEWSET;
      state->src->ideal_viewangles = qa_bot_fv3(qa_vector_to_angles(qa_sub3(qa_math_vec3(304, 352, 920), qa_bot_v3(state->src->eye))));
      double accuracy = ai_character_bounded(ctx, state->src->character, QA_BCHAR_AIM_ACCURACY);
      double pitch = qa_qvm_angle_mod(
          AI_F(state->src->ideal_viewangles.x + AI_F(AI_F(8 * ai_crandom(ctx)) * AI_F(1 - accuracy))));
      double yaw = qa_qvm_angle_mod(
          AI_F(state->src->ideal_viewangles.y + AI_F(AI_F(8 * ai_crandom(ctx)) * AI_F(1 - accuracy))));
      state->src->ideal_viewangles = qa_bot_fv3(qa_math_vec3(pitch, yaw, state->src->ideal_viewangles.z));
      /* TS inFieldOfVision normalizes the live idealViewangles cell. */
      qa_vec3 ideal = qa_bot_v3(state->src->ideal_viewangles);
      bool in_view = qa_in_field_of_vision_bots_a(qa_bot_v3(state->src->viewangles), 20, &ideal);
      state->src->ideal_viewangles = qa_bot_fv3(ideal);
      if (in_view)
        qa_bot_action_attack(ai_actions(ctx), state->src->client);
    }
  } else if (same_name(map_name, "mpq3tourney6")) {
    state->src->tfl &= ~QA_TFL_FUNCBOB;
  }
}

static void bot_developer_error(qa_game_ai_context_bots_a *ctx, const char *prefix, const char *value,
                                const char *suffix) {
  if (ai_cvar_integer(ctx, "bot_developer") == 0)
    return;
  char *line = ai_concat3(prefix, value, suffix);
  error_text(ctx, line);
  free(line);
}

int64_t qa_bot_get_activate_goal_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, int64_t entity_num,
                                        qa_bot_activate_goal_bots_a *activation) {
  qa_bot_activate_goal_clear(activation);
  qa_aas_entity_info_bots_a entity = qa_bot_entity_info_bots_a(ctx, entity_num);
  qa_aas_bsp_entities *bsp = ai_bsp(ctx);
  char model[BSP_LONG_SIZE];
  snprintf(model, sizeof model, "*%lld", (long long)entity.model_index);
  int64_t entity_index = qa_aas_bsp_entities_next_entity(bsp, 0);
  for (; entity_index != 0; entity_index = qa_aas_bsp_entities_next_entity(bsp, entity_index))
    if (bsp_value_equals(ctx, entity_index, "model", model))
      break;
  if (entity_index == 0) {
    char *line = ai_concat3("BotGetActivateGoal: no entity found with model ", model, "\n");
    error_text(ctx, line);
    free(line);
    return 0;
  }
  char classname[BSP_MODEL_SIZE];
  bool has_classname = bsp_value(ctx, entity_index, "classname", classname, sizeof classname);
  if (has_classname && strcmp(classname, "func_door") == 0) {
    qa_aas_bsp_number_result health = qa_aas_bsp_entities_float(bsp, entity_index, "health");
    if (health.found && health.value != 0) {
      qa_bot_func_door_activate_goal_bots_a(ctx, state, entity_index, activation);
      return entity_index;
    }
    if (((int64_t)qa_aas_bsp_entities_int(bsp, entity_index, "spawnflags").value & 1) != 0)
      return 0;
    qa_vec3 origin = qa_aas_bsp_entities_vector(bsp, entity_index, "origin").value;
    if (!same_vector(origin, entity.origin))
      return 0;
    if (!bsp_value(ctx, entity_index, "model", model, sizeof model))
      model[0] = 0;
    if (model[0] != 0) {
      int64_t model_index = qa_game_atoi(model + 1);
      if (model_index != 0) {
        qa_vec3 mins = qa_math_vec3(0, 0, 0), maxs = qa_math_vec3(0, 0, 0);
        qa_bot_model_mins_maxs_bots_a(ctx, model_index, QA_Q3_BASE_ET_MOVER, 0, &mins, &maxs);
        int64_t areas[QA_MAX_ACTIVATEAREAS * 2];
        qa_bounds box = {mins, maxs};
        size_t area_count = ai_nav_bbox_areas(ctx, box, QA_MAX_ACTIVATEAREAS * 2, areas);
        for (int pass = 0; pass < 2; pass++) {
          bool require_reachability = pass == 0;
          for (size_t index = 0; index < area_count; index++) {
            if (activation->src->num_areas >= QA_MAX_ACTIVATEAREAS)
              break;
            if (reachable(ctx, areas[index]) != require_reachability)
              continue;
            if ((ai_nav_area(ctx, areas[index]).contents & AREA_CONTENTS_MOVER) != 0)
              activation->src->areas[activation->src->num_areas++] = (int32_t)areas[index];
          }
        }
      }
    }
  }
  if (has_classname && strcmp(classname, "func_button") == 0)
    return 0;
  char target_name[BSP_MODEL_SIZE];
  if (!bsp_value(ctx, entity_index, "targetname", target_name, sizeof target_name)) {
    if (ai_cvar_integer(ctx, "bot_developer") != 0) {
      char *line = ai_concat3("BotGetActivateGoal: entity with model \"", model, "\" has no targetname\n");
      error_text(ctx, line);
      free(line);
    }
    return 0;
  }
  char targets[10][BSP_MODEL_SIZE];
  int64_t next_entities[10];
  snprintf(targets[0], sizeof targets[0], "%s", target_name);
  next_entities[0] = qa_aas_bsp_entities_next_entity(bsp, 0);
  for (int depth = 0; depth >= 0 && depth < 10;) {
    const char *wanted = targets[depth];
    int64_t activator = next_entities[depth];
    for (; activator != 0; activator = qa_aas_bsp_entities_next_entity(bsp, activator)) {
      if (bsp_value_equals(ctx, activator, "target", wanted)) {
        next_entities[depth] = qa_aas_bsp_entities_next_entity(bsp, activator);
        break;
      }
    }
    if (activator == 0) {
      bot_developer_error(ctx, "BotGetActivateGoal: no entity with target \"", wanted, "\"\n");
      depth--;
      continue;
    }
    char activator_class[BSP_MODEL_SIZE];
    if (!bsp_value(ctx, activator, "classname", activator_class, sizeof activator_class)) {
      bot_developer_error(ctx, "BotGetActivateGoal: entity with target \"", wanted, "\" has no classname\n");
      continue;
    }
    bool is_button = strcmp(activator_class, "func_button") == 0;
    if (is_button || strcmp(activator_class, "trigger_multiple") == 0) {
      bool success = is_button ? qa_bot_func_button_activate_goal_bots_a(ctx, state, activator, activation)
                               : qa_bot_trigger_multiple_activate_goal_bots_a(ctx, state, activator, activation);
      if (!success)
        continue;
      const qa_bot_activate_goal_bots_a *current = state->activate_stack;
      if (current != NULL && current->src->inuse && current->src->goal.entity == activation->src->goal.entity &&
          current->src->time > ctx->time && current->src->start_time < AI_F(ctx->time - 2))
        continue;
      if (reachable(ctx, state->src->area_num)) {
        qa_bot_enable_activate_goal_areas_bots_a(ctx, activation, false);
        double time = travel_time(ctx, state, activation->src->goal.area);
        if (time == 0)
          continue;
        activation->src->time = (float)(AI_F(ctx->time + AI_F(AI_F(time) * AI_F(0.01))) + 5);
      }
      return activator;
    }
    if (strcmp(activator_class, "func_timer") == 0)
      continue;
    if (strcmp(activator_class, "target_relay") == 0 || strcmp(activator_class, "target_delay") == 0) {
      char target[BSP_MODEL_SIZE];
      if (bsp_value(ctx, activator, "targetname", target, sizeof target)) {
        /* The source writes one row past its ten-level arrays here and then leaves the
         * loop; the TS port stops without the overrun. */
        if (depth + 1 >= 10)
          break;
        depth++;
        snprintf(targets[depth], sizeof targets[depth], "%s", target);
        next_entities[depth] = qa_aas_bsp_entities_next_entity(bsp, 0);
      }
    }
  }
  int64_t obstacle_debug = 0;
  if (ai_registry_find_integer(ctx, "com_botObstacleDebug", &obstacle_debug) && obstacle_debug != 0) {
    char *line = ai_concat3("BotGetActivateGoal: no valid activator for entity with target \"", target_name, "\"\n");
    error_text(ctx, line);
    free(line);
  }
  return 0;
}

bool qa_bot_go_for_activate_goal_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                        qa_bot_activate_goal_bots_a *activation) {
  activation->src->inuse = 1;
  if (activation->src->time == 0)
    activation->src->time = (float)(ctx->time + 10);
  activation->src->start_time = (float)ctx->time;
  activation->src->origin = qa_bot_fv3(qa_bot_entity_info_bots_a(ctx, activation->src->goal.entity).origin);
  if (qa_bot_push_onto_activate_goal_stack_bots_a(ctx, state, activation)) {
    qa_ai_enter_seek_activate_entity_bots_a(ctx, state, "BotGoForActivateGoal");
    return true;
  }
  qa_bot_enable_activate_goal_areas_bots_a(ctx, activation, true);
  return false;
}

void qa_bot_print_activate_goal_info_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                            const qa_bot_activate_goal_bots_a *activation, int64_t bsp_entity) {
  char name[64];
  qa_client_name_bots_a(ctx, state->src->client, 36, name);
  char classname[BSP_MODEL_SIZE];
  if (!bsp_value(ctx, bsp_entity, "classname", classname, sizeof classname))
    classname[0] = 0;
  qa_vec3 origin = qa_bot_v3(activation->src->goal.origin);
  const char *format = activation->src->shoot ? "%s: I have to shoot at a %s from %1.1f %1.1f %1.1f in area %d\n"
                                         : "%s: I have to activate a %s at %1.1f %1.1f %1.1f in area %d\n";
  qa_q3_game_format_arg args[6] = {
      qa_q3_game_format_string(name),     qa_q3_game_format_string(classname),
      qa_q3_game_format_number(origin.x), qa_q3_game_format_number(origin.y),
      qa_q3_game_format_number(origin.z), qa_q3_game_format_number((double)activation->src->goal.area)};
  char *text = qa_q3_game_format(format, args, 6, 128);
  if (text == NULL)
    qa_range_error("Out of memory formatting BotPrintActivateGoalInfo");
  qa_bot_action_say(ai_actions(ctx), state->src->client, text);
  free(text);
}

void qa_bot_random_move_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                               qa_bot_move_result *result) {
  qa_vec3 direction = qa_qvm_angle_vectors(qa_math_vec3(0, ai_random(ctx) * 360, 0)).forward;
  ai_nav_move_in_direction(ctx, state->src->ms, direction, 400, QA_BOT_MOVE_WALK);
  result->failure = false;
  result->move_direction = direction;
}

void qa_bot_ai_blocked_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, qa_bot_move_result *result,
                              bool activate) {
  if (!result->blocked) {
    state->src->not_blocked_time = (float)ctx->time;
    return;
  }
  if (result->type == QA_BOT_MRT_INSOLIDAREA) {
    qa_bot_random_move_bots_a(ctx, state, result);
    return;
  }
  qa_aas_entity_info_bots_a entity = qa_bot_entity_info_bots_a(ctx, result->block_entity);
  if (activate && entity.model_index > 0 && entity.model_index <= ai_deathmatch(ctx)->max_bsp_model_index) {
    qa_bot_activate_goal_bots_a activation;
    qa_bot_activate_goal_init(&activation);
    int64_t bsp_entity = qa_bot_get_activate_goal_bots_a(ctx, state, entity.number, &activation);
    if (bsp_entity != 0) {
      if (state->activate_stack != NULL && !state->activate_stack->src->inuse)
        state->activate_stack = NULL;
      if (!qa_bot_is_going_to_activate_entity_bots_a(ctx, state, activation.src->goal.entity))
        qa_bot_go_for_activate_goal_bots_a(ctx, state, &activation);
      if ((result->flags & QA_BOT_MRF_ONTOPOFOBSTACLE) == 0 && reachable(ctx, state->src->area_num))
        return;
    } else {
      qa_bot_enable_activate_goal_areas_bots_a(ctx, &activation, true);
    }
  }
  qa_vec3 horizontal = qa_math_vec3(result->move_direction.x, result->move_direction.y, 0);
  qa_vec3 direction = qa_normalize3(horizontal);
  if (qa_length3(horizontal) < AI_F(0.1))
    direction = qa_qvm_angle_vectors(qa_math_vec3(0, 360 * ai_random(ctx), 0)).forward;
  qa_vec3 sideward = qa_cross3(direction, qa_math_vec3(0, 0, 1));
  if ((state->src->flags & QA_BOT_FLAG_AVOIDRIGHT) != 0)
    sideward = qa_scale3(sideward, -1);
  if (!ai_nav_move_in_direction(ctx, state->src->ms, sideward, 400, QA_BOT_MOVE_WALK)) {
    state->src->flags ^= QA_BOT_FLAG_AVOIDRIGHT;
    sideward = qa_sub3(sideward, direction);
    ai_nav_move_in_direction(ctx, state->src->ms, sideward, 400, QA_BOT_MOVE_WALK);
  }
  if (state->src->not_blocked_time < AI_F(ctx->time - AI_F(0.4))) {
    if (state->ai_node == QA_AI_NODE_SEEK_NBG)
      state->src->nbg_time = 0;
    else if (state->ai_node == QA_AI_NODE_SEEK_LTG)
      state->src->ltg_time = 0;
  }
}

bool qa_bot_ai_predict_obstacles_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                        const qa_bot_goal *goal) {
  if (ai_cvar_integer(ctx, "bot_predictobstacles") == 0)
    return false;
  if (state->src->predict_obstacles_goal_area_num == goal->area && state->src->predict_obstacles_time > AI_F(ctx->time - 6))
    return false;
  state->src->predict_obstacles_goal_area_num = (int32_t)goal->area;
  state->src->predict_obstacles_time = (float)ctx->time;
  qa_predicted_route route = ai_nav_predict_route(ctx, state->src->area_num, qa_bot_v3(state->src->origin), goal->area, state->src->tfl, 100,
                                                  1000, QA_ROUTE_STOP_USE_TRAVEL_TYPE | QA_ROUTE_STOP_ENTER_CONTENTS,
                                                  AREA_CONTENTS_MOVER, QA_TFL_BRIDGE, 0);
  if ((route.stop_event & QA_ROUTE_STOP_ENTER_CONTENTS) != 0 && (route.end_contents & AREA_CONTENTS_MOVER) != 0) {
    /* JS bitwise: the int32 contents word shifts arithmetically. */
    int32_t model = ((int32_t)route.end_contents & (int32_t)0xff000000) >> 24;
    if (model != 0) {
      int64_t entity = qa_bot_model_mins_maxs_bots_a(ctx, model, QA_Q3_BASE_ET_MOVER, 0, NULL, NULL);
      if (entity != 0) {
        qa_bot_activate_goal_bots_a activation;
        qa_bot_activate_goal_init(&activation);
        if (qa_bot_get_activate_goal_bots_a(ctx, state, entity, &activation) != 0) {
          if (state->activate_stack != NULL && !state->activate_stack->src->inuse)
            state->activate_stack = NULL;
          if (!qa_bot_is_going_to_activate_entity_bots_a(ctx, state, activation.src->goal.entity)) {
            qa_bot_go_for_activate_goal_bots_a(ctx, state, &activation);
            return true;
          }
          qa_bot_enable_activate_goal_areas_bots_a(ctx, &activation, true);
        }
      }
    }
  }
  return false;
}

qa_bot_goal qa_bot_alternate_route_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                          qa_bot_goal *goal) {
  if (state->src->alt_route_goal.area != 0) {
    if (state->src->reached_alt_route_goal_time != 0)
      return *goal;
    double time = travel_time(ctx, state, state->src->alt_route_goal.area);
    if (time != 0 && time < 20)
      state->src->reached_alt_route_goal_time = (float)ctx->time;
    *goal = qa_bot_goal_state_get(&state->src->alt_route_goal);
    return *goal;
  }
  return *goal;
}

bool qa_bot_get_alternate_route_goal_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, int64_t base) {
  qa_game_ai_deathmatch_state_bots_a *globals = ai_deathmatch(ctx);
  const qa_alternative_goal *goals =
      base == QA_Q3_BASE_TEAM_RED ? globals->red_alternate_goals : globals->blue_alternate_goals;
  size_t count = base == QA_Q3_BASE_TEAM_RED ? globals->red_alternate_goal_count : globals->blue_alternate_goal_count;
  if (count == 0)
    return false;
  size_t index = (size_t)trunc(AI_F(ai_random(ctx) * (double)count));
  if (index >= count)
    index = count - 1;
  const qa_alternative_goal *alternate = &goals[index];
  qa_bot_goal_state_bots_a *goal = &state->src->alt_route_goal;
  goal->area = (int32_t)alternate->area;
  goal->origin = qa_bot_fv3(alternate->origin);
  goal->mins = qa_bot_fv3(qa_math_vec3(-8, -8, -8));
  goal->maxs = qa_bot_fv3(qa_math_vec3(8, 8, 8));
  goal->entity = 0;
  goal->item_info = 0;
  goal->number = 0;
  goal->flags = 0;
  state->src->reached_alt_route_goal_time = 0;
  return true;
}

static void level_goal(qa_game_ai_context_bots_a *ctx, const char *name, qa_bot_goal *goal, const char *missing) {
  /* TS passes the goal cell as the blank/prior argument. */
  qa_bot_goal item;
  if (!qa_bot_goal_get_level_item_goal(ai_goals(ctx), -1, qa_goal_name_text(name), goal, &item))
    warning(ctx, missing);
  else
    ai_goal_copy(goal, &item);
}

static size_t alternate_goals(qa_game_ai_context_bots_a *ctx, const qa_bot_goal *neutral, const qa_bot_goal *target,
                              qa_alternative_goal *out) {
  qa_alternative_route_query query = {0};
  query.start = neutral->origin;
  query.start_area = neutral->area;
  query.goal = target->origin;
  query.goal_area = target->area;
  query.travel_flags = QA_TFL_DEFAULT;
  query.maximum_goals = QA_MAX_ALT_ROUTE_GOALS_BOTS_A;
  query.type = QA_ALT_ROUTE_CLUSTER_PORTALS | QA_ALT_ROUTE_VIEW_PORTALS;
  return ai_nav_alternative_route_goals(ctx, &query, out, QA_MAX_ALT_ROUTE_GOALS_BOTS_A);
}

void qa_bot_setup_alternative_route_goals_bots_a(qa_game_ai_context_bots_a *ctx) {
  qa_game_ai_deathmatch_state_bots_a *globals = ai_deathmatch(ctx);
  if (globals->alternate_routes_setup)
    return;
  if (ai_is_missionpack(ctx)) {
    const qa_bot_goal *neutral = NULL, *red = &globals->ctf_red_flag, *blue = &globals->ctf_blue_flag;
    int64_t game_type = globals->gametype;
    if (game_type == QA_Q3_BASE_GT_CTF) {
      level_goal(ctx, "Neutral Flag", &globals->ctf_neutral_flag, "no alt routes without Neutral Flag\n");
      if (globals->ctf_neutral_flag.area != 0)
        neutral = &globals->ctf_neutral_flag;
    } else if (game_type == QA_Q3_BASE_GT_1FCTF) {
      neutral = &globals->ctf_neutral_flag;
    } else if (game_type == QA_Q3_BASE_GT_OBELISK || game_type == QA_Q3_BASE_GT_HARVESTER) {
      if (game_type == QA_Q3_BASE_GT_OBELISK)
        level_goal(ctx, "Neutral Obelisk", &globals->neutral_obelisk, "Harvester without neutral obelisk\n");
      neutral = &globals->neutral_obelisk;
      red = &globals->red_obelisk;
      blue = &globals->blue_obelisk;
    }
    if (neutral != NULL) {
      globals->red_alternate_goal_count = alternate_goals(ctx, neutral, red, globals->red_alternate_goals);
      globals->blue_alternate_goal_count = alternate_goals(ctx, neutral, blue, globals->blue_alternate_goals);
    }
  }
  globals->alternate_routes_setup = true;
}

void qa_bot_set_entity_num_for_goal_with_model_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_goal *goal,
                                                      int64_t entity_type, const char *model_name) {
  int64_t model = ai_game_model_index(ctx, model_name);
  int64_t count = ai_entity_count(ctx);
  for (int64_t index = 0; index < count; index++) {
    qa_bot_observed_entity_bots_a entity = ai_entity(ctx, index);
    if (!entity.present || (entity_type != 0 && entity.state.e_type != entity_type) ||
        entity.state.modelindex != model)
      continue;
    qa_vec3 direction = qa_sub3(goal->origin, entity.state.origin);
    if (qa_dot3(direction, direction) < 100) {
      goal->entity = index;
      return;
    }
  }
}

void qa_bot_set_entity_num_for_goal_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_goal *goal, const char *classname) {
  int64_t count = ai_entity_count(ctx);
  for (int64_t index = 0; index < count; index++) {
    qa_bot_observed_entity_bots_a entity = ai_entity(ctx, index);
    if (!entity.present)
      continue;
    /* Faithful to the TS: an entity whose classname matches is skipped. */
    if (entity.classname != NULL && same_name(entity.classname, classname))
      continue;
    qa_vec3 direction = qa_sub3(goal->origin, entity.state.origin);
    if (qa_dot3(direction, direction) < 100) {
      goal->entity = index;
      return;
    }
  }
}

bool qa_bot_goal_for_bsp_entity_bots_a(qa_game_ai_context_bots_a *ctx, const char *classname, qa_bot_goal *goal) {
  memset(goal, 0, sizeof *goal);
  qa_aas_bsp_entities *bsp = ai_bsp(ctx);
  for (int64_t entity = qa_aas_bsp_entities_next_entity(bsp, 0); entity != 0;
       entity = qa_aas_bsp_entities_next_entity(bsp, entity)) {
    char value[BSP_LONG_SIZE];
    if (!bsp_value(ctx, entity, "classname", value, sizeof value) || strcmp(value, classname) != 0)
      continue;
    qa_aas_bsp_vector_result origin = qa_aas_bsp_entities_vector(bsp, entity, "origin");
    if (!origin.found)
      return false;
    goal->origin = ai_fv(origin.value);
    qa_vec3 start = qa_math_vec3(origin.value.x, origin.value.y, origin.value.z - 32);
    qa_vec3 end = qa_math_vec3(origin.value.x, origin.value.y, origin.value.z + 32);
    qa_bot_area_crossing crossings[MAX_TRACE_CROSSINGS];
    size_t count = ai_nav_trace_areas(ctx, start, end, MAX_TRACE_CROSSINGS, crossings);
    if (count == 0)
      return false;
    goal->area = crossings[0].area;
    return true;
  }
  return false;
}

static void level_goal_mode(qa_game_ai_context_bots_a *ctx, const char *name, qa_bot_goal *goal, const char *mode,
                            const char *what) {
  char *missing = ai_concat3(mode, what, "\n");
  level_goal(ctx, name, goal, missing);
  free(missing);
}

void qa_bot_setup_deathmatch_ai_bots_a(qa_game_ai_context_bots_a *ctx) {
  qa_game_ai_deathmatch_state_bots_a *globals = ai_deathmatch(ctx);
  int64_t game_type = 0, max_clients = 0;
  /* Cvar_VariableIntegerValue returns zero for a name absent from the registry. */
  if (!ai_registry_find_integer(ctx, "g_gametype", &game_type))
    game_type = 0;
  if (!ai_registry_find_integer(ctx, "sv_maxclients", &max_clients))
    max_clients = 0;
  globals->gametype = game_type;
  globals->maxclients = max_clients;
  ai_register_cvar(ctx, "bot_rocketjump", "1");
  ai_register_cvar(ctx, "bot_grapple", "0");
  ai_register_cvar(ctx, "bot_fastchat", "0");
  ai_register_cvar(ctx, "bot_nochat", "0");
  ai_register_cvar(ctx, "bot_testrchat", "0");
  ai_register_cvar(ctx, "bot_challenge", "0");
  ai_register_cvar(ctx, "bot_predictobstacles", "1");
  ai_register_cvar(ctx, "g_spSkill", "2");
  if (globals->gametype == QA_Q3_BASE_GT_CTF) {
    level_goal(ctx, "Red Flag", &globals->ctf_red_flag, "CTF without Red Flag\n");
    level_goal(ctx, "Blue Flag", &globals->ctf_blue_flag, "CTF without Blue Flag\n");
  } else if (ai_is_missionpack(ctx)) {
    if (globals->gametype == QA_Q3_BASE_GT_1FCTF) {
      level_goal(ctx, "Neutral Flag", &globals->ctf_neutral_flag, "One Flag CTF without Neutral Flag\n");
      level_goal(ctx, "Red Flag", &globals->ctf_red_flag, "CTF without Red Flag\n");
      level_goal(ctx, "Blue Flag", &globals->ctf_blue_flag, "CTF without Blue Flag\n");
    } else if (globals->gametype == QA_Q3_BASE_GT_OBELISK || globals->gametype == QA_Q3_BASE_GT_HARVESTER) {
      const char *mode = globals->gametype == QA_Q3_BASE_GT_OBELISK ? "Obelisk" : "Harvester";
      level_goal_mode(ctx, "Red Obelisk", &globals->red_obelisk, mode, " without red obelisk");
      qa_bot_set_entity_num_for_goal_bots_a(ctx, &globals->red_obelisk, "team_redobelisk");
      level_goal_mode(ctx, "Blue Obelisk", &globals->blue_obelisk, mode, " without blue obelisk");
      qa_bot_set_entity_num_for_goal_bots_a(ctx, &globals->blue_obelisk, "team_blueobelisk");
      if (globals->gametype == QA_Q3_BASE_GT_HARVESTER) {
        level_goal(ctx, "Neutral Obelisk", &globals->neutral_obelisk, "Harvester without neutral obelisk\n");
        qa_bot_set_entity_num_for_goal_bots_a(ctx, &globals->neutral_obelisk, "team_neutralobelisk");
      }
    }
  }
  globals->max_bsp_model_index = 0;
  qa_aas_bsp_entities *bsp = ai_bsp(ctx);
  for (int64_t entity = qa_aas_bsp_entities_next_entity(bsp, 0); entity != 0;
       entity = qa_aas_bsp_entities_next_entity(bsp, entity)) {
    char model[BSP_MODEL_SIZE];
    if (!bsp_value(ctx, entity, "model", model, sizeof model) || model[0] != '*')
      continue;
    int64_t index = qa_game_atoi(model + 1);
    if (index > globals->max_bsp_model_index)
      globals->max_bsp_model_index = index;
  }
  qa_bot_init_waypoints_bots_a(ctx);
}

void qa_bot_shutdown_deathmatch_ai_bots_a(qa_game_ai_context_bots_a *ctx) {
  ai_deathmatch(ctx)->alternate_routes_setup = false;
}
