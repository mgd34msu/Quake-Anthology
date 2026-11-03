/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/ai_decision.h"
#include "ai_access_internal.h"
#include "qa/bots/behavior/library/actions.h"
#include "qa/bots/behavior/library/chat.h"
#include "qa/bots/behavior/orders.h"
#include "qa/bots/behavior/q3/ai_chat.h"
#include "qa/bots/behavior/q3/ai_combat.h"
#include "qa/bots/behavior/q3/ai_navigation.h"
#include "qa/bots/behavior/q3/ai_orders.h"
#include "qa/bots/behavior/q3/ai_team.h"
#include "qa/content/q3/base/game/format.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"

enum {
  AIR_GOAL = 128,
  SOLID = 1,
  PLAYERCLIP = 0x10000,
  LIQUID = 8 | 16 | 32,
  MASK_SHOT = SOLID | 0x2000000 | 0x4000000,
  CHAT_TEAM = 1,
  CHAT_TELL = 2,
  MOVEMENT_VIEW = QA_BOT_MRF_MOVEMENTVIEWSET | QA_BOT_MRF_MOVEMENTVIEW | QA_BOT_MRF_SWIMVIEW,
  MOVEMENT_AIM = QA_BOT_MRF_MOVEMENTVIEW | QA_BOT_MRF_MOVEMENTWEAPON,
  NAME_SIZE = 64, /* qa_client_name_bots_a: 36 bytes, at least 22 for its error text */
  GOAL_NAME_SIZE = 144
};

static double inventory(qa_bot_state_bots_a *state, int index) { return qa_bot_inventory_value_bots_a(state, index); }

static int64_t area_reachability(qa_game_ai_context_bots_a *ctx, int64_t area) {
  return ai_nav_area(ctx, area).reachable_area_count;
}

static double default_travel_time(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, int64_t goal_area) {
  return ai_nav_travel_time(ctx, state->src->area_num, qa_bot_v3(state->src->origin), goal_area, QA_TFL_DEFAULT);
}

static bool swimming(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  return ai_nav_swimming(ctx, qa_bot_v3(state->src->origin));
}

static void set_ideal(qa_bot_state_bots_a *state, qa_vec3 angles) { state->src->ideal_viewangles = qa_bot_fv3(angles); }

static void half_roll(qa_bot_state_bots_a *state) {
  state->src->ideal_viewangles.z = (float)((double)state->src->ideal_viewangles.z * 0.5);
}

static void look_at(qa_bot_state_bots_a *state, qa_vec3 target) {
  set_ideal(state, qa_vector_to_angles(qa_sub3(target, qa_bot_v3(state->src->origin))));
}

static void look_at_roam(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  look_at(state, qa_bot_roam_goal_bots_a(ctx, state));
  half_roll(state);
}

static bool movement_view_target(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, const qa_bot_goal *goal,
                                 qa_vec3 *target) {
  *target = qa_math_vec3(0, 0, 0);
  return ai_nav_movement_view_target(ctx, state->src->ms, goal, state->src->tfl, 300, target);
}

static void set_travel_flags(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, bool rocket_jump) {
  state->src->tfl = QA_TFL_DEFAULT;
  if (ai_cvar_integer(ctx, "bot_grapple") != 0)
    state->src->tfl |= QA_TFL_GRAPPLEHOOK;
  if (qa_bot_in_lava_or_slime_bots_a(ctx, state))
    state->src->tfl |= QA_TFL_LAVA | QA_TFL_SLIME;
  if (rocket_jump && qa_bot_can_and_wants_to_rocket_jump_bots_a(ctx, state))
    state->src->tfl |= QA_TFL_ROCKETJUMP;
}

void qa_bot_reset_node_switches_bots_a(qa_game_ai_context_bots_a *ctx) {
  for (size_t index = 0; index < ctx->node_switch_count; index++)
    free(ctx->node_switches[index]);
  ctx->node_switch_count = 0;
}

void qa_bot_record_node_switch_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, const char *node,
                                      const char *detail, const char *reason) {
  char name[NAME_SIZE];
  qa_client_name_bots_a(ctx, state->src->client, 36, name);
  qa_q3_game_format_arg args[5] = {qa_q3_game_format_string(name), qa_q3_game_format_number(ctx->time),
                                   qa_q3_game_format_string(node), qa_q3_game_format_string(detail),
                                   qa_q3_game_format_string(reason)};
  char *line = qa_q3_game_format("%s at %2.1f entered %s: %s from %s\n", args, 5, 144);
  qa_bot_push_node_switch_bots_a(ctx, line);
}

void qa_bot_dump_node_switches_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  char name[NAME_SIZE];
  qa_client_name_bots_a(ctx, state->src->client, 36, name);
  qa_q3_game_format_arg args[3] = {qa_q3_game_format_string(name), qa_q3_game_format_number(ctx->time),
                                   qa_q3_game_format_number(QA_MAX_NODESWITCHES)};
  char *line = qa_q3_game_format("%s at %1.1f switched more than %d AI nodes\n", args, 3, 32000);
  if (line == NULL)
    qa_range_error("Out of memory dumping AI node switches");
  ai_print(ctx, line);
  free(line);
  for (size_t index = 0; index < ctx->node_switch_count; index++)
    ai_print(ctx, ctx->node_switches[index]);
  ai_print(ctx, "^1Fatal: ");
}

bool qa_bot_get_air_goal_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, qa_bot_goal *goal) {
  qa_bounds bounds = {qa_math_vec3(-15, -15, -2), qa_math_vec3(15, 15, 2)};
  qa_vec3 origin = qa_bot_v3(state->src->origin);
  qa_vec3 above = qa_math_vec3(origin.x, origin.y, origin.z + 1000);
  qa_q3_base_server_trace_result ceiling =
      qa_bot_ai_trace_bots_a(ctx, origin, above, state->src->entity_num, SOLID | PLAYERCLIP, &bounds);
  qa_q3_base_server_trace_result surface =
      qa_bot_ai_trace_bots_a(ctx, ceiling.end, origin, state->src->entity_num, LIQUID, &bounds);
  if (surface.fraction > 0) {
    int64_t area = qa_bot_point_area_num_bots_a(ctx, surface.end);
    if (area != 0) {
      goal->origin = qa_math_vec3(surface.end.x, surface.end.y, surface.end.z - 2);
      goal->area = area;
      goal->mins = qa_math_vec3(-15, -15, -1);
      goal->maxs = qa_math_vec3(15, 15, 1);
      goal->flags = AIR_GOAL;
      goal->number = 0;
      goal->item_info = 0;
      goal->entity = 0;
      return true;
    }
  }
  return false;
}

bool qa_bot_go_for_air_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, int64_t flags,
                              const qa_bot_goal *long_term_goal, double range) {
  if (state->src->last_air_time < AI_F(ctx->time - 6)) {
    qa_bot_goal goal = {0};
    if (qa_bot_get_air_goal_bots_a(ctx, state, &goal)) {
      qa_bot_goal_push_goal(ai_goals(ctx), state->src->gs, &goal);
      return true;
    }
    while (qa_bot_goal_choose_nbg_item(ai_goals(ctx), state->src->gs, qa_bot_v3(state->src->origin), ai_inventory(state),
                                       flags, long_term_goal, range)) {
      qa_bot_goal nearby;
      if (!qa_bot_goal_get_top_goal(ai_goals(ctx), state->src->gs, &nearby))
        qa_range_error("Selected nearby item has no goal stack entry");
      if ((ai_host_point_contents(ctx, nearby.origin) & LIQUID) == 0)
        return true;
      qa_bot_goal_pop_goal(ai_goals(ctx), state->src->gs);
    }
    qa_bot_goal_reset_avoid_goals(ai_goals(ctx), state->src->gs);
  }
  return false;
}

bool qa_bot_nearby_goal_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, int64_t flags,
                               const qa_bot_goal *long_term_goal, double range) {
  if (qa_bot_go_for_air_bots_a(ctx, state, flags, long_term_goal, range))
    return true;
  if (qa_bot_ctf_carrying_flag_bots_a(ctx, state) != 0 &&
      default_travel_time(ctx, state, state->src->team_goal.area) < 300)
    range = 50;
  return qa_bot_goal_choose_nbg_item(ai_goals(ctx), state->src->gs, qa_bot_v3(state->src->origin), ai_inventory(state),
                                     flags, long_term_goal, range);
}

bool qa_bot_reached_goal_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, const qa_bot_goal *goal) {
  qa_source_goal_status status = qa_bot_goal_source_goal_status(ai_goals(ctx), state->src->client, goal);
  if (status != QA_SOURCE_GOAL_NATIVE)
    return status == QA_SOURCE_GOAL_UNAVAILABLE;
  qa_vec3 origin = qa_bot_v3(state->src->origin);
  if ((goal->flags & QA_GOAL_FLAG_ITEM) != 0) {
    if (qa_touching_goal(origin, *goal)) {
      if ((goal->flags & QA_GOAL_FLAG_DROPPED) == 0)
        qa_bot_goal_set_avoid_goal_time(ai_goals(ctx), state->src->gs, goal->number, -1);
      return true;
    }
    if (qa_bot_goal_item_goal_in_vis_but_not_visible(ai_goals(ctx), state->src->entity_num, qa_bot_v3(state->src->eye),
                                                     qa_bot_v3(state->src->viewangles), goal))
      return true;
    if (state->src->area_num == goal->area && origin.x > AI_F(goal->origin.x + goal->mins.x) &&
        origin.x < AI_F(goal->origin.x + goal->maxs.x) && origin.y > AI_F(goal->origin.y + goal->mins.y) &&
        origin.y < AI_F(goal->origin.y + goal->maxs.y) && !swimming(ctx, state))
      return true;
  } else if ((goal->flags & AIR_GOAL) != 0) {
    if (qa_touching_goal(origin, *goal) || state->src->last_air_time > AI_F(ctx->time - 1))
      return true;
  } else if (qa_touching_goal(origin, *goal)) {
    return true;
  }
  return false;
}

bool qa_bot_get_item_long_term_goal_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, int64_t flags,
                                           qa_bot_goal *goal) {
  qa_bot_goal top;
  if (!qa_bot_goal_get_top_goal(ai_goals(ctx), state->src->gs, &top)) {
    state->src->ltg_time = 0;
  } else {
    ai_goal_copy(goal, &top);
    if (qa_bot_reached_goal_bots_a(ctx, state, goal)) {
      qa_bot_choose_weapon_bots_a(ctx, state);
      state->src->ltg_time = 0;
    }
  }
  if (state->src->ltg_time < ctx->time) {
    qa_bot_goal_pop_goal(ai_goals(ctx), state->src->gs);
    if (qa_bot_goal_choose_ltg_item(ai_goals(ctx), state->src->gs, qa_bot_v3(state->src->origin), ai_inventory(state), flags)) {
      state->src->ltg_time = (float)(ctx->time + 20);
    } else {
      if (ai_debug_build(ctx)) {
        char name[128 + 22];
        qa_client_name_bots_a(ctx, state->src->client, 128, name);
        char *line = ai_concat3(name, ": no valid ltg (probably stuck)\n", "");
        ai_print(ctx, line);
        free(line);
      }
      qa_bot_goal_reset_avoid_goals(ai_goals(ctx), state->src->gs);
      ai_move_reset_avoid_reach(ctx, state->src->ms);
    }
    qa_bot_goal next;
    if (!qa_bot_goal_get_top_goal(ai_goals(ctx), state->src->gs, &next))
      return false;
    ai_goal_copy(goal, &next);
  }
  return true;
}

