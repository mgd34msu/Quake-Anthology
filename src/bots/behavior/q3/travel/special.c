/* Port of src/bots/behavior/q3/travel/special.ts. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/travel/special.h"
#include "qa/bots/behavior/library/actions.h"
#include "qa/bots/behavior/q3/navigation_types.h"
#include "qa/bots/behavior/q3/travel/routing.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static float fr(double v) { return (float)v; }
#define RSTART(r) ((r).reach.start)
#define REND(r) ((r).reach.end)
#define RFACE(r) ((r).reach.face)
#define REDGE(r) ((r).reach.edge)

static qa_bot_move_variable var_of(qa_bot_travel_context *c, qa_bot_movement_variable_name name) {
  return c->variable(c->ctx, name);
}
static qa_vec3 mover_bottom_center(qa_bot_travel_context *c, qa_travel_reachability reach) {
  qa_bot_travel_model model;
  if (!c->model_info(c->ctx, RFACE(reach) & 0xffff, &model))
    qa_range_error("Source mover has no shared entity/model binding");
  qa_vec3 middle = qa_add3(model.bounds.min, model.bounds.max);
  return qa_math_vec3(model.origin.x + 0.5 * middle.x, model.origin.y + 0.5 * middle.y, RSTART(reach).z);
}
qa_bot_move_result qa_travel_elevator(qa_bot_travel_context *c, qa_bot_move_state *s, qa_travel_reachability reach) {
  qa_bot_move_result result; qa_bot_move_result_init(&result);
  if (c->on_mover(c->ctx, s, reach)) {
    if (llabs((int64_t)trunc((double)fr(s->origin.z - REND(reach).z))) < var_of(c, QA_BMV_SV_MAX_BARRIER).value) {
      qa_vec3 direction = qa_normalize3(qa_math_vec3(REND(reach).x - s->origin.x, REND(reach).y - s->origin.y, 0));
      if (!c->check_barrier_jump(c->ctx, s, direction, 100)) qa_bot_action_move(c->actions, s->client, direction, 400);
      result.move_direction = direction;
    } else {
      qa_vec3 center = mover_bottom_center(c, reach);
      qa_vec3 offset = qa_math_vec3(center.x - s->origin.x, center.y - s->origin.y, 0);
      double distance = qa_length3(offset);
      qa_vec3 direction = qa_normalize3(offset);
      if (distance > 10) {
        if (distance > 100) distance = 100;
        qa_bot_action_move(c->actions, s->client, direction, (double)fr(400 - (double)fr(400 - (double)fr(4 * distance))));
        result.move_direction = direction;
      }
    }
    return result;
  }
  qa_vec3 direction = qa_sub3(REND(reach), s->origin);
  double distance = qa_length3(direction);
  if (distance < 64) {
    if (distance > 60) distance = 60;
    float speed = fr(360 - (double)fr(360 - (double)fr(6 * distance)));
    if ((s->move_flags & QA_BOT_MF_SWIMMING) || !c->check_barrier_jump(c->ctx, s, direction, 50)) {
      if (speed > 5) qa_bot_action_move(c->actions, s->client, direction, speed);
    }
    result.move_direction = direction;
    if (s->move_flags & QA_BOT_MF_SWIMMING) result.flags |= QA_BOT_MRF_SWIMVIEW;
    s->reachability_time = 0;
    return result;
  }
  qa_vec3 start_offset = qa_sub3(RSTART(reach), s->origin);
  qa_vec3 start_direction = (s->move_flags & QA_BOT_MF_SWIMMING) ? start_offset : qa_math_vec3(start_offset.x, start_offset.y, 0);
  double start_distance = qa_length3(start_direction);
  qa_vec3 first = qa_normalize3(start_direction);
  if (!c->mover_down(c->ctx, reach)) {
    distance = start_distance; direction = first;
    c->check_blocked(c->ctx, s, direction, false, &result);
    if (distance > 60) distance = 60;
    float speed = fr(360 - (double)fr(360 - (double)fr(6 * distance)));
    if (!(s->move_flags & QA_BOT_MF_SWIMMING) && !c->check_barrier_jump(c->ctx, s, direction, 50)) {
      if (speed > 5) qa_bot_action_move(c->actions, s->client, direction, speed);
    }
    result.move_direction = direction;
    if (s->move_flags & QA_BOT_MF_SWIMMING) result.flags |= QA_BOT_MRF_SWIMVIEW;
    result.type = QA_BOT_MRT_ELEVATORUP;
    result.flags |= QA_BOT_MRF_WAITING;
    return result;
  }
  qa_vec3 center_offset = qa_sub3(mover_bottom_center(c, reach), s->origin);
  qa_vec3 center_direction = (s->move_flags & QA_BOT_MF_SWIMMING) ? center_offset : qa_math_vec3(center_offset.x, center_offset.y, 0);
  double center_distance = qa_length3(center_direction);
  qa_vec3 second = qa_normalize3(center_direction);
  if (start_distance < 20 || center_distance < start_distance || qa_dot3(first, second) < 0) {
    distance = center_distance; direction = second;
  } else { distance = start_distance; direction = first; }
  c->check_blocked(c->ctx, s, direction, false, &result);
  if (distance > 60) distance = 60;
  float speed = fr(400 - (double)fr(400 - (double)fr(6 * distance)));
  if (!(s->move_flags & QA_BOT_MF_SWIMMING) && !c->check_barrier_jump(c->ctx, s, direction, 50))
    qa_bot_action_move(c->actions, s->client, direction, speed);
  result.move_direction = direction;
  if (s->move_flags & QA_BOT_MF_SWIMMING) result.flags |= QA_BOT_MRF_SWIMVIEW;
  return result;
}
qa_bot_move_result qa_finish_travel_elevator(qa_bot_travel_context *c, qa_bot_move_state *s, qa_travel_reachability reach) {
  qa_bot_move_result result; qa_bot_move_result_init(&result);
  qa_vec3 bottom = qa_sub3(mover_bottom_center(c, reach), s->origin);
  qa_vec3 top = qa_sub3(REND(reach), s->origin);
  qa_vec3 direction = fabs(bottom.z) < fabs(top.z) ? bottom : top;
  qa_bot_action_move(c->actions, s->client, qa_normalize3(direction), 300);
  return result;
}
typedef struct { qa_vec3 start, end, origin; bool has_origin; } BobRange;
static BobRange func_bob_start_end(qa_bot_travel_context *c, qa_travel_reachability reach) {
  int64_t model = RFACE(reach) & 0xffff;
  qa_bot_travel_model info;
  if (!c->model_info(c->ctx, model, &info)) {
    char buf[160];
    snprintf(buf, sizeof buf, "BotFuncBobStartEnd: no entity with model %lld\n", (long long)model);
    c->print(c->ctx, 1, buf);
    return (BobRange){qa_math_vec3(0, 0, 0), qa_math_vec3(0, 0, 0), qa_math_vec3(0, 0, 0), false};
  }
  qa_vec3 origin = info.origin;
  qa_bounds bounds = info.bounds;
  qa_vec3 sum = qa_add3(bounds.min, bounds.max);
  qa_vec3 middle = qa_math_vec3(sum.x * 0.5, sum.y * 0.5, sum.z * 0.5);
  int32_t face = (int32_t)RFACE(reach), edge = (int32_t)REDGE(reach);
  int64_t flags = face >> 16;
  int64_t first = edge >> 16, second = (int32_t)((uint32_t)edge << 16) >> 16;
  if (flags & 1) return (BobRange){qa_math_vec3((double)first, middle.y, middle.z), qa_math_vec3((double)second, middle.y, middle.z),
                                   qa_math_vec3(origin.x + middle.x, middle.y, middle.z), true};
  if (flags & 2) return (BobRange){qa_math_vec3(middle.x, (double)first, middle.z), qa_math_vec3(middle.x, (double)second, middle.z),
                                   qa_math_vec3(middle.x, origin.y + middle.y, middle.z), true};
  return (BobRange){qa_math_vec3(middle.x, middle.y, (double)first), qa_math_vec3(middle.x, middle.y, (double)second),
                    qa_math_vec3(middle.x, middle.y, origin.z + middle.z), true};
}
qa_bot_move_result qa_travel_func_bobbing(qa_bot_travel_context *c, qa_bot_move_state *s, qa_travel_reachability reach) {
  qa_bot_move_result result; qa_bot_move_result_init(&result);
  BobRange bob = func_bob_start_end(c, reach);
  if (c->on_mover(c->ctx, s, reach)) {
    if (!bob.has_origin) qa_range_error("BotTravel_FuncBobbing consumes an undefined mover origin");
    if (qa_length3(qa_sub3(bob.origin, bob.end)) < 24) {
      qa_vec3 direction = qa_normalize3(qa_math_vec3(REND(reach).x - s->origin.x, REND(reach).y - s->origin.y, 0));
      if (!c->check_barrier_jump(c->ctx, s, direction, 100)) qa_bot_action_move(c->actions, s->client, direction, 400);
      result.move_direction = direction;
    } else {
      qa_vec3 center = mover_bottom_center(c, reach);
      qa_vec3 offset = qa_math_vec3(center.x - s->origin.x, center.y - s->origin.y, 0);
      double distance = qa_length3(offset);
      qa_vec3 direction = qa_normalize3(offset);
      if (distance > 10) {
        if (distance > 100) distance = 100;
        qa_bot_action_move(c->actions, s->client, direction, (double)fr(400 - (double)fr(400 - (double)fr(4 * distance))));
        result.move_direction = direction;
      }
    }
    return result;
  }
  qa_vec3 direction = qa_sub3(REND(reach), s->origin);
  double distance = qa_length3(direction);
  if (distance < 64) {
    if (distance > 60) distance = 60;
    float speed = fr(360 - (double)fr(360 - (double)fr(6 * distance)));
    if ((s->move_flags & QA_BOT_MF_SWIMMING) || !c->check_barrier_jump(c->ctx, s, direction, 50)) {
      if (speed > 5) qa_bot_action_move(c->actions, s->client, direction, speed);
    }
    result.move_direction = direction;
    if (s->move_flags & QA_BOT_MF_SWIMMING) result.flags |= QA_BOT_MRF_SWIMVIEW;
    s->reachability_time = 0;
    return result;
  }
  qa_vec3 start_offset = qa_sub3(RSTART(reach), s->origin);
  qa_vec3 start_direction = (s->move_flags & QA_BOT_MF_SWIMMING) ? start_offset : qa_math_vec3(start_offset.x, start_offset.y, 0);
  double start_distance = qa_length3(start_direction);
  qa_vec3 first = qa_normalize3(start_direction);
  if (!bob.has_origin) qa_range_error("BotTravel_FuncBobbing consumes an undefined mover origin");
  if (qa_length3(qa_sub3(bob.origin, bob.start)) > 16) {
    distance = start_distance; direction = first;
    c->check_blocked(c->ctx, s, direction, false, &result);
    if (distance > 60) distance = 60;
    float speed = fr(360 - (double)fr(360 - (double)fr(6 * distance)));
    if (!(s->move_flags & QA_BOT_MF_SWIMMING) && !c->check_barrier_jump(c->ctx, s, direction, 50)) {
      if (speed > 5) qa_bot_action_move(c->actions, s->client, direction, speed);
    }
    result.move_direction = direction;
    if (s->move_flags & QA_BOT_MF_SWIMMING) result.flags |= QA_BOT_MRF_SWIMVIEW;
    result.type = QA_BOT_MRT_WAITFORFUNCBOBBING;
    result.flags |= QA_BOT_MRF_WAITING;
    return result;
  }
  qa_vec3 center_offset = qa_sub3(mover_bottom_center(c, reach), s->origin);
  qa_vec3 center_direction = (s->move_flags & QA_BOT_MF_SWIMMING) ? center_offset : qa_math_vec3(center_offset.x, center_offset.y, 0);
  double center_distance = qa_length3(center_direction);
  qa_vec3 second = qa_normalize3(center_direction);
  if (start_distance < 20 || center_distance < start_distance || qa_dot3(first, second) < 0) {
    distance = center_distance; direction = second;
  } else { distance = start_distance; direction = first; }
  c->check_blocked(c->ctx, s, direction, false, &result);
  if (distance > 60) distance = 60;
  float speed = fr(400 - (double)fr(400 - (double)fr(6 * distance)));
  if (!(s->move_flags & QA_BOT_MF_SWIMMING) && !c->check_barrier_jump(c->ctx, s, direction, 50))
    qa_bot_action_move(c->actions, s->client, direction, speed);
  result.move_direction = direction;
  if (s->move_flags & QA_BOT_MF_SWIMMING) result.flags |= QA_BOT_MRF_SWIMVIEW;
  return result;
}
qa_bot_move_result qa_finish_travel_func_bobbing(qa_bot_travel_context *c, qa_bot_move_state *s, qa_travel_reachability reach) {
  qa_bot_move_result result; qa_bot_move_result_init(&result);
  BobRange bob = func_bob_start_end(c, reach);
  if (!bob.has_origin) qa_range_error("BotFinishTravel_FuncBobbing consumes an undefined mover origin");
  qa_vec3 direction = qa_sub3(bob.origin, bob.end);
  if (qa_length3(direction) < 16) {
    qa_vec3 offset = qa_sub3(REND(reach), s->origin);
    double distance = qa_length3((s->move_flags & QA_BOT_MF_SWIMMING) ? offset : qa_math_vec3(offset.x, offset.y, 0));
    if (distance > 60) distance = 60;
    float speed = fr(360 - (double)fr(360 - (double)fr(6 * distance)));
    if (speed > 5) qa_bot_action_move(c->actions, s->client, direction, speed);
    result.move_direction = direction;
    if (s->move_flags & QA_BOT_MF_SWIMMING) result.flags |= QA_BOT_MRF_SWIMVIEW;
  } else {
    qa_vec3 offset = qa_sub3(mover_bottom_center(c, reach), s->origin);
    qa_vec3 horizontal = (s->move_flags & QA_BOT_MF_SWIMMING) ? offset : qa_math_vec3(offset.x, offset.y, 0);
    double distance = qa_length3(horizontal);
    qa_vec3 normalized = qa_normalize3(horizontal);
    if (distance > 5) {
      if (distance > 100) distance = 100;
      qa_bot_action_move(c->actions, s->client, normalized, (double)fr(400 - (double)fr(400 - (double)fr(4 * distance))));
      result.move_direction = normalized;
    }
  }
  return result;
}
static int grapple_state(qa_bot_travel_context *c, qa_bot_move_state *s) {
  if (s->move_flags & QA_BOT_MF_GRAPPLEPULL) return 2;
  int64_t grapple_weapon;
  if (!c->host->travel_weapon(c->host->ctx, s->client, "grapple", &grapple_weapon))
    grapple_weapon = (int64_t)trunc(var_of(c, QA_BMV_GRAPPLE_INDEX).value);
  for (int64_t entity = c->host->next_entity(c->host->ctx, 0); entity != 0; entity = c->host->next_entity(c->host->ctx, entity)) {
    if (c->host->entity_type(c->host->ctx, entity) == (int64_t)trunc(var_of(c, QA_BMV_MISSILE_ENTITY_TYPE).value)) {
      if (c->host->entity_weapon(c->host->ctx, entity) == grapple_weapon) return 1;
    }
  }
  return 0;
}
static void grapple_off(qa_bot_travel_context *c, qa_bot_move_state *s) {
  if (var_of(c, QA_BMV_OFFHAND_GRAPPLE).value != 0)
    qa_bot_action_command(c->actions, s->client, var_of(c, QA_BMV_GRAPPLE_OFF_COMMAND).string);
}
void qa_reset_grapple(qa_bot_travel_context *c, qa_bot_move_state *s) {
  qa_travel_reachability reach;
  bool has = c->reachability(c->ctx, s->last_reachability, &reach);
  if (!has || (reach.reach.travel_type & QA_TRAVEL_MASK) != QA_TRAVEL_GRAPPLEHOOK) {
    if ((s->move_flags & QA_BOT_MF_ACTIVEGRAPPLE) || s->grapple_visible_time != 0) {
      grapple_off(c, s);
      s->move_flags &= ~QA_BOT_MF_ACTIVEGRAPPLE;
      s->grapple_visible_time = 0;
    }
  }
}
qa_bot_move_result qa_travel_grapple(qa_bot_travel_context *c, qa_bot_move_state *s, qa_travel_reachability reach) {
  qa_bot_move_result result; qa_bot_move_result_init(&result);
  if (s->move_flags & QA_BOT_MF_GRAPPLERESET) {
    grapple_off(c, s);
    s->move_flags &= ~QA_BOT_MF_ACTIVEGRAPPLE;
    return result;
  }
  if ((int64_t)trunc(var_of(c, QA_BMV_OFFHAND_GRAPPLE).value) == 0) {
    result.weapon = c->weapon(c->ctx, s->client, "grapple");
    result.flags |= QA_BOT_MRF_MOVEMENTWEAPON;
  }
  if (s->move_flags & QA_BOT_MF_ACTIVEGRAPPLE) {
    int hook_state = grapple_state(c, s);
    double distance = qa_length3(qa_math_vec3(REND(reach).x - s->origin.x, REND(reach).y - s->origin.y, 0));
    if (hook_state != 0 && distance < 48) {
      if (fr((double)s->last_grapple_distance - distance) < 1) {
        grapple_off(c, s);
        s->move_flags &= ~QA_BOT_MF_ACTIVEGRAPPLE;
        s->move_flags |= QA_BOT_MF_GRAPPLERESET;
        s->reachability_time = 0;
        return result;
      }
    } else if (hook_state == 0 || (hook_state == 2 && distance > fr((double)s->last_grapple_distance - 2))) {
      if (s->grapple_visible_time < fr(c->time(c->ctx)) - 0.4f) {
        grapple_off(c, s);
        s->move_flags &= ~QA_BOT_MF_ACTIVEGRAPPLE;
        s->move_flags |= QA_BOT_MF_GRAPPLERESET;
        s->reachability_time = 0;
        return result;
      }
    } else {
      s->grapple_visible_time = fr(c->time(c->ctx));
    }
    if ((int64_t)trunc(var_of(c, QA_BMV_OFFHAND_GRAPPLE).value) == 0)
      qa_bot_action_add_flag(c->actions, s->client, QA_BOT_ACTION_ATTACK);
    s->last_grapple_distance = (float)distance;
  } else {
    s->grapple_visible_time = fr(c->time(c->ctx));
    qa_vec3 offset = qa_sub3(RSTART(reach), s->origin);
    qa_vec3 move_offset = (s->move_flags & QA_BOT_MF_SWIMMING) ? offset : qa_math_vec3(offset.x, offset.y, 0);
    qa_vec3 eye = qa_add3(s->origin, s->view_offset);
    qa_vec3 view_direction = qa_sub3(REND(reach), eye);
    double distance = qa_length3(move_offset);
    qa_vec3 direction = qa_normalize3(move_offset);
    result.ideal_view_angles = c->vector_to_angles(c->ctx, view_direction);
    result.flags |= QA_BOT_MRF_MOVEMENTVIEW;
    if (distance < 5 && fabsf(qa_movement_angle_difference(result.ideal_view_angles.x, s->view_angles.x)) < 2 &&
        fabsf(qa_movement_angle_difference(result.ideal_view_angles.y, s->view_angles.y)) < 2) {
      qa_bot_server_trace_result trace = c->host->trace(c->host->ctx, eye, REND(reach), NULL, s->entity_num, 1);
      if (qa_length3(qa_sub3(REND(reach), trace.end)) > 16) { result.failure = true; return result; }
      if (var_of(c, QA_BMV_OFFHAND_GRAPPLE).value != 0)
        qa_bot_action_command(c->actions, s->client, var_of(c, QA_BMV_GRAPPLE_ON_COMMAND).string);
      else qa_bot_action_add_flag(c->actions, s->client, QA_BOT_ACTION_ATTACK);
      s->move_flags |= QA_BOT_MF_ACTIVEGRAPPLE;
      s->last_grapple_distance = 999999;
    } else {
      float speed = distance < 70 ? fr(300 - (double)fr(300 - (double)fr(4 * distance))) : 400;
      c->check_blocked(c->ctx, s, direction, true, &result);
      qa_bot_action_move(c->actions, s->client, direction, speed);
      result.move_direction = direction;
    }
    int64_t area = c->point_area(c->ctx, s->origin);
    if (area != 0 && area != s->reach_area) s->reachability_time = 0;
  }
  return result;
}
static qa_bot_move_result weapon_jump(qa_bot_travel_context *c, qa_bot_move_state *s, qa_travel_reachability reach, const char *mode) {
  qa_bot_move_result result; qa_bot_move_result_init(&result);
  qa_vec3 offset = qa_math_vec3(RSTART(reach).x - s->origin.x, RSTART(reach).y - s->origin.y, 0);
  double distance = qa_length3(offset);
  qa_vec3 direction = qa_normalize3(offset);
  qa_vec3 initial = c->vector_to_angles(c->ctx, direction);
  result.ideal_view_angles = qa_math_vec3(90, initial.y, initial.z);
  if (distance < 5 && fabsf(qa_movement_angle_difference(result.ideal_view_angles.x, s->view_angles.x)) < 5 &&
      fabsf(qa_movement_angle_difference(result.ideal_view_angles.y, s->view_angles.y)) < 5) {
    direction = qa_normalize3(qa_math_vec3(REND(reach).x - s->origin.x, REND(reach).y - s->origin.y, 0));
    qa_bot_action_jump(c->actions, s->client);
    qa_bot_action_add_flag(c->actions, s->client, QA_BOT_ACTION_ATTACK);
    qa_bot_action_move(c->actions, s->client, direction, 800);
    s->jump_reach = s->last_reachability;
  } else {
    if (distance > 80) distance = 80;
    qa_bot_action_move(c->actions, s->client, direction, (double)fr(400 - (double)fr(400 - (double)fr(5 * distance))));
  }
  qa_vec3 angles = c->vector_to_angles(c->ctx, direction);
  result.ideal_view_angles = qa_math_vec3(90, angles.y, angles.z);
  qa_bot_action_view(c->actions, s->client, result.ideal_view_angles);
  result.flags |= QA_BOT_MRF_MOVEMENTVIEWSET;
  qa_bot_action_select_weapon(c->actions, s->client, c->weapon(c->ctx, s->client, mode));
  result.weapon = c->weapon(c->ctx, s->client, mode);
  result.flags |= QA_BOT_MRF_MOVEMENTWEAPON;
  result.move_direction = direction;
  return result;
}
qa_bot_move_result qa_travel_rocket_jump(qa_bot_travel_context *c, qa_bot_move_state *s, qa_travel_reachability reach) {
  return weapon_jump(c, s, reach, "rocket-jump");
}
qa_bot_move_result qa_travel_bfg_jump(qa_bot_travel_context *c, qa_bot_move_state *s, qa_travel_reachability reach) {
  return weapon_jump(c, s, reach, "bfg-jump");
}
qa_bot_move_result qa_finish_travel_weapon_jump(qa_bot_travel_context *c, qa_bot_move_state *s, qa_travel_reachability reach) {
  qa_bot_move_result result; qa_bot_move_result_init(&result);
  if (s->jump_reach == 0) return result;
  qa_bot_travel_air_control control = c->air_control(c->ctx, s, REND(reach));
  qa_vec3 direction = control.controlled ? control.direction
                                          : qa_normalize3(qa_math_vec3(REND(reach).x - s->origin.x, REND(reach).y - s->origin.y, 0));
  qa_bot_action_move(c->actions, s->client, direction, control.controlled ? control.speed : 400);
  result.move_direction = direction;
  return result;
}
