/* See header. Active native botlib branches use float32 stores and double
 * literal promotion; every fr() is a TS Math.fround. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/travel/ground.h"
#include "qa/bots/behavior/library/actions.h"
#include "qa/core/math.h"
#include <math.h>
#include <string.h>
#include "qa/foundation/js.h"

static double fr(double v) { return (double)(float)v; }
#define ORIGIN(s) qa_bot_v3((s)->src->origin)
#define START(r) ((r).reach.start)
#define END(r) ((r).reach.end)
static qa_vec3 horizontal(qa_vec3 from, qa_vec3 to) {
  return qa_math_vec3(to.x - from.x, to.y - from.y, 0);
}
static qa_vec3 ma(qa_vec3 origin, double distance, qa_vec3 direction) {
  return qa_add3(origin, qa_scale3(direction, distance));
}
static double centered_random(qa_bot_travel_context *c) {
  double value = (double)(c->host->random.next_int(c->host->random.ctx) & 0x7fff);
  return 2.0 * (fr(value / 0x7fff) - 0.5);
}
static qa_bot_move_result empty(void) {
  qa_bot_move_result result;
  qa_bot_move_result_init(&result);
  return result;
}

qa_bot_move_result qa_travel_walk(qa_bot_travel_context *c, qa_bot_move_state *s,
                                  qa_travel_reachability r) {
  qa_bot_move_result result = empty();
  qa_vec3 direction = horizontal(ORIGIN(s), START(r));
  double distance = qa_length3(direction);
  direction = qa_normalize3(direction);
  bool waypoint = r.graph_edge.source.kind != QA_NS_AAS;
  if (!waypoint)
    c->check_blocked(c->ctx, s, direction, true, &result);
  if (waypoint && s->has_walk_progress && s->walk_edge_id != r.graph_edge.id)
    s->has_walk_progress = false;
  if (waypoint && distance < 10) {
    s->has_walk_progress = true;
    s->walk_edge_id = r.graph_edge.id;
  }
  if (distance < 10 || (waypoint && s->has_walk_progress)) {
    direction = horizontal(ORIGIN(s), END(r));
    distance = qa_length3(direction);
    direction = qa_normalize3(direction);
  }
  if (waypoint)
    c->check_blocked(c->ctx, s, direction, true, &result);
  if ((c->area_presence(c->ctx, r.reach.area) & 2) == 0 && distance < 20)
    qa_bot_action_crouch(c->actions, s->src->client);
  double gap = c->gap_distance(c->ctx, ORIGIN(s), direction, s->src->entity_num);
  double speed;
  if ((s->src->move_flags & QA_BOT_MF_WALK) != 0) {
    speed = gap > 0 ? fr(200 - fr(180 - fr(1 * gap))) : 200;
    qa_bot_action_walk(c->actions, s->src->client);
  } else
    speed = gap > 0 ? fr(400 - fr(360 - fr(2 * gap))) : 400;
  qa_bot_action_move(c->actions, s->src->client, direction, speed);
  result.move_direction = direction;
  return result;
}
qa_bot_move_result qa_finish_travel_walk(qa_bot_travel_context *c, qa_bot_move_state *s,
                                         qa_travel_reachability r) {
  qa_bot_move_result result = empty();
  qa_vec3 direction = horizontal(ORIGIN(s), END(r));
  double distance = qa_js_min(qa_length3(direction), 100);
  qa_vec3 normalized = qa_normalize3(direction);
  qa_bot_action_move(c->actions, s->src->client, normalized, fr(400 - fr(400 - fr(3 * distance))));
  result.move_direction = normalized;
  return result;
}
qa_bot_move_result qa_travel_crouch(qa_bot_travel_context *c, qa_bot_move_state *s,
                                    qa_travel_reachability r) {
  qa_bot_move_result result = empty();
  qa_vec3 direction = qa_normalize3(horizontal(ORIGIN(s), END(r)));
  c->check_blocked(c->ctx, s, direction, true, &result);
  qa_bot_action_crouch(c->actions, s->src->client);
  qa_bot_action_move(c->actions, s->src->client, direction, 400);
  result.move_direction = direction;
  return result;
}
qa_bot_move_result qa_travel_barrier_jump(qa_bot_travel_context *c, qa_bot_move_state *s,
                                          qa_travel_reachability r) {
  qa_bot_move_result result = empty();
  qa_vec3 offset = horizontal(ORIGIN(s), START(r));
  double distance = qa_length3(offset);
  qa_vec3 direction = qa_normalize3(offset);
  c->check_blocked(c->ctx, s, direction, true, &result);
  if (distance < 9)
    qa_bot_action_jump(c->actions, s->src->client);
  else
    qa_bot_action_move(c->actions, s->src->client, direction,
                       fr(360 - fr(360 - fr(6 * qa_js_min(distance, 60)))));
  result.move_direction = direction;
  return result;
}
qa_bot_move_result qa_finish_travel_barrier_jump(qa_bot_travel_context *c, qa_bot_move_state *s,
                                                 qa_travel_reachability r) {
  qa_bot_move_result result = empty();
  if (s->src->velocity.z < 250) {
    qa_vec3 direction = qa_normalize3(horizontal(ORIGIN(s), END(r)));
    c->check_blocked(c->ctx, s, direction, true, &result);
    qa_bot_action_move(c->actions, s->src->client, direction, 400);
    result.move_direction = direction;
  }
  return result;
}
qa_bot_move_result qa_travel_swim(qa_bot_travel_context *c, qa_bot_move_state *s,
                                  qa_travel_reachability r) {
  qa_bot_move_result result = empty();
  qa_vec3 direction = qa_normalize3(qa_sub3(START(r), ORIGIN(s)));
  c->check_blocked(c->ctx, s, direction, true, &result);
  qa_bot_action_move(c->actions, s->src->client, direction, 400);
  result.move_direction = direction;
  result.ideal_view_angles = c->vector_to_angles(c->ctx, direction);
  result.flags |= QA_BOT_MRF_SWIMVIEW;
  return result;
}
qa_bot_move_result qa_travel_water_jump(qa_bot_travel_context *c, qa_bot_move_state *s,
                                        qa_travel_reachability r) {
  qa_bot_move_result result = empty();
  qa_vec3 offset = qa_sub3(END(r), ORIGIN(s));
  double horizontal_distance = qa_length3(qa_math_vec3(offset.x, offset.y, 0));
  qa_vec3 direction =
      qa_normalize3(qa_math_vec3(offset.x, offset.y, offset.z + (15 + centered_random(c) * 40)));
  qa_bot_action_move_forward(c->actions, s->src->client);
  if (horizontal_distance < 40)
    qa_bot_action_move_up(c->actions, s->src->client);
  result.ideal_view_angles = c->vector_to_angles(c->ctx, direction);
  result.flags |= QA_BOT_MRF_MOVEMENTVIEW;
  result.move_direction = direction;
  return result;
}
qa_bot_move_result qa_finish_travel_water_jump(qa_bot_travel_context *c, qa_bot_move_state *s,
                                               qa_travel_reachability r) {
  qa_bot_move_result result = empty();
  if ((s->src->move_flags & QA_BOT_MF_WATERJUMP) != 0)
    return result;
  qa_vec3 origin = ORIGIN(s);
  if ((c->point_contents(c->ctx, qa_math_vec3(origin.x, origin.y, origin.z - 32)) & 56) == 0)
    return result;
  qa_vec3 offset = qa_sub3(END(r), origin);
  double x = fr(offset.x + centered_random(c) * 10);
  double y = fr(offset.y + centered_random(c) * 10);
  double z = fr(offset.z + (70 + centered_random(c) * 10));
  qa_vec3 direction = qa_normalize3(qa_math_vec3(x, y, z));
  qa_bot_action_move(c->actions, s->src->client, direction, 400);
  result.ideal_view_angles = c->vector_to_angles(c->ctx, direction);
  result.flags |= QA_BOT_MRF_MOVEMENTVIEW;
  result.move_direction = direction;
  return result;
}
qa_bot_move_result qa_travel_walk_off_ledge(qa_bot_travel_context *c, qa_bot_move_state *s,
                                            qa_travel_reachability r) {
  qa_bot_move_result result = empty();
  c->check_blocked(c->ctx, s, qa_normalize3(qa_sub3(START(r), ORIGIN(s))), true, &result);
  double reach_distance = qa_length3(horizontal(START(r), END(r)));
  qa_vec3 start_direction = horizontal(ORIGIN(s), START(r));
  double distance = qa_length3(start_direction), speed;
  qa_vec3 direction = qa_normalize3(start_direction);
  if (distance < 48) {
    direction = qa_normalize3(horizontal(ORIGIN(s), END(r)));
    if (reach_distance < 20)
      speed = 100;
    else {
      qa_bot_travel_jump_speed velocity = c->jump_speed(c->ctx, s, START(r), END(r), 0);
      speed = velocity.success ? velocity.velocity : 400;
    }
  } else
    speed = reach_distance < 20 ? fr(400 - fr(256 - fr(4 * qa_js_min(distance, 64)))) : 400;
  c->check_blocked(c->ctx, s, direction, true, &result);
  qa_bot_action_move(c->actions, s->src->client, direction, speed);
  result.move_direction = direction;
  return result;
}
qa_bot_move_result qa_finish_travel_walk_off_ledge(qa_bot_travel_context *c, qa_bot_move_state *s,
                                                   qa_travel_reachability r) {
  qa_bot_move_result result = empty();
  qa_vec3 direction = qa_sub3(END(r), ORIGIN(s));
  c->check_blocked(c->ctx, s, direction, true, &result);
  qa_vec3 horizontal_direction = horizontal(ORIGIN(s), END(r));
  double distance = qa_length3(horizontal_direction);
  qa_vec3 end = distance > 16 ? ma(END(r), 16, qa_normalize3(horizontal_direction)) : END(r);
  qa_bot_travel_air_control control = c->air_control(c->ctx, s, end);
  qa_vec3 move_direction = control.controlled
                               ? control.direction
                               : qa_normalize3(qa_math_vec3(direction.x, direction.y, 0));
  qa_bot_action_move(c->actions, s->src->client, move_direction,
                     control.controlled ? control.speed : 400);
  result.move_direction = move_direction;
  return result;
}
/* The third Jump body is the active source implementation; the first two are comments. */
qa_bot_move_result qa_travel_jump(qa_bot_travel_context *c, qa_bot_move_state *s,
                                  qa_travel_reachability r) {
  qa_bot_move_result result = empty();
  qa_vec3 run_start = c->jump_run_start(c->ctx, s, r);
  qa_vec3 direction = qa_normalize3(horizontal(START(r), run_start));
  if (r.graph_edge.source.kind == QA_NS_AAS) {
    qa_vec3 start = qa_math_vec3(START(r).x, START(r).y, START(r).z + 1);
    run_start = ma(START(r), 80, direction);
    double scan_distance = 0;
    for (; scan_distance < 80; scan_distance += 10) {
      qa_vec3 end = ma(start, scan_distance + 10, direction);
      if (c->point_area(c->ctx, qa_math_vec3(end.x, end.y, end.z + 1)) != s->src->reach_area)
        break;
    }
    if (scan_distance < 80)
      run_start = ma(START(r), scan_distance, direction);
  }
  qa_vec3 from_start = horizontal(START(r), ORIGIN(s));
  qa_vec3 from_run_start = horizontal(run_start, ORIGIN(s));
  double start_distance = qa_length3(from_start), run_distance = qa_length3(from_run_start);
  if (qa_dot3(qa_normalize3(from_start), qa_normalize3(from_run_start)) < -0.8 || run_distance < 5) {
    direction = qa_normalize3(horizontal(ORIGIN(s), END(r)));
    if (start_distance < 24)
      qa_bot_action_jump(c->actions, s->src->client);
    else if (start_distance < 32)
      qa_bot_action_delayed_jump(c->actions, s->src->client);
    qa_bot_action_move(c->actions, s->src->client, direction, 600);
    s->src->jump_reach = s->src->last_reachability;
  } else {
    direction = qa_normalize3(horizontal(ORIGIN(s), run_start));
    qa_bot_action_move(c->actions, s->src->client, direction,
                       fr(400 - fr(400 - fr(5 * qa_js_min(run_distance, 80)))));
  }
  result.move_direction = direction;
  return result;
}
qa_bot_move_result qa_finish_travel_jump(qa_bot_travel_context *c, qa_bot_move_state *s,
                                         qa_travel_reachability r) {
  qa_bot_move_result result = empty();
  if (s->src->jump_reach == 0)
    return result;
  qa_vec3 offset = horizontal(ORIGIN(s), END(r));
  double distance = qa_length3(offset);
  qa_vec3 direction = qa_normalize3(offset);
  if (qa_dot3(direction, qa_normalize3(horizontal(START(r), END(r)))) < -0.5 && distance < 24)
    return result;
  qa_bot_action_move(c->actions, s->src->client, direction, 800);
  result.move_direction = direction;
  return result;
}
qa_bot_move_result qa_travel_ladder(qa_bot_travel_context *c, qa_bot_move_state *s,
                                    qa_travel_reachability r) {
  qa_bot_move_result result = empty();
  qa_vec3 direction = qa_normalize3(qa_sub3(END(r), ORIGIN(s)));
  result.ideal_view_angles =
      c->vector_to_angles(c->ctx, qa_math_vec3(direction.x, direction.y, 3 * direction.z));
  qa_bot_action_move(c->actions, s->src->client, qa_math_vec3(0, 0, 0), 0);
  qa_bot_action_move_forward(c->actions, s->src->client);
  result.flags |= QA_BOT_MRF_MOVEMENTVIEW;
  result.move_direction = direction;
  return result;
}
qa_bot_move_result qa_travel_teleport(qa_bot_travel_context *c, qa_bot_move_state *s,
                                      qa_travel_reachability r) {
  qa_bot_move_result result = empty();
  if ((s->src->move_flags & QA_BOT_MF_TELEPORTED) != 0)
    return result;
  bool swimming = (s->src->move_flags & QA_BOT_MF_SWIMMING) != 0;
  qa_vec3 offset = swimming ? qa_sub3(START(r), ORIGIN(s)) : horizontal(ORIGIN(s), START(r));
  double distance = qa_length3(offset);
  qa_vec3 direction = qa_normalize3(offset);
  c->check_blocked(c->ctx, s, direction, true, &result);
  qa_bot_action_move(c->actions, s->src->client, direction, distance < 30 ? 200 : 400);
  if (swimming)
    result.flags |= QA_BOT_MRF_SWIMVIEW;
  result.move_direction = direction;
  return result;
}
qa_bot_move_result qa_travel_jump_pad(qa_bot_travel_context *c, qa_bot_move_state *s,
                                      qa_travel_reachability r) {
  qa_bot_move_result result = empty();
  qa_vec3 direction = qa_normalize3(horizontal(ORIGIN(s), START(r)));
  c->check_blocked(c->ctx, s, direction, true, &result);
  qa_bot_action_move(c->actions, s->src->client, direction, 400);
  result.move_direction = direction;
  return result;
}
qa_bot_move_result qa_finish_travel_jump_pad(qa_bot_travel_context *c, qa_bot_move_state *s,
                                             qa_travel_reachability r) {
  qa_bot_move_result result = empty();
  qa_bot_travel_air_control control = c->air_control(c->ctx, s, END(r));
  qa_vec3 direction =
      control.controlled ? control.direction : qa_normalize3(horizontal(ORIGIN(s), END(r)));
  c->check_blocked(c->ctx, s, direction, true, &result);
  qa_bot_action_move(c->actions, s->src->client, direction, control.controlled ? control.speed : 400);
  result.move_direction = direction;
  return result;
}