void qa_ai_enter_intermission_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, const char *reason) {
  qa_bot_record_node_switch_bots_a(ctx, state, "intermission", "", reason);
  ai_reset_state(ctx, state);
  if (qa_bot_chat_end_level_bots_a(ctx, state))
    qa_bot_chat_library_enter_chat(ai_chat(ctx), state->src->cs, 0, state->src->chat_to, false, 0);
  state->ai_node = QA_AI_NODE_INTERMISSION;
}

bool qa_ai_node_intermission_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (!qa_bot_intermission_bots_a(ctx, state)) {
    if (qa_bot_chat_start_level_bots_a(ctx, state))
      state->src->stand_time = (float)(ctx->time + qa_bot_chat_time_bots_a(ctx, state));
    else
      state->src->stand_time = (float)(ctx->time + 2);
    qa_ai_enter_stand_bots_a(ctx, state, "intermission: chat");
  }
  return true;
}

void qa_ai_enter_observer_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, const char *reason) {
  qa_bot_record_node_switch_bots_a(ctx, state, "observer", "", reason);
  ai_reset_state(ctx, state);
  state->ai_node = QA_AI_NODE_OBSERVER;
}

bool qa_ai_node_observer_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (!qa_bot_is_observer_bots_a(ctx, state))
    qa_ai_enter_stand_bots_a(ctx, state, "observer: left observer");
  return true;
}

void qa_ai_enter_stand_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, const char *reason) {
  qa_bot_record_node_switch_bots_a(ctx, state, "stand", "", reason);
  state->src->stand_find_enemy_time = (float)(ctx->time + 1);
  state->ai_node = QA_AI_NODE_STAND;
}

bool qa_ai_node_stand_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (state->src->last_frame_health > inventory(state, QA_INV_HEALTH) && qa_bot_chat_hit_talking_bots_a(ctx, state)) {
    state->src->stand_find_enemy_time = (float)(AI_F(ctx->time + qa_bot_chat_time_bots_a(ctx, state)) + AI_F(0.1));
    state->src->stand_time = (float)(AI_F(ctx->time + qa_bot_chat_time_bots_a(ctx, state)) + AI_F(0.1));
  }
  if (state->src->stand_find_enemy_time < ctx->time) {
    if (qa_bot_find_enemy_bots_a(ctx, state, -1)) {
      qa_ai_enter_battle_fight_bots_a(ctx, state, "stand: found enemy");
      return false;
    }
    state->src->stand_find_enemy_time = (float)(ctx->time + 1);
  }
  qa_bot_action_talk(ai_actions(ctx), state->src->client);
  if (state->src->stand_time < ctx->time) {
    qa_bot_chat_library_enter_chat(ai_chat(ctx), state->src->cs, 0, state->src->chat_to, false, 0);
    qa_ai_enter_seek_ltg_bots_a(ctx, state, "stand: time out");
    return false;
  }
  return true;
}

void qa_ai_enter_respawn_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, const char *reason) {
  qa_bot_record_node_switch_bots_a(ctx, state, "respawn", "", reason);
  ai_move_reset(ctx, state->src->ms);
  qa_bot_goal_reset_goal_state(ai_goals(ctx), state->src->gs);
  qa_bot_goal_reset_avoid_goals(ai_goals(ctx), state->src->gs);
  ai_move_reset_avoid_reach(ctx, state->src->ms);
  if (qa_bot_chat_death_bots_a(ctx, state)) {
    state->src->respawn_time = (float)(ctx->time + qa_bot_chat_time_bots_a(ctx, state));
    state->src->respawn_chat_time = (float)ctx->time;
  } else {
    state->src->respawn_time = (float)(AI_F(ctx->time + 1) + ai_random(ctx));
    state->src->respawn_chat_time = 0;
  }
  state->src->respawn_wait = 0;
  state->ai_node = QA_AI_NODE_RESPAWN;
}

bool qa_ai_node_respawn_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (state->src->respawn_wait) {
    if (!qa_bot_is_dead_bots_a(ctx, state))
      qa_ai_enter_seek_ltg_bots_a(ctx, state, "respawn: respawned");
    else
      qa_bot_action_respawn(ai_actions(ctx), state->src->client);
  } else if (state->src->respawn_time < ctx->time) {
    state->src->respawn_wait = 1;
    qa_bot_action_respawn(ai_actions(ctx), state->src->client);
    if (state->src->respawn_chat_time != 0) {
      qa_bot_chat_library_enter_chat(ai_chat(ctx), state->src->cs, 0, state->src->chat_to, false, 0);
      state->src->enemy = -1;
    }
  }
  if (state->src->respawn_chat_time != 0 && state->src->respawn_chat_time < AI_F(ctx->time - 0.5))
    qa_bot_action_talk(ai_actions(ctx), state->src->client);
  return true;
}

void qa_ai_enter_seek_activate_entity_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                             const char *reason) {
  qa_bot_record_node_switch_bots_a(ctx, state, "activate entity", "", reason);
  state->ai_node = QA_AI_NODE_SEEK_ACTIVATE;
}

/* TS: goal === null ? "no goal" : goalName(goal.number).slice(0, 143). */
static void top_goal_detail(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, char *out, size_t size) {
  qa_bot_goal goal;
  if (!qa_bot_goal_get_top_goal(ai_goals(ctx), state->src->gs, &goal))
    snprintf(out, size, "no goal");
  else
    snprintf(out, size, "%s", qa_bot_goal_goal_name(ai_goals(ctx), goal.number));
}

void qa_ai_enter_seek_nbg_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, const char *reason) {
  char detail[GOAL_NAME_SIZE];
  top_goal_detail(ctx, state, detail, sizeof detail);
  qa_bot_record_node_switch_bots_a(ctx, state, "seek NBG", detail, reason);
  state->ai_node = QA_AI_NODE_SEEK_NBG;
}

void qa_ai_enter_seek_ltg_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, const char *reason) {
  char detail[GOAL_NAME_SIZE];
  top_goal_detail(ctx, state, detail, sizeof detail);
  qa_bot_record_node_switch_bots_a(ctx, state, "seek LTG", detail, reason);
  state->ai_node = QA_AI_NODE_SEEK_LTG;
}

void qa_ai_enter_battle_fight_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, const char *reason) {
  qa_bot_record_node_switch_bots_a(ctx, state, "battle fight", "", reason);
  ai_move_reset_last_avoid_reach(ctx, state->src->ms);
  state->ai_node = QA_AI_NODE_BATTLE_FIGHT;
}

void qa_ai_enter_battle_suicidal_fight_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                              const char *reason) {
  qa_ai_enter_battle_fight_bots_a(ctx, state, reason);
  state->src->flags |= QA_BOT_FLAG_FIGHTSUICIDAL;
}

void qa_ai_enter_battle_chase_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, const char *reason) {
  qa_bot_record_node_switch_bots_a(ctx, state, "battle chase", "", reason);
  state->src->chase_time = (float)ctx->time;
  state->ai_node = QA_AI_NODE_BATTLE_CHASE;
}

void qa_ai_enter_battle_retreat_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, const char *reason) {
  qa_bot_record_node_switch_bots_a(ctx, state, "battle retreat", "", reason);
  state->ai_node = QA_AI_NODE_BATTLE_RETREAT;
}

void qa_ai_enter_battle_nbg_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, const char *reason) {
  qa_bot_record_node_switch_bots_a(ctx, state, "battle NBG", "", reason);
  state->ai_node = QA_AI_NODE_BATTLE_NBG;
}

static bool check_lifecycle(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, const char *node,
                            bool clear_activation) {
  char reason[80];
  if (qa_bot_is_observer_bots_a(ctx, state)) {
    if (clear_activation)
      qa_bot_clear_activate_goal_stack_bots_a(ctx, state);
    snprintf(reason, sizeof reason, "%s: observer", strcmp(node, "activate entity") == 0 ? "active entity" : node);
    qa_ai_enter_observer_bots_a(ctx, state, reason);
    return true;
  }
  if (qa_bot_intermission_bots_a(ctx, state)) {
    if (clear_activation)
      qa_bot_clear_activate_goal_stack_bots_a(ctx, state);
    snprintf(reason, sizeof reason, "%s: %s", node, strcmp(node, "seek nbg") == 0 ? "intermision" : "intermission");
    qa_ai_enter_intermission_bots_a(ctx, state, reason);
    return true;
  }
  if (qa_bot_is_dead_bots_a(ctx, state)) {
    if (clear_activation)
      qa_bot_clear_activate_goal_stack_bots_a(ctx, state);
    snprintf(reason, sizeof reason, "%s: bot dead", node);
    qa_ai_enter_respawn_bots_a(ctx, state, reason);
    return true;
  }
  return false;
}

/* botInitialChat with no variables / one variable. */
static void initial_chat0(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, const char *type) {
  qa_bot_initial_chat_bots_a(ctx, state, type, NULL, 0);
}

static void initial_chat1(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, const char *type,
                          const char *variable) {
  const char *variables[1] = {variable};
  qa_bot_initial_chat_bots_a(ctx, state, type, variables, 1);
}

static const char *easy_name(qa_game_ai_context_bots_a *ctx, int64_t client, char *out) {
  return qa_easy_client_name_bots_a(ctx, client, 36, out);
}

static void enter_chat(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, int64_t to, int64_t mode) {
  qa_bot_chat_library_enter_chat(ai_chat(ctx), state->src->cs, to, mode, false, 0);
}

static void team_chat(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, const char *type, const char *voice) {
  initial_chat0(ctx, state, type);
  enter_chat(ctx, state, 0, CHAT_TEAM);
  qa_bot_voice_chat_only_bots_a(ctx, state, -1, voice);
  state->src->team_message_time = 0;
}

static void acknowledge(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, const char *type,
                        const char *variable) {
  initial_chat1(ctx, state, type, variable);
  enter_chat(ctx, state, state->src->decisionmaker, CHAT_TELL);
  qa_bot_voice_chat_only_bots_a(ctx, state, state->src->decisionmaker, "yes");
  qa_bot_action_action(ai_actions(ctx), state->src->client, QA_BOT_ACTION_AFFIRMATIVE);
  state->src->team_message_time = 0;
}

static void update_companion_goal(qa_game_ai_context_bots_a *ctx, qa_bot_goal_state_bots_a *goal, int64_t companion,
                                  qa_vec3 origin) {
  int64_t area = qa_bot_point_area_num_bots_a(ctx, origin);
  if (area != 0 && area_reachability(ctx, area) != 0) {
    goal->entity = (int32_t)companion;
    goal->area = (int32_t)area;
    goal->origin = qa_bot_fv3(origin);
    goal->mins = qa_bot_fv3(qa_math_vec3(-8, -8, -8));
    goal->maxs = qa_bot_fv3(qa_math_vec3(8, 8, 8));
  }
}

static bool select_team_base(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, qa_bot_goal *goal, bool enemy,
                             bool obelisk) {
  int64_t team = qa_bot_team_bots_a(ctx, state);
  if (team != QA_Q3_BASE_TEAM_RED && team != QA_Q3_BASE_TEAM_BLUE)
    return false;
  bool red = (team == QA_Q3_BASE_TEAM_RED) != enemy;
  qa_game_ai_deathmatch_state_bots_a *dm = ai_deathmatch(ctx);
  ai_goal_copy(goal, obelisk ? (red ? &dm->red_obelisk : &dm->blue_obelisk) : (red ? &dm->ctf_red_flag : &dm->ctf_blue_flag));
  return true;
}

static void wants_crouch(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (state->src->attack_crouch_time < AI_F(ctx->time - 5)) {
    double croucher = ai_character_bounded(ctx, state->src->character, QA_BCHAR_CROUCHER);
    if (ai_random(ctx) < AI_F((double)state->src->think_time * croucher))
      state->src->attack_crouch_time = (float)(AI_F(ctx->time + 5) + AI_F(croucher * 15));
  }
}

static bool message_due(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  return state->src->team_message_time != 0 && state->src->team_message_time < ctx->time;
}

static double dist_squared(qa_vec3 a, qa_vec3 b) {
  qa_vec3 direction = qa_sub3(a, b);
  return qa_dot3(direction, direction);
}

static bool team_help_goal(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, qa_bot_goal *goal) {
  char name[NAME_SIZE];
  if (message_due(ctx, state))
    acknowledge(ctx, state, "help_start", easy_name(ctx, state->src->teammate, name));
  if (state->src->team_goal_time < ctx->time)
    state->src->ltg_type = QA_LTG_NONE;
  if (state->src->teammate_visible_time < AI_F(ctx->time - 10))
    state->src->ltg_type = QA_LTG_NONE;
  qa_aas_entity_info_bots_a info = qa_bot_entity_info_bots_a(ctx, state->src->teammate);
  if (qa_bot_entity_visible_bots_a(ctx, state->src->entity_num, qa_bot_v3(state->src->eye),
                                   qa_bot_v3(state->src->viewangles), 360, state->src->teammate) != 0) {
    if (dist_squared(info.origin, qa_bot_v3(state->src->origin)) < 100 * 100) {
      ai_move_reset_avoid_reach(ctx, state->src->ms);
      return false;
    }
  } else {
    state->src->teammate_visible_time = (float)ctx->time;
  }
  if (info.valid)
    update_companion_goal(ctx, &state->src->team_goal, state->src->teammate, info.origin);
  *goal = qa_bot_goal_state_get(&state->src->team_goal);
  return true;
}

static bool team_accompany_goal(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, qa_bot_goal *goal) {
  char name[NAME_SIZE];
  if (message_due(ctx, state))
    acknowledge(ctx, state, "accompany_start", easy_name(ctx, state->src->teammate, name));
  if (state->src->team_goal_time < ctx->time) {
    initial_chat1(ctx, state, "accompany_stop", easy_name(ctx, state->src->teammate, name));
    enter_chat(ctx, state, state->src->teammate, CHAT_TELL);
    state->src->ltg_type = QA_LTG_NONE;
  }
  qa_aas_entity_info_bots_a info = qa_bot_entity_info_bots_a(ctx, state->src->teammate);
  qa_vec3 origin = qa_bot_v3(state->src->origin);
  if (qa_bot_entity_visible_bots_a(ctx, state->src->entity_num, qa_bot_v3(state->src->eye),
                                   qa_bot_v3(state->src->viewangles), 360, state->src->teammate) != 0) {
    state->src->teammate_visible_time = (float)ctx->time;
    if (dist_squared(info.origin, origin) <
        AI_F((double)state->src->formation_dist * (double)state->src->formation_dist)) {
      qa_aas_entity_info_bots_a self = qa_bot_entity_info_bots_a(ctx, state->src->entity_num);
      if (AI_F(self.origin.z + self.maxs.z) > AI_F(info.origin.z + info.mins.z) &&
          AI_F(self.origin.x + self.maxs.x) > AI_F(AI_F(info.origin.x + info.mins.x) - 4) &&
          AI_F(self.origin.x + self.mins.x) < AI_F(AI_F(info.origin.x + info.maxs.x) + 4) &&
          AI_F(self.origin.y + self.maxs.y) > AI_F(AI_F(info.origin.y + info.mins.y) - 4) &&
          AI_F(self.origin.y + self.mins.y) < AI_F(AI_F(info.origin.y + info.maxs.y) + 4) &&
          AI_F(self.origin.z + self.maxs.z) > AI_F(AI_F(info.origin.z + info.mins.z) - 4) &&
          AI_F(self.origin.z + self.mins.z) < AI_F(AI_F(info.origin.z + info.maxs.z) + 4)) {
        qa_vec3 forward = qa_qvm_angle_vectors(info.angles).forward;
        qa_vec3 horizontal = qa_normalize3(qa_math_vec3(forward.x, forward.y, 0));
        qa_vec3 away = qa_normalize3(qa_sub3(origin, info.origin));
        if (qa_dot3(horizontal, away) > AI_F(0.7)) {
          qa_bot_setup_for_movement_bots_a(ctx, state);
          ai_nav_move_in_direction(ctx, state->src->ms, away, 400, QA_BOT_MOVE_WALK);
        }
      }
      wants_crouch(ctx, state);
      if (swimming(ctx, state))
        state->src->attack_crouch_time = (float)(ctx->time - 1);
      if (state->src->arrive_time < AI_F(ctx->time - 2)) {
        if (state->src->arrive_time == 0) {
          qa_bot_action_gesture(ai_actions(ctx), state->src->client);
          initial_chat1(ctx, state, "accompany_arrive", easy_name(ctx, state->src->teammate, name));
          enter_chat(ctx, state, state->src->teammate, CHAT_TELL);
          state->src->arrive_time = (float)ctx->time;
        } else if (state->src->attack_crouch_time > ctx->time) {
          qa_bot_action_crouch(ai_actions(ctx), state->src->client);
        } else if (ai_random(ctx) < AI_F((double)state->src->think_time * AI_F(0.05))) {
          qa_bot_action_gesture(ai_actions(ctx), state->src->client);
        }
      }
      if (state->src->arrive_time > AI_F(ctx->time - 2)) {
        look_at(state, info.origin);
        half_roll(state);
      } else if (ai_random(ctx) < AI_F((double)state->src->think_time * AI_F(0.8))) {
        look_at_roam(ctx, state);
      }
      qa_bot_goal team_goal = qa_bot_goal_state_get(&state->src->team_goal);
      if (qa_bot_go_for_air_bots_a(ctx, state, state->src->tfl, &team_goal, 400)) {
        ai_move_reset_last_avoid_reach(ctx, state->src->ms);
        state->src->nbg_time = (float)(ctx->time + 8);
        qa_ai_enter_seek_nbg_bots_a(ctx, state, "BotLongTermGoal: go for air");
        return false;
      }
      ai_move_reset_avoid_reach(ctx, state->src->ms);
      return false;
    }
  }
  if (info.valid)
    update_companion_goal(ctx, &state->src->team_goal, state->src->teammate, info.origin);
  *goal = qa_bot_goal_state_get(&state->src->team_goal);
  if (state->src->teammate_visible_time < AI_F(ctx->time - 60)) {
    initial_chat1(ctx, state, "accompany_cannotfind", easy_name(ctx, state->src->teammate, name));
    enter_chat(ctx, state, state->src->teammate, CHAT_TELL);
    state->src->ltg_type = QA_LTG_NONE;
    state->src->teammate_visible_time = (float)ctx->time;
  }
  return true;
}

static const char *team_goal_name(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  return qa_bot_goal_goal_name(ai_goals(ctx), state->src->team_goal.number);
}

static bool defend_key_area_goal(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, qa_bot_goal *goal) {
  if (message_due(ctx, state)) {
    initial_chat1(ctx, state, "defend_start", team_goal_name(ctx, state));
    enter_chat(ctx, state, 0, CHAT_TEAM);
    qa_bot_voice_chat_only_bots_a(ctx, state, -1, "ondefense");
    state->src->team_message_time = 0;
  }
  *goal = qa_bot_goal_state_get(&state->src->team_goal);
  if (state->src->team_goal_time < ctx->time) {
    initial_chat1(ctx, state, "defend_stop", team_goal_name(ctx, state));
    enter_chat(ctx, state, 0, CHAT_TEAM);
    state->src->ltg_type = QA_LTG_NONE;
  }
  if (dist_squared(goal->origin, qa_bot_v3(state->src->origin)) < 70 * 70) {
    ai_move_reset_avoid_reach(ctx, state->src->ms);
    state->src->defend_away_time = (float)(AI_F(ctx->time + 3) + AI_F(3 * ai_random(ctx)));
    state->src->defend_away_range = qa_bot_has_persistant_powerup_and_weapon_bots_a(ctx, state) ? 100 : 350;
  }
  return true;
}

static bool kill_goal(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, int64_t flags, qa_bot_goal *goal) {
  char name[NAME_SIZE];
  if (message_due(ctx, state)) {
    initial_chat1(ctx, state, "kill_start", easy_name(ctx, state->src->team_goal.entity, name));
    enter_chat(ctx, state, state->src->decisionmaker, CHAT_TELL);
    state->src->team_message_time = 0;
  }
  if (state->src->last_killed_player == state->src->team_goal.entity) {
    initial_chat1(ctx, state, "kill_done", easy_name(ctx, state->src->team_goal.entity, name));
    enter_chat(ctx, state, state->src->decisionmaker, CHAT_TELL);
    state->src->last_killed_player = -1;
    state->src->ltg_type = QA_LTG_NONE;
  }
  if (state->src->team_goal_time < ctx->time)
    state->src->ltg_type = QA_LTG_NONE;
  return qa_bot_get_item_long_term_goal_bots_a(ctx, state, flags, goal);
}

static bool get_item_goal(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, qa_bot_goal *goal) {
  if (message_due(ctx, state))
    acknowledge(ctx, state, "getitem_start", team_goal_name(ctx, state));
  *goal = qa_bot_goal_state_get(&state->src->team_goal);
  if (state->src->team_goal_time < ctx->time)
    state->src->ltg_type = QA_LTG_NONE;
  qa_source_goal_status status = qa_bot_goal_source_goal_status(ai_goals(ctx), state->src->client, goal);
  if (status == QA_SOURCE_GOAL_UNAVAILABLE) {
    state->src->ltg_type = QA_LTG_NONE;
    return false;
  }
  if (status == QA_SOURCE_GOAL_AVAILABLE)
    return true;
  if (qa_bot_goal_item_goal_in_vis_but_not_visible(ai_goals(ctx), state->src->entity_num, qa_bot_v3(state->src->eye),
                                                   qa_bot_v3(state->src->viewangles), goal)) {
    initial_chat1(ctx, state, "getitem_notthere", team_goal_name(ctx, state));
    enter_chat(ctx, state, state->src->decisionmaker, CHAT_TELL);
    state->src->ltg_type = QA_LTG_NONE;
  } else if (qa_bot_reached_goal_bots_a(ctx, state, goal)) {
    initial_chat1(ctx, state, "getitem_gotit", team_goal_name(ctx, state));
    enter_chat(ctx, state, state->src->decisionmaker, CHAT_TELL);
    state->src->ltg_type = QA_LTG_NONE;
  }
  return true;
}

static bool camp_goal(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, qa_bot_goal *goal) {
  char name[NAME_SIZE];
  if (message_due(ctx, state)) {
    if (state->src->ltg_type == QA_LTG_CAMPORDER)
      acknowledge(ctx, state, "camp_start", easy_name(ctx, state->src->teammate, name));
    state->src->team_message_time = 0;
  }
  *goal = qa_bot_goal_state_get(&state->src->team_goal);
  if (state->src->team_goal_time < ctx->time) {
    if (state->src->ltg_type == QA_LTG_CAMPORDER) {
      initial_chat0(ctx, state, "camp_stop");
      enter_chat(ctx, state, state->src->decisionmaker, CHAT_TELL);
    }
    state->src->ltg_type = QA_LTG_NONE;
  }
  if (dist_squared(goal->origin, qa_bot_v3(state->src->origin)) < 60 * 60) {
    if (state->src->arrive_time == 0) {
      if (state->src->ltg_type == QA_LTG_CAMPORDER) {
        initial_chat1(ctx, state, "camp_arrive", easy_name(ctx, state->src->teammate, name));
        enter_chat(ctx, state, state->src->decisionmaker, CHAT_TELL);
        qa_bot_voice_chat_only_bots_a(ctx, state, state->src->decisionmaker, "inposition");
      }
      state->src->arrive_time = (float)ctx->time;
    }
    if (ai_random(ctx) < AI_F((double)state->src->think_time * AI_F(0.8)))
      look_at_roam(ctx, state);
    wants_crouch(ctx, state);
    if (state->src->attack_crouch_time > ctx->time)
      qa_bot_action_crouch(ai_actions(ctx), state->src->client);
    if (swimming(ctx, state))
      state->src->attack_crouch_time = (float)(ctx->time - 1);
    if ((ai_point_contents(ctx, qa_bot_v3(state->src->eye), state->src->entity_num) & LIQUID) != 0) {
      if (state->src->ltg_type == QA_LTG_CAMPORDER) {
        initial_chat0(ctx, state, "camp_stop");
        enter_chat(ctx, state, state->src->decisionmaker, CHAT_TELL);
        if (state->src->last_goal_ltg_type == QA_LTG_CAMPORDER)
          state->src->last_goal_ltg_type = QA_LTG_NONE;
      }
      state->src->ltg_type = QA_LTG_NONE;
    }
    ai_move_reset_avoid_reach(ctx, state->src->ms);
    return false;
  }
  return true;
}

static bool patrol_goal(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, qa_bot_goal *goal) {
  if (message_due(ctx, state)) {
    /* TS builds the route with string +=; the C string grows with the list. */
    size_t capacity = 1;
    for (const qa_bot_waypoint_bots_a *point = state->patrol_points; point != NULL; point = point->next)
      capacity += strlen(point->name) + 4;
    char *route = malloc(capacity);
    if (route == NULL)
      qa_range_error("Out of memory building patrol route");
    size_t length = 0;
    route[0] = 0;
    for (const qa_bot_waypoint_bots_a *point = state->patrol_points; point != NULL; point = point->next)
      length += (size_t)sprintf(route + length, "%s%s", point->name, point->next == NULL ? "" : " to ");
    acknowledge(ctx, state, "patrol_start", route);
    free(route);
  }
  qa_bot_waypoint_bots_a *current = state->current_patrol_point;
  if (current == NULL) {
    state->src->ltg_type = QA_LTG_NONE;
    return false;
  }
  if (qa_touching_goal(qa_bot_v3(state->src->origin), qa_bot_goal_state_get(&current->goal))) {
    if ((state->src->patrol_flags & QA_PATROL_BACK) != 0) {
      if (current->prev != NULL) {
        state->current_patrol_point = current->prev;
      } else {
        state->current_patrol_point = current->next;
        state->src->patrol_flags &= ~QA_PATROL_BACK;
      }
    } else if (current->next != NULL) {
      state->current_patrol_point = current->next;
    } else {
      state->current_patrol_point = current->prev;
      state->src->patrol_flags |= QA_PATROL_BACK;
    }
  }
  if (state->src->team_goal_time < ctx->time) {
    initial_chat0(ctx, state, "patrol_stop");
    enter_chat(ctx, state, state->src->decisionmaker, CHAT_TELL);
    state->src->ltg_type = QA_LTG_NONE;
  }
  if (state->current_patrol_point == NULL) {
    state->src->ltg_type = QA_LTG_NONE;
    return false;
  }
  *goal = qa_bot_goal_state_get(&state->current_patrol_point->goal);
  return true;
}

static void none_if_expired(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (state->src->team_goal_time < ctx->time)
    state->src->ltg_type = QA_LTG_NONE;
}

static void none_if_touching(qa_bot_state_bots_a *state, const qa_bot_goal *goal) {
  if (qa_touching_goal(qa_bot_v3(state->src->origin), *goal))
    state->src->ltg_type = QA_LTG_NONE;
}

static bool select_or_none(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, qa_bot_goal *goal, bool enemy,
                           bool obelisk) {
  if (!select_team_base(ctx, state, goal, enemy, obelisk)) {
    state->src->ltg_type = QA_LTG_NONE;
    return false;
  }
  return true;
}

/* TS: the GT_CTF block; *handled false falls through to the item goal. */
static bool ctf_goal(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, qa_bot_goal *goal, bool *handled) {
  *handled = true;
  if (state->src->ltg_type == QA_LTG_GETFLAG) {
    if (message_due(ctx, state))
      team_chat(ctx, state, "captureflag_start", "ongetflag");
    if (!select_or_none(ctx, state, goal, true, false))
      return false;
    if (qa_touching_goal(qa_bot_v3(state->src->origin), *goal)) {
      int64_t team = qa_bot_team_bots_a(ctx, state);
      if (team == QA_Q3_BASE_TEAM_RED)
        state->src->blue_flag_status = 1;
      else if (team == QA_Q3_BASE_TEAM_BLUE)
        state->src->red_flag_status = 1;
      state->src->ltg_type = QA_LTG_NONE;
    }
    none_if_expired(ctx, state);
    qa_bot_alternate_route_bots_a(ctx, state, goal);
    return true;
  }
  if (state->src->ltg_type == QA_LTG_RUSHBASE && state->src->rush_base_away_time < ctx->time) {
    if (!select_or_none(ctx, state, goal, false, false))
      return false;
    if (qa_bot_ctf_carrying_flag_bots_a(ctx, state) == 0)
      state->src->ltg_type = QA_LTG_NONE;
    none_if_expired(ctx, state);
    if (qa_touching_goal(qa_bot_v3(state->src->origin), *goal)) {
      if (qa_bot_ctf_carrying_flag_bots_a(ctx, state) != 0) {
        ai_move_reset_avoid_reach(ctx, state->src->ms);
        state->src->rush_base_away_time = (float)(AI_F(ctx->time + 5) + AI_F(10 * ai_random(ctx)));
      } else {
        state->src->ltg_type = QA_LTG_NONE;
      }
    }
    qa_bot_alternate_route_bots_a(ctx, state, goal);
    return true;
  }
  if (state->src->ltg_type == QA_LTG_RETURNFLAG) {
    if (message_due(ctx, state))
      team_chat(ctx, state, "returnflag_start", "onreturnflag");
    if (!select_or_none(ctx, state, goal, true, false))
      return false;
    none_if_touching(state, goal);
    none_if_expired(ctx, state);
    qa_bot_alternate_route_bots_a(ctx, state, goal);
    return true;
  }
  *handled = false;
  return false;
}

static bool one_flag_goal(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, int64_t flags, qa_bot_goal *goal,
                          bool *handled) {
  *handled = true;
  if (state->src->ltg_type == QA_LTG_GETFLAG) {
    if (message_due(ctx, state))
      team_chat(ctx, state, "captureflag_start", "ongetflag");
    ai_goal_copy(goal, &ai_deathmatch(ctx)->ctf_neutral_flag);
    none_if_touching(state, goal);
    none_if_expired(ctx, state);
    return true;
  }
  if (state->src->ltg_type == QA_LTG_RUSHBASE) {
    if (!select_or_none(ctx, state, goal, true, false))
      return false;
    if (!qa_bot_1fctf_carrying_flag_bots_a(ctx, state))
      state->src->ltg_type = QA_LTG_NONE;
    none_if_expired(ctx, state);
    none_if_touching(state, goal);
    qa_bot_alternate_route_bots_a(ctx, state, goal);
    return true;
  }
  if (state->src->ltg_type == QA_LTG_ATTACKENEMYBASE && state->src->attack_away_time < ctx->time) {
    if (message_due(ctx, state))
      team_chat(ctx, state, "attackenemybase_start", "onoffense");
    if (!select_or_none(ctx, state, goal, true, false))
      return false;
    none_if_expired(ctx, state);
    if (qa_touching_goal(qa_bot_v3(state->src->origin), *goal))
      state->src->attack_away_time = (float)(AI_F(ctx->time + 2) + AI_F(5 * ai_random(ctx)));
    return true;
  }
  if (state->src->ltg_type == QA_LTG_RETURNFLAG) {
    if (message_due(ctx, state))
      team_chat(ctx, state, "returnflag_start", "onreturnflag");
    none_if_expired(ctx, state);
    return qa_bot_get_item_long_term_goal_bots_a(ctx, state, flags, goal);
  }
  *handled = false;
  return false;
}

static bool obelisk_goal(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, int64_t flags, qa_bot_goal *goal,
                         bool *handled) {
  *handled = true;
  if (state->src->ltg_type == QA_LTG_ATTACKENEMYBASE && state->src->attack_away_time < ctx->time) {
    if (message_due(ctx, state))
      team_chat(ctx, state, "attackenemybase_start", "onoffense");
    if (!select_or_none(ctx, state, goal, true, true))
      return false;
    if (qa_bot_feeling_bad_bots_a(ctx, state) > 50)
      return qa_bot_get_item_long_term_goal_bots_a(ctx, state, flags, goal);
    if (qa_touching_goal(qa_bot_v3(state->src->origin), *goal))
      state->src->attack_away_time = (float)(AI_F(ctx->time + 3) + AI_F(5 * ai_random(ctx)));
    if (dist_squared(qa_bot_v3(state->src->origin), goal->origin) < 60 * 60)
      state->src->attack_away_time = (float)(AI_F(ctx->time + 3) + AI_F(5 * ai_random(ctx)));
    none_if_expired(ctx, state);
    qa_bot_alternate_route_bots_a(ctx, state, goal);
    return true;
  }
  *handled = false;
  return false;
}

static bool harvester_goal(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, qa_bot_goal *goal, bool *handled) {
  *handled = true;
  if (state->src->ltg_type == QA_LTG_RUSHBASE) {
    if (!select_team_base(ctx, state, goal, true, true) || !qa_bot_harvester_carrying_cubes_bots_a(ctx, state) ||
        state->src->team_goal_time < ctx->time || qa_touching_goal(qa_bot_v3(state->src->origin), *goal)) {
      qa_bot_go_harvest_bots_a(ctx, state);
      return false;
    }
    qa_bot_alternate_route_bots_a(ctx, state, goal);
    return true;
  }
  if (state->src->ltg_type == QA_LTG_ATTACKENEMYBASE && state->src->attack_away_time < ctx->time) {
    if (message_due(ctx, state))
      team_chat(ctx, state, "attackenemybase_start", "onoffense");
    if (!select_or_none(ctx, state, goal, true, true))
      return false;
    none_if_expired(ctx, state);
    if (qa_touching_goal(qa_bot_v3(state->src->origin), *goal))
      state->src->attack_away_time = (float)(AI_F(ctx->time + 2) + AI_F(5 * ai_random(ctx)));
    return true;
  }
  if (state->src->ltg_type == QA_LTG_HARVEST && state->src->harvest_away_time < ctx->time) {
    if (message_due(ctx, state))
      team_chat(ctx, state, "harvest_start", "onoffense");
    ai_goal_copy(goal, &ai_deathmatch(ctx)->neutral_obelisk);
    none_if_expired(ctx, state);
    if (qa_touching_goal(qa_bot_v3(state->src->origin), *goal))
      state->src->harvest_away_time = (float)(AI_F(ctx->time + 4) + AI_F(3 * ai_random(ctx)));
    return true;
  }
  *handled = false;
  return false;
}

bool qa_bot_get_long_term_goal_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, int64_t flags,
                                      bool retreat, qa_bot_goal *goal) {
  if (state->src->ltg_type == QA_LTG_TEAMHELP && !retreat)
    return team_help_goal(ctx, state, goal);
  if (state->src->ltg_type == QA_LTG_TEAMACCOMPANY && !retreat)
    return team_accompany_goal(ctx, state, goal);
  if (state->src->ltg_type == QA_LTG_DEFENDKEYAREA &&
      default_travel_time(ctx, state, state->src->team_goal.area) > state->src->defend_away_range)
    state->src->defend_away_time = 0;
  if (state->src->ltg_type == QA_LTG_DEFENDKEYAREA && !retreat && state->src->defend_away_time < ctx->time)
    return defend_key_area_goal(ctx, state, goal);
  if (state->src->ltg_type == QA_LTG_KILL && !retreat)
    return kill_goal(ctx, state, flags, goal);
  if (state->src->ltg_type == QA_LTG_GETITEM && !retreat)
    return get_item_goal(ctx, state, goal);
  if ((state->src->ltg_type == QA_LTG_CAMP || state->src->ltg_type == QA_LTG_CAMPORDER) && !retreat)
    return camp_goal(ctx, state, goal);
  if (state->src->ltg_type == QA_LTG_PATROL && !retreat)
    return patrol_goal(ctx, state, goal);
  int64_t game_type = ai_deathmatch(ctx)->gametype;
  bool handled = false, result = false;
  if (game_type == QA_Q3_BASE_GT_CTF) {
    result = ctf_goal(ctx, state, goal, &handled);
  } else if (ai_is_missionpack(ctx)) {
    if (game_type == QA_Q3_BASE_GT_1FCTF)
      result = one_flag_goal(ctx, state, flags, goal, &handled);
    else if (game_type == QA_Q3_BASE_GT_OBELISK)
      result = obelisk_goal(ctx, state, flags, goal, &handled);
    else if (game_type == QA_Q3_BASE_GT_HARVESTER)
      result = harvester_goal(ctx, state, goal, &handled);
  }
  if (handled)
    return result;
  return qa_bot_get_item_long_term_goal_bots_a(ctx, state, flags, goal);
}

static void lead_followme(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  char name[NAME_SIZE];
  initial_chat1(ctx, state, "followme", easy_name(ctx, state->src->lead_teammate, name));
  enter_chat(ctx, state, state->src->teammate, CHAT_TELL);
  state->src->lead_message_time = (float)ctx->time;
}

bool qa_bot_long_term_goal_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, int64_t flags,
                                  bool retreat, qa_bot_goal *goal) {
  if (state->src->lead_time > 0 && !retreat) {
    if (state->src->lead_time < ctx->time) {
      char name[NAME_SIZE];
      initial_chat1(ctx, state, "lead_stop", easy_name(ctx, state->src->lead_teammate, name));
      enter_chat(ctx, state, state->src->teammate, CHAT_TELL);
      state->src->lead_time = 0;
      return qa_bot_get_long_term_goal_bots_a(ctx, state, flags, retreat, goal);
    }
    if (state->src->lead_message_time < 0 && -(double)state->src->lead_message_time < ctx->time)
      lead_followme(ctx, state);
    qa_aas_entity_info_bots_a info = qa_bot_entity_info_bots_a(ctx, state->src->lead_teammate);
    if (info.valid)
      update_companion_goal(ctx, &state->src->lead_team_goal, state->src->lead_teammate, info.origin);
    if (qa_bot_entity_visible_bots_a(ctx, state->src->entity_num, qa_bot_v3(state->src->eye),
                                     qa_bot_v3(state->src->viewangles), 360, state->src->lead_teammate) != 0)
      state->src->lead_visible_time = (float)ctx->time;
    if (state->src->lead_visible_time < AI_F(ctx->time - 1))
      state->src->lead_backup_time = (float)(ctx->time + 2);
    double distance_squared = dist_squared(qa_bot_v3(state->src->origin), qa_bot_v3(state->src->lead_team_goal.origin));
    if (state->src->lead_backup_time > ctx->time) {
      if (state->src->lead_message_time < AI_F(ctx->time - 20))
        lead_followme(ctx, state);
      if (distance_squared < 100 * 100)
        state->src->lead_backup_time = 0;
      *goal = qa_bot_goal_state_get(&state->src->lead_team_goal);
      return true;
    } else if (distance_squared > 500 * 500) {
      if (state->src->lead_message_time < AI_F(ctx->time - 20))
        lead_followme(ctx, state);
      look_at(state, info.origin);
      half_roll(state);
      return false;
    }
  }
  return qa_bot_get_long_term_goal_bots_a(ctx, state, flags, retreat, goal);
}

static void attack_if_clear(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, qa_vec3 *ideal, qa_vec3 target,
                            int64_t number) {
  if (qa_in_field_of_vision_bots_a(qa_bot_v3(state->src->viewangles), 20, ideal)) {
    qa_q3_base_server_trace_result trace =
        qa_bot_ai_trace_bots_a(ctx, qa_bot_v3(state->src->eye), target, state->src->entity_num, MASK_SHOT, NULL);
    if (trace.fraction >= 1 || trace.entity_num == number)
      qa_bot_action_attack(ai_actions(ctx), state->src->client);
  }
}

void qa_bot_clear_path_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, qa_bot_move_result *result) {
  qa_vec3 eye = qa_bot_v3(state->src->eye);
  if (state->src->kamikaze_body != 0 && (result->flags & MOVEMENT_AIM) == 0) {
    qa_q3_base_entity_state body;
    ai_entity_state_or_blank(ctx, state->src->kamikaze_body, &body);
    qa_vec3 base = ai_entity_pos_base(&body);
    qa_vec3 target = qa_math_vec3(base.x, base.y, base.z + 8);
    result->ideal_view_angles = qa_vector_to_angles(qa_sub3(target, eye));
    result->weapon = ai_knowledge_activation_weapon(ctx, state);
    if (result->weapon == -1)
      result->weapon = 0;
    if (result->weapon != 0) {
      result->flags |= MOVEMENT_AIM;
      if (qa_bot_state_weapon_checked(state) == result->weapon)
        attack_if_clear(ctx, state, &result->ideal_view_angles, target, body.number);
    }
  }
  if ((result->flags & QA_BOT_MRF_BLOCKEDBYAVOIDSPOT) != 0)
    state->src->blocked_by_avoid_spot_time = (float)(ctx->time + 5);
  if (state->src->blocked_by_avoid_spot_time > ctx->time && (result->flags & MOVEMENT_AIM) == 0) {
    double best_distance = 300;
    int64_t best_mine = -1;
    qa_vec3 origin = qa_bot_v3(state->src->origin);
    for (int64_t index = 0; index < state->src->num_prox_mines; index++) {
      if (index >= QA_MAX_PROXMINES_BOTS_A)
        qa_range_error("Missing proximity mine slot %lld", (long long)index);
      qa_q3_base_entity_state mine;
      ai_entity_state_or_blank(ctx, state->src->prox_mines[index], &mine);
      double distance = qa_length3(qa_sub3(ai_entity_pos_base(&mine), origin));
      if (distance < best_distance) {
        best_distance = distance;
        best_mine = index;
      }
    }
    if (best_mine != -1) {
      qa_q3_base_entity_state mine;
      ai_entity_state_or_blank(ctx, state->src->prox_mines[best_mine], &mine);
      qa_vec3 base = ai_entity_pos_base(&mine);
      qa_vec3 target = qa_math_vec3(base.x, base.y, base.z + 2);
      result->ideal_view_angles = qa_vector_to_angles(qa_sub3(target, eye));
      if (inventory(state, QA_INV_PLASMAGUN) > 0 && inventory(state, QA_INV_CELLS) > 0)
        result->weapon = QA_Q3_WP_PLASMAGUN;
      else if (inventory(state, QA_INV_ROCKETLAUNCHER) > 0 && inventory(state, QA_INV_ROCKETS) > 0)
        result->weapon = QA_Q3_WP_ROCKET_LAUNCHER;
      else if (inventory(state, QA_INV_BFG10K) > 0 && inventory(state, QA_INV_BFGAMMO) > 0)
        result->weapon = QA_Q3_WP_BFG;
      else
        result->weapon = 0;
      if (result->weapon != 0) {
        result->flags |= MOVEMENT_AIM;
        if (qa_bot_state_weapon_checked(state) == result->weapon)
          attack_if_clear(ctx, state, &result->ideal_view_angles, target, mine.number);
      }
    }
  }
}

static void adopt_movement_weapon(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                  const qa_bot_move_result *result) {
  if ((result->flags & QA_BOT_MRF_MOVEMENTWEAPON) != 0 && ai_knowledge_has_weapon(ctx, state->src->ws, result->weapon))
    state->src->weapon_num = (int32_t)result->weapon;
}

static void view_target_or_direction(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, const qa_bot_goal *goal,
                                     const qa_bot_move_result *result) {
  qa_vec3 target;
  if (movement_view_target(ctx, state, goal, &target))
    look_at(state, target);
  else
    set_ideal(state, qa_vector_to_angles(result->move_direction));
}

bool qa_ai_node_seek_activate_entity_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (check_lifecycle(ctx, state, "activate entity", true))
    return false;
  set_travel_flags(ctx, state, false);
  qa_bot_map_scripts_bots_a(ctx, state);
  state->src->enemy = -1;
  qa_bot_activate_goal_bots_a *activation = state->activate_stack;
  if (activation == NULL) {
    qa_bot_clear_activate_goal_stack_bots_a(ctx, state);
    qa_ai_enter_seek_nbg_bots_a(ctx, state, "activate entity: no goal");
    return false;
  }
  /* TS goal is the live cell of the first activation; the reread below keeps it. */
  qa_bot_goal_state_bots_a *goal_cell = &activation->src->goal;
  qa_vec3 eye = qa_bot_v3(state->src->eye);
  bool target_visible = false;
  if (activation->src->shoot) {
    qa_vec3 target = qa_bot_v3(activation->src->target);
    qa_q3_base_server_trace_result trace =
        qa_bot_ai_trace_bots_a(ctx, eye, target, state->src->entity_num, MASK_SHOT, NULL);
    if (trace.fraction >= 1 || trace.entity_num == goal_cell->entity) {
      target_visible = true;
      if (qa_bot_state_weapon_checked(state) == activation->src->weapon) {
        qa_vec3 angles = qa_vector_to_angles(qa_sub3(target, eye));
        if (qa_in_field_of_vision_bots_a(qa_bot_v3(state->src->viewangles), 20, &angles))
          qa_bot_action_attack(ai_actions(ctx), state->src->client);
      }
    }
  }
  qa_bot_move_result result = {0};
  if (target_visible) {
    qa_aas_entity_info_bots_a info = qa_bot_entity_info_bots_a(ctx, goal_cell->entity);
    qa_vec3 stored = qa_bot_v3(activation->src->origin);
    if (stored.x != info.origin.x || stored.y != info.origin.y || stored.z != info.origin.z) {
      if (ai_debug_build(ctx))
        ai_print(ctx, "hit shootable button or trigger\n");
      activation->src->time = 0;
    }
    if (activation->src->time < ctx->time) {
      qa_bot_pop_from_activate_goal_stack_bots_a(ctx, state);
      if (state->activate_stack != NULL) {
        state->activate_stack->src->time = (float)(ctx->time + 10);
        return false;
      }
      qa_ai_enter_seek_nbg_bots_a(ctx, state, "activate entity: time out");
      return false;
    }
  } else {
    qa_bot_goal goal = qa_bot_goal_state_get(goal_cell);
    if (!activation->src->shoot && qa_touching_goal(qa_bot_v3(state->src->origin), goal)) {
      if (ai_debug_build(ctx))
        ai_print(ctx, "touched button or trigger\n");
      activation->src->time = 0;
    }
    if (activation->src->time < ctx->time) {
      qa_bot_pop_from_activate_goal_stack_bots_a(ctx, state);
      if (state->activate_stack != NULL) {
        state->activate_stack->src->time = (float)(ctx->time + 10);
        return false;
      }
      qa_ai_enter_seek_nbg_bots_a(ctx, state, "activate entity: activated");
      return false;
    }
    if (qa_bot_ai_predict_obstacles_bots_a(ctx, state, &goal))
      return false;
    qa_bot_setup_for_movement_bots_a(ctx, state);
    ai_nav_move_to_goal(ctx, &result, state->src->ms, &goal, state->src->tfl);
    if (result.failure) {
      ai_move_reset_avoid_reach(ctx, state->src->ms);
      activation->src->time = 0;
    }
    qa_bot_ai_blocked_bots_a(ctx, state, &result, true);
  }
  qa_bot_clear_path_bots_a(ctx, state, &result);
  /* BotAIBlocked may push an activation; the source rereads the stack here. */
  activation = state->activate_stack;
  if (activation == NULL)
    qa_range_error("Activation movement cleared the source activation stack");
  if (activation->src->shoot) {
    if ((result.flags & QA_BOT_MRF_MOVEMENTVIEW) == 0) {
      result.ideal_view_angles = qa_vector_to_angles(qa_sub3(qa_bot_v3(activation->src->target), eye));
      result.flags |= QA_BOT_MRF_MOVEMENTVIEW;
    }
    if ((result.flags & QA_BOT_MRF_MOVEMENTWEAPON) == 0) {
      result.flags |= QA_BOT_MRF_MOVEMENTWEAPON;
      activation->src->weapon = (int32_t)ai_knowledge_activation_weapon(ctx, state);
      if (activation->src->weapon == -1)
        activation->src->weapon = 0;
      result.weapon = activation->src->weapon;
    }
  }
  if ((result.flags & MOVEMENT_VIEW) != 0) {
    set_ideal(state, result.ideal_view_angles);
  } else if ((result.flags & QA_BOT_MRF_WAITING) != 0) {
    if (ai_random(ctx) < AI_F((double)state->src->think_time * AI_F(0.8)))
      look_at_roam(ctx, state);
  } else if ((state->src->flags & QA_BOT_FLAG_IDEALVIEWSET) == 0) {
    qa_bot_goal goal = qa_bot_goal_state_get(goal_cell);
    view_target_or_direction(ctx, state, &goal, &result);
    half_roll(state);
  }
  adopt_movement_weapon(ctx, state, &result);
  if (qa_bot_find_enemy_bots_a(ctx, state, -1)) {
    if (qa_bot_wants_to_retreat_bots_a(ctx, state)) {
      qa_ai_enter_battle_nbg_bots_a(ctx, state, "activate entity: found enemy");
    } else {
      ai_move_reset_last_avoid_reach(ctx, state->src->ms);
      qa_bot_goal_empty_goal_stack(ai_goals(ctx), state->src->gs);
      qa_ai_enter_battle_fight_bots_a(ctx, state, "activate entity: found enemy");
    }
    qa_bot_clear_activate_goal_stack_bots_a(ctx, state);
  }
  return true;
}

bool qa_ai_node_seek_nbg_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (check_lifecycle(ctx, state, "seek nbg", false))
    return false;
  set_travel_flags(ctx, state, true);
  qa_bot_map_scripts_bots_a(ctx, state);
  state->src->enemy = -1;
  qa_bot_goal goal = {0}, top;
  if (!qa_bot_goal_get_top_goal(ai_goals(ctx), state->src->gs, &top)) {
    state->src->nbg_time = 0;
  } else {
    ai_goal_copy(&goal, &top);
    if (qa_bot_reached_goal_bots_a(ctx, state, &goal)) {
      qa_bot_choose_weapon_bots_a(ctx, state);
      state->src->nbg_time = 0;
    }
  }
  if (state->src->nbg_time < ctx->time) {
    qa_bot_goal_pop_goal(ai_goals(ctx), state->src->gs);
    state->src->check_time = (float)(ctx->time + AI_F(0.05));
    qa_ai_enter_seek_ltg_bots_a(ctx, state, "seek nbg: time out");
    return false;
  }
  if (qa_bot_ai_predict_obstacles_bots_a(ctx, state, &goal))
    return false;
  qa_bot_setup_for_movement_bots_a(ctx, state);
  qa_bot_move_result result = {0};
  ai_nav_move_to_goal(ctx, &result, state->src->ms, &goal, state->src->tfl);
  if (result.failure) {
    ai_move_reset_avoid_reach(ctx, state->src->ms);
    state->src->nbg_time = 0;
  }
  qa_bot_ai_blocked_bots_a(ctx, state, &result, true);
  qa_bot_clear_path_bots_a(ctx, state, &result);
  if ((result.flags & MOVEMENT_VIEW) != 0) {
    set_ideal(state, result.ideal_view_angles);
  } else if ((result.flags & QA_BOT_MRF_WAITING) != 0) {
    if (ai_random(ctx) < AI_F((double)state->src->think_time * AI_F(0.8)))
      look_at_roam(ctx, state);
  } else if ((state->src->flags & QA_BOT_FLAG_IDEALVIEWSET) == 0) {
    qa_bot_goal next;
    if (qa_bot_goal_get_second_goal(ai_goals(ctx), state->src->gs, &next) ||
        qa_bot_goal_get_top_goal(ai_goals(ctx), state->src->gs, &next))
      ai_goal_copy(&goal, &next);
    view_target_or_direction(ctx, state, &goal, &result);
    half_roll(state);
  }
  adopt_movement_weapon(ctx, state, &result);
  if (qa_bot_find_enemy_bots_a(ctx, state, -1)) {
    if (qa_bot_wants_to_retreat_bots_a(ctx, state)) {
      qa_ai_enter_battle_nbg_bots_a(ctx, state, "seek nbg: found enemy");
    } else {
      ai_move_reset_last_avoid_reach(ctx, state->src->ms);
      qa_bot_goal_empty_goal_stack(ai_goals(ctx), state->src->gs);
      qa_ai_enter_battle_fight_bots_a(ctx, state, "seek nbg: found enemy");
    }
  }
  return true;
}

static double objective_nearby_range(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, double range) {
  int64_t game_type = ai_deathmatch(ctx)->gametype;
  if (game_type == QA_Q3_BASE_GT_CTF) {
    if (qa_bot_ctf_carrying_flag_bots_a(ctx, state) != 0)
      return 50;
  } else if (state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    if (game_type == QA_Q3_BASE_GT_1FCTF && qa_bot_1fctf_carrying_flag_bots_a(ctx, state))
      return 50;
    if (game_type == QA_Q3_BASE_GT_HARVESTER && qa_bot_harvester_carrying_cubes_bots_a(ctx, state))
      return 80;
  }
  return range;
}

static void scripted_combat(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (state->src->enemy < 0)
    return;
  qa_bot_update_battle_inventory_bots_a(ctx, state, state->src->enemy);
  qa_bot_choose_weapon_bots_a(ctx, state);
  qa_bot_aim_at_enemy_bots_a(ctx, state);
  qa_bot_check_attack_bots_a(ctx, state);
}


static double bounds_radius(qa_bounds bounds) {
  return qa_js_max(qa_js_max(qa_js_max(fabs(bounds.min.x), fabs(bounds.min.y)), bounds.max.x), bounds.max.y);
}

static bool scripted_goal(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, qa_bot_goal *goal) {
  if (!state->has_scripted_order)
    return false;
  qa_bot_observed_entity_bots_a self = ai_entity(ctx, state->src->entity_num);
  const qa_bot_order *order = &state->scripted_order.order;
  bool has_target = order->kind == QA_BOT_ORDER_FOLLOW;
  qa_bot_observed_entity_bots_a target;
  memset(&target, 0, sizeof target);
  if (has_target) {
    target = ai_entity(ctx, order->entity.number);
    if (!target.present || target.generation != order->entity.generation) {
      state->scripted_order.progress = QA_BOT_ORDER_ERROR;
      return false;
    }
  }
  qa_vec3 origin = has_target ? target.origin : order->point;
  double radius = bounds_radius(self.bounds);
  double target_radius = has_target ? bounds_radius(target.bounds) : 0;
  /* Math.SQRT2 */
  double clearance = has_target ? 1.4142135623730951 * (radius + target_radius) : 0;
  qa_vec3 self_origin = qa_bot_v3(state->src->origin);
  if (qa_length3(qa_sub3(origin, self_origin)) <= (has_target ? clearance + radius : radius * 2)) {
    state->scripted_order.progress = QA_BOT_ORDER_SUCCESS;
    return false;
  }
  qa_vec3 toward_bot = qa_sub3(self_origin, origin);
  double horizontal = qa_js_hypot2(toward_bot.x, toward_bot.y);
  double stand_off = !has_target || horizontal == 0 ? 0 : (clearance + radius * 0.5) / horizontal;
  qa_vec3 destination =
      qa_math_vec3(origin.x + toward_bot.x * stand_off, origin.y + toward_bot.y * stand_off, origin.z);
  int64_t area = qa_bot_point_area_num_bots_a(ctx, destination);
  if (area == 0) {
    state->scripted_order.progress = QA_BOT_ORDER_ERROR;
    return false;
  }
  state->scripted_order.progress = QA_BOT_ORDER_IN_PROGRESS;
  goal->origin = destination;
  goal->area = area;
  goal->mins = ai_fv(qa_math_vec3(-radius, -radius, -radius));
  goal->maxs = ai_fv(qa_math_vec3(radius, radius, radius));
  goal->entity = has_target ? order->entity.number : -1;
  return true;
}

static bool order_active(const qa_bot_state_bots_a *state) {
  return qa_bot_order_active(state->has_scripted_order ? &state->scripted_order : NULL);
}

bool qa_ai_node_seek_ltg_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (check_lifecycle(ctx, state, "seek ltg", false))
    return false;
  bool scripted = order_active(state);
  if (!scripted && qa_bot_chat_random_bots_a(ctx, state)) {
    state->src->stand_time = (float)(ctx->time + qa_bot_chat_time_bots_a(ctx, state));
    qa_ai_enter_stand_bots_a(ctx, state, "seek ltg: random chat");
    return false;
  }
  set_travel_flags(ctx, state, true);
  qa_bot_map_scripts_bots_a(ctx, state);
  state->src->enemy = -1;
  if (state->src->killed_enemy_time > AI_F(ctx->time - 2) && ai_random(ctx) < state->src->think_time)
    qa_bot_action_gesture(ai_actions(ctx), state->src->client);
  if (qa_bot_find_enemy_bots_a(ctx, state, -1) && !scripted) {
    if (qa_bot_wants_to_retreat_bots_a(ctx, state)) {
      qa_ai_enter_battle_retreat_bots_a(ctx, state, "seek ltg: found enemy");
      return false;
    }
    ai_move_reset_last_avoid_reach(ctx, state->src->ms);
    qa_bot_goal_empty_goal_stack(ai_goals(ctx), state->src->gs);
    qa_ai_enter_battle_fight_bots_a(ctx, state, "seek ltg: found enemy");
    return false;
  }
  if (!scripted)
    qa_bot_team_goals_bots_a(ctx, state, false);
  qa_bot_goal goal = {0};
  if (scripted ? !scripted_goal(ctx, state, &goal)
               : !qa_bot_long_term_goal_bots_a(ctx, state, state->src->tfl, false, &goal)) {
    if (scripted)
      scripted_combat(ctx, state);
    return true;
  }
  if (!scripted && state->src->check_time < ctx->time) {
    state->src->check_time = (float)(ctx->time + 0.5);
    qa_bot_wants_to_camp_bots_a(ctx, state);
    double range = objective_nearby_range(ctx, state, state->src->ltg_type == QA_LTG_DEFENDKEYAREA ? 400 : 150);
    if (qa_bot_nearby_goal_bots_a(ctx, state, state->src->tfl, &goal, range)) {
      ai_move_reset_last_avoid_reach(ctx, state->src->ms);
      state->src->nbg_time = (float)(AI_F(ctx->time + 4) + AI_F(range * AI_F(0.01)));
      qa_ai_enter_seek_nbg_bots_a(ctx, state, "ltg seek: nbg");
      return false;
    }
  }
  if (qa_bot_ai_predict_obstacles_bots_a(ctx, state, &goal))
    return false;
  qa_bot_setup_for_movement_bots_a(ctx, state);
  qa_bot_move_result result = {0};
  ai_nav_move_to_goal(ctx, &result, state->src->ms, &goal, state->src->tfl);
  if (result.failure) {
    ai_move_reset_avoid_reach(ctx, state->src->ms);
    state->src->ltg_time = 0;
    if (scripted && state->has_scripted_order)
      state->scripted_order.progress = QA_BOT_ORDER_ERROR;
  }
  qa_bot_ai_blocked_bots_a(ctx, state, &result, true);
  qa_bot_clear_path_bots_a(ctx, state, &result);
  if ((result.flags & MOVEMENT_VIEW) != 0) {
    set_ideal(state, result.ideal_view_angles);
  } else if ((result.flags & QA_BOT_MRF_WAITING) != 0) {
    if (ai_random(ctx) < AI_F((double)state->src->think_time * AI_F(0.8)))
      look_at_roam(ctx, state);
  } else if ((state->src->flags & QA_BOT_FLAG_IDEALVIEWSET) == 0) {
    qa_vec3 target;
    if (movement_view_target(ctx, state, &goal, &target))
      look_at(state, target);
    else if (qa_dot3(result.move_direction, result.move_direction) != 0)
      set_ideal(state, qa_vector_to_angles(result.move_direction));
    else if (ai_random(ctx) < AI_F((double)state->src->think_time * AI_F(0.8)))
      look_at_roam(ctx, state);
    half_roll(state);
  }
  adopt_movement_weapon(ctx, state, &result);
  if (scripted && (result.flags & (MOVEMENT_VIEW | QA_BOT_MRF_MOVEMENTWEAPON)) == 0)
    scripted_combat(ctx, state);
  return true;
}

static void remember_enemy_position(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, qa_vec3 origin) {
  qa_game_ai_deathmatch_state_bots_a *dm = ai_deathmatch(ctx);
  qa_vec3 target = qa_math_vec3(origin.x, origin.y, origin.z);
  if (state->src->enemy >= QA_Q3_MAX_CLIENTS && state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK &&
      (state->src->enemy == dm->red_obelisk.entity || state->src->enemy == dm->blue_obelisk.entity))
    target = qa_math_vec3(target.x, target.y, target.z + 16);
  int64_t area = qa_bot_point_area_num_bots_a(ctx, target);
  if (area != 0 && area_reachability(ctx, area) != 0) {
    state->src->last_enemy_origin = qa_bot_fv3(target);
    state->src->last_enemy_area_num = (int32_t)area;
  }
}

bool qa_ai_node_battle_fight_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (check_lifecycle(ctx, state, "battle fight", false))
    return false;
  if (qa_bot_find_enemy_bots_a(ctx, state, state->src->enemy) && ai_debug_build(ctx))
    ai_print(ctx, "found new better enemy\n");
  if (state->src->enemy < 0) {
    qa_ai_enter_seek_ltg_bots_a(ctx, state, "battle fight: no enemy");
    return false;
  }
  qa_aas_entity_info_bots_a info = qa_bot_entity_info_bots_a(ctx, state->src->enemy);
  if (state->src->enemy_death_time != 0) {
    if (state->src->enemy_death_time < AI_F(ctx->time - 1)) {
      state->src->enemy_death_time = 0;
      if (state->src->enemy_suicide)
        qa_bot_chat_enemy_suicide_bots_a(ctx, state);
      if (state->src->last_killed_player == state->src->enemy && qa_bot_chat_kill_bots_a(ctx, state)) {
        state->src->stand_time = (float)(ctx->time + qa_bot_chat_time_bots_a(ctx, state));
        qa_ai_enter_stand_bots_a(ctx, state, "battle fight: enemy dead");
      } else {
        state->src->ltg_time = 0;
        qa_ai_enter_seek_ltg_bots_a(ctx, state, "battle fight: enemy dead");
      }
      return false;
    }
  } else if (qa_entity_is_dead_bots_a(ctx, &info)) {
    state->src->enemy_death_time = (float)ctx->time;
  }
  if (qa_entity_is_invisible_bots_a(ctx, &info) && !qa_entity_is_shooting_bots_a(&info) && ai_random(ctx) < AI_F(0.2)) {
    qa_ai_enter_seek_ltg_bots_a(ctx, state, "battle fight: invisible");
    return false;
  }
  remember_enemy_position(ctx, state, info.origin);
  qa_bot_update_battle_inventory_bots_a(ctx, state, state->src->enemy);
  if (state->src->last_frame_health > inventory(state, QA_INV_HEALTH) && qa_bot_chat_hit_no_death_bots_a(ctx, state)) {
    state->src->stand_time = (float)(ctx->time + qa_bot_chat_time_bots_a(ctx, state));
    qa_ai_enter_stand_bots_a(ctx, state, "battle fight: chat health decreased");
    return false;
  }
  int32_t hits = state->src->cur_ps.persistant[QA_Q3_BASE_PERS_HITS];
  if (hits > state->src->last_hit_count && qa_bot_chat_hit_no_kill_bots_a(ctx, state)) {
    state->src->stand_time = (float)(ctx->time + qa_bot_chat_time_bots_a(ctx, state));
    qa_ai_enter_stand_bots_a(ctx, state, "battle fight: chat hit someone");
    return false;
  }
  if (qa_bot_entity_visible_bots_a(ctx, state->src->entity_num, qa_bot_v3(state->src->eye),
                                   qa_bot_v3(state->src->viewangles), 360, state->src->enemy) == 0) {
    if (qa_bot_wants_to_chase_bots_a(ctx, state))
      qa_ai_enter_battle_chase_bots_a(ctx, state, "battle fight: enemy out of sight");
    else
      qa_ai_enter_seek_ltg_bots_a(ctx, state, "battle fight: enemy out of sight");
    return false;
  }
  qa_bot_battle_use_items_bots_a(ctx, state);
  set_travel_flags(ctx, state, true);
  qa_bot_choose_weapon_bots_a(ctx, state);
  qa_bot_move_result result = qa_bot_attack_move_bots_a(ctx, state, state->src->tfl);
  if (result.failure) {
    ai_move_reset_avoid_reach(ctx, state->src->ms);
    state->src->ltg_time = 0;
  }
  qa_bot_ai_blocked_bots_a(ctx, state, &result, false);
  qa_bot_aim_at_enemy_bots_a(ctx, state);
  qa_bot_check_attack_bots_a(ctx, state);
  if ((state->src->flags & QA_BOT_FLAG_FIGHTSUICIDAL) == 0 && qa_bot_wants_to_retreat_bots_a(ctx, state)) {
    qa_ai_enter_battle_retreat_bots_a(ctx, state, "battle fight: wants to retreat");
    return true;
  }
  return true;
}

bool qa_ai_node_battle_chase_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (check_lifecycle(ctx, state, "battle chase", false))
    return false;
  if (state->src->enemy < 0) {
    qa_ai_enter_seek_ltg_bots_a(ctx, state, "battle chase: no enemy");
    return false;
  }
  if (qa_bot_entity_visible_bots_a(ctx, state->src->entity_num, qa_bot_v3(state->src->eye),
                                   qa_bot_v3(state->src->viewangles), 360, state->src->enemy) != 0) {
    qa_ai_enter_battle_fight_bots_a(ctx, state, "battle chase");
    return false;
  }
  if (qa_bot_find_enemy_bots_a(ctx, state, -1)) {
    qa_ai_enter_battle_fight_bots_a(ctx, state, "battle chase: better enemy");
    return false;
  }
  if (state->src->last_enemy_area_num == 0) {
    qa_ai_enter_seek_ltg_bots_a(ctx, state, "battle chase: no enemy area");
    return false;
  }
  set_travel_flags(ctx, state, true);
  qa_bot_map_scripts_bots_a(ctx, state);
  qa_bot_goal goal = {0};
  goal.entity = state->src->enemy;
  goal.area = state->src->last_enemy_area_num;
  goal.origin = qa_bot_v3(state->src->last_enemy_origin);
  goal.mins = qa_math_vec3(-8, -8, -8);
  goal.maxs = qa_math_vec3(8, 8, 8);
  if (qa_touching_goal(qa_bot_v3(state->src->origin), goal))
    state->src->chase_time = 0;
  if (state->src->chase_time == 0 || state->src->chase_time < AI_F(ctx->time - 10)) {
    qa_ai_enter_seek_ltg_bots_a(ctx, state, "battle chase: time out");
    return false;
  }
  if (state->src->check_time < ctx->time) {
    state->src->check_time = (float)(ctx->time + 1);
    double range = 150;
    if (qa_bot_nearby_goal_bots_a(ctx, state, state->src->tfl, &goal, range)) {
      state->src->nbg_time = (float)(AI_F(ctx->time + AI_F(AI_F(0.1) * range)) + 1);
      ai_move_reset_last_avoid_reach(ctx, state->src->ms);
      qa_ai_enter_battle_nbg_bots_a(ctx, state, "battle chase: nbg");
      return false;
    }
  }
  qa_bot_update_battle_inventory_bots_a(ctx, state, state->src->enemy);
  qa_bot_setup_for_movement_bots_a(ctx, state);
  qa_bot_move_result result = {0};
  ai_nav_move_to_goal(ctx, &result, state->src->ms, &goal, state->src->tfl);
  if (result.failure) {
    ai_move_reset_avoid_reach(ctx, state->src->ms);
    state->src->ltg_time = 0;
  }
  qa_bot_ai_blocked_bots_a(ctx, state, &result, false);
  if ((result.flags & MOVEMENT_VIEW) != 0) {
    set_ideal(state, result.ideal_view_angles);
  } else if ((state->src->flags & QA_BOT_FLAG_IDEALVIEWSET) == 0) {
    if (state->src->chase_time > AI_F(ctx->time - 2))
      qa_bot_aim_at_enemy_bots_a(ctx, state);
    else
      view_target_or_direction(ctx, state, &goal, &result);
    half_roll(state);
  }
  adopt_movement_weapon(ctx, state, &result);
  if (state->src->area_num == state->src->last_enemy_area_num)
    state->src->chase_time = 0;
  if (qa_bot_wants_to_retreat_bots_a(ctx, state)) {
    qa_ai_enter_battle_retreat_bots_a(ctx, state, "battle chase: wants to retreat");
    return true;
  }
  return true;
}

static void retreat_view(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, const qa_bot_goal *goal,
                         const qa_bot_move_result *result) {
  if ((result->flags & (QA_BOT_MRF_MOVEMENTVIEW | QA_BOT_MRF_SWIMVIEW)) != 0) {
    set_ideal(state, result->ideal_view_angles);
  } else if ((result->flags & QA_BOT_MRF_MOVEMENTVIEWSET) == 0 && (state->src->flags & QA_BOT_FLAG_IDEALVIEWSET) == 0) {
    double skill = ai_character_bounded(ctx, state->src->character, QA_BCHAR_ATTACK_SKILL);
    if (skill > AI_F(0.3)) {
      qa_bot_aim_at_enemy_bots_a(ctx, state);
    } else {
      view_target_or_direction(ctx, state, goal, result);
      half_roll(state);
    }
  }
  adopt_movement_weapon(ctx, state, result);
}

bool qa_ai_node_battle_retreat_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (check_lifecycle(ctx, state, "battle retreat", false))
    return false;
  if (state->src->enemy < 0) {
    qa_ai_enter_seek_ltg_bots_a(ctx, state, "battle retreat: no enemy");
    return false;
  }
  qa_aas_entity_info_bots_a info = qa_bot_entity_info_bots_a(ctx, state->src->enemy);
  if (qa_entity_is_dead_bots_a(ctx, &info)) {
    qa_ai_enter_seek_ltg_bots_a(ctx, state, "battle retreat: enemy dead");
    return false;
  }
  if (qa_bot_find_enemy_bots_a(ctx, state, state->src->enemy) && ai_debug_build(ctx))
    ai_print(ctx, "found new better enemy\n");
  set_travel_flags(ctx, state, false);
  qa_bot_map_scripts_bots_a(ctx, state);
  qa_bot_update_battle_inventory_bots_a(ctx, state, state->src->enemy);
  if (qa_bot_wants_to_chase_bots_a(ctx, state)) {
    qa_bot_goal_empty_goal_stack(ai_goals(ctx), state->src->gs);
    qa_ai_enter_battle_chase_bots_a(ctx, state, "battle retreat: wants to chase");
    return false;
  }
  if (qa_bot_entity_visible_bots_a(ctx, state->src->entity_num, qa_bot_v3(state->src->eye),
                                   qa_bot_v3(state->src->viewangles), 360, state->src->enemy) != 0) {
    state->src->enemy_visible_time = (float)ctx->time;
    remember_enemy_position(ctx, state, info.origin);
  }
  if (state->src->enemy_visible_time < AI_F(ctx->time - 4)) {
    qa_ai_enter_seek_ltg_bots_a(ctx, state, "battle retreat: lost enemy");
    return false;
  } else if (state->src->enemy_visible_time < ctx->time && qa_bot_find_enemy_bots_a(ctx, state, -1)) {
    qa_ai_enter_battle_fight_bots_a(ctx, state, "battle retreat: another enemy");
    return false;
  }
  qa_bot_team_goals_bots_a(ctx, state, true);
  qa_bot_battle_use_items_bots_a(ctx, state);
  qa_bot_goal goal = {0};
  if (!qa_bot_long_term_goal_bots_a(ctx, state, state->src->tfl, true, &goal)) {
    qa_ai_enter_battle_suicidal_fight_bots_a(ctx, state, "battle retreat: no way out");
    return false;
  }
  if (state->src->check_time < ctx->time) {
    state->src->check_time = (float)(ctx->time + 1);
    double range = objective_nearby_range(ctx, state, 150);
    if (qa_bot_nearby_goal_bots_a(ctx, state, state->src->tfl, &goal, range)) {
      ai_move_reset_last_avoid_reach(ctx, state->src->ms);
      state->src->nbg_time = (float)(AI_F(ctx->time + AI_F(range / 100)) + 1);
      qa_ai_enter_battle_nbg_bots_a(ctx, state, "battle retreat: nbg");
      return false;
    }
  }
  qa_bot_setup_for_movement_bots_a(ctx, state);
  qa_bot_move_result result = {0};
  ai_nav_move_to_goal(ctx, &result, state->src->ms, &goal, state->src->tfl);
  if (result.failure) {
    ai_move_reset_avoid_reach(ctx, state->src->ms);
    state->src->ltg_time = 0;
  }
  qa_bot_ai_blocked_bots_a(ctx, state, &result, false);
  qa_bot_choose_weapon_bots_a(ctx, state);
  retreat_view(ctx, state, &goal, &result);
  qa_bot_check_attack_bots_a(ctx, state);
  return true;
}

bool qa_ai_node_battle_nbg_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (check_lifecycle(ctx, state, "battle nbg", false))
    return false;
  if (state->src->enemy < 0) {
    qa_ai_enter_seek_nbg_bots_a(ctx, state, "battle nbg: no enemy");
    return false;
  }
  qa_aas_entity_info_bots_a info = qa_bot_entity_info_bots_a(ctx, state->src->enemy);
  if (qa_entity_is_dead_bots_a(ctx, &info)) {
    qa_ai_enter_seek_nbg_bots_a(ctx, state, "battle nbg: enemy dead");
    return false;
  }
  set_travel_flags(ctx, state, true);
  qa_bot_map_scripts_bots_a(ctx, state);
  if (qa_bot_entity_visible_bots_a(ctx, state->src->entity_num, qa_bot_v3(state->src->eye),
                                   qa_bot_v3(state->src->viewangles), 360, state->src->enemy) != 0) {
    state->src->enemy_visible_time = (float)ctx->time;
    remember_enemy_position(ctx, state, info.origin);
  }
  qa_bot_goal goal = {0}, top;
  if (!qa_bot_goal_get_top_goal(ai_goals(ctx), state->src->gs, &top)) {
    state->src->nbg_time = 0;
  } else {
    ai_goal_copy(&goal, &top);
    if (qa_bot_reached_goal_bots_a(ctx, state, &goal))
      state->src->nbg_time = 0;
  }
  if (state->src->nbg_time < ctx->time) {
    qa_bot_goal_pop_goal(ai_goals(ctx), state->src->gs);
    if (qa_bot_goal_get_top_goal(ai_goals(ctx), state->src->gs, &top))
      qa_ai_enter_battle_retreat_bots_a(ctx, state, "battle nbg: time out");
    else
      qa_ai_enter_battle_fight_bots_a(ctx, state, "battle nbg: time out");
    return false;
  }
  qa_bot_setup_for_movement_bots_a(ctx, state);
  qa_bot_move_result result = {0};
  ai_nav_move_to_goal(ctx, &result, state->src->ms, &goal, state->src->tfl);
  if (result.failure) {
    ai_move_reset_avoid_reach(ctx, state->src->ms);
    state->src->nbg_time = 0;
  }
  qa_bot_ai_blocked_bots_a(ctx, state, &result, false);
  qa_bot_update_battle_inventory_bots_a(ctx, state, state->src->enemy);
  qa_bot_choose_weapon_bots_a(ctx, state);
  retreat_view(ctx, state, &goal, &result);
  qa_bot_check_attack_bots_a(ctx, state);
  return true;
}

bool qa_run_ai_node_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state) {
  if (order_active(state) && state->ai_node != QA_AI_NODE_SEEK_LTG && state->ai_node != QA_AI_NODE_SEEK_ACTIVATE &&
      state->ai_node != QA_AI_NODE_RESPAWN && state->ai_node != QA_AI_NODE_OBSERVER &&
      state->ai_node != QA_AI_NODE_INTERMISSION)
    qa_ai_enter_seek_ltg_bots_a(ctx, state, "scripted order");
  switch (state->ai_node) {
  case QA_AI_NODE_INTERMISSION:
    return qa_ai_node_intermission_bots_a(ctx, state);
  case QA_AI_NODE_OBSERVER:
    return qa_ai_node_observer_bots_a(ctx, state);
  case QA_AI_NODE_STAND:
    return qa_ai_node_stand_bots_a(ctx, state);
  case QA_AI_NODE_RESPAWN:
    return qa_ai_node_respawn_bots_a(ctx, state);
  case QA_AI_NODE_SEEK_ACTIVATE:
    return qa_ai_node_seek_activate_entity_bots_a(ctx, state);
  case QA_AI_NODE_SEEK_NBG:
    return qa_ai_node_seek_nbg_bots_a(ctx, state);
  case QA_AI_NODE_SEEK_LTG:
    return qa_ai_node_seek_ltg_bots_a(ctx, state);
  case QA_AI_NODE_BATTLE_FIGHT:
    return qa_ai_node_battle_fight_bots_a(ctx, state);
  case QA_AI_NODE_BATTLE_CHASE:
    return qa_ai_node_battle_chase_bots_a(ctx, state);
  case QA_AI_NODE_BATTLE_RETREAT:
    return qa_ai_node_battle_retreat_bots_a(ctx, state);
  case QA_AI_NODE_BATTLE_NBG:
    return qa_ai_node_battle_nbg_bots_a(ctx, state);
  case QA_AI_NODE_NONE:
    break;
  }
  qa_range_error("BotDeathmatchAI must enter an AI node before dispatch");
}
