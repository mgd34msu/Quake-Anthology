/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/travel/ground.h"
#include "qa/core/math.h"
#include <math.h>
#include <string.h>
static qa_vec3 horiz(qa_vec3 from, qa_vec3 to) {
  return qa_math_vec3(to.x - from.x, to.y - from.y, 0);
}
static float fr(double v) { return (float)v; }
qa_bot_move_result qa_travel_walk_bots_a(qa_bot_travel_context_bots_a *c, qa_bot_move_state *s,
                                         qa_travel_reachability_bots_a r, bool waypoint) {
  qa_bot_move_result result;
  qa_bot_move_result_init(&result);
  qa_vec3 dir = horiz(s->origin, r.start);
  double dist = qa_length3(dir);
  dir = qa_normalize3(dir);
  if (!waypoint && c->check_blocked)
    c->check_blocked(c->ctx, s, dir, true, &result);
  if (dist < 10 || waypoint) {
    dir = horiz(s->origin, r.end);
    dist = qa_length3(dir);
    dir = qa_normalize3(dir);
  }
  if (waypoint && c->check_blocked)
    c->check_blocked(c->ctx, s, dir, true, &result);
  if (c->area_presence && (c->area_presence(c->ctx, r.area) & 2) == 0 && dist < 20)
    qa_bot_action_add_flag(c->actions, s->client, QA_BOT_ACTION_CROUCH);
  double gap = c->gap_distance ? c->gap_distance(c->ctx, s->origin, dir, s->entity_num) : 0;
  double speed;
  if (s->move_flags & QA_BOT_MF_WALK) {
    speed = gap > 0 ? (double)fr(200 - (double)fr(180 - (double)fr(1 * gap))) : 200;
    qa_bot_action_add_flag(c->actions, s->client, QA_BOT_ACTION_WALK);
  } else
    speed = gap > 0 ? (double)fr(400 - (double)fr(360 - (double)fr(2 * gap))) : 400;
  qa_bot_action_move(c->actions, s->client, dir, speed);
  result.move_direction = dir;
  return result;
}
qa_bot_move_result qa_finish_travel_walk_bots_a(qa_bot_travel_context_bots_a *c,
                                                qa_bot_move_state *s,
                                                qa_travel_reachability_bots_a r) {
  qa_bot_move_result result;
  qa_bot_move_result_init(&result);
  qa_vec3 dir = horiz(s->origin, r.end);
  double dist = qa_length3(dir);
  if (dist > 100)
    dist = 100;
  qa_vec3 norm = qa_normalize3(dir);
  qa_bot_action_move(c->actions, s->client, norm,
                     (double)fr(400 - (double)fr(400 - (double)fr(3 * dist))));
  result.move_direction = norm;
  return result;
}
qa_bot_move_result qa_travel_crouch_bots_a(qa_bot_travel_context_bots_a *c, qa_bot_move_state *s,
                                           qa_travel_reachability_bots_a r) {
  qa_bot_move_result result;
  qa_bot_move_result_init(&result);
  qa_vec3 dir = qa_normalize3(horiz(s->origin, r.end));
  if (c->check_blocked)
    c->check_blocked(c->ctx, s, dir, true, &result);
  qa_bot_action_add_flag(c->actions, s->client, QA_BOT_ACTION_CROUCH);
  qa_bot_action_move(c->actions, s->client, dir, 400);
  result.move_direction = dir;
  return result;
}
qa_bot_move_result qa_travel_barrier_jump_bots_a(qa_bot_travel_context_bots_a *c,
                                                 qa_bot_move_state *s,
                                                 qa_travel_reachability_bots_a r) {
  qa_bot_move_result result;
  qa_bot_move_result_init(&result);
  qa_vec3 hd = horiz(s->origin, r.start);
  double dist = qa_length3(hd);
  qa_vec3 dir = qa_normalize3(hd);
  if (c->check_blocked)
    c->check_blocked(c->ctx, s, dir, true, &result);
  if (dist < 9)
    qa_bot_action_jump(c->actions, s->client);
  else {
    double m = dist < 60 ? dist : 60;
    qa_bot_action_move(c->actions, s->client, dir,
                       (double)fr(360 - (double)fr(360 - (double)fr(6 * m))));
  }
  result.move_direction = dir;
  return result;
}
qa_bot_move_result qa_finish_travel_barrier_jump_bots_a(qa_bot_travel_context_bots_a *c,
                                                        qa_bot_move_state *s,
                                                        qa_travel_reachability_bots_a r) {
  qa_bot_move_result result;
  qa_bot_move_result_init(&result);
  if (s->velocity.z < 250) {
    qa_vec3 dir = qa_normalize3(horiz(s->origin, r.end));
    if (c->check_blocked)
      c->check_blocked(c->ctx, s, dir, true, &result);
    qa_bot_action_move(c->actions, s->client, dir, 400);
    result.move_direction = dir;
  }
  return result;
}
qa_bot_move_result qa_travel_swim_bots_a(qa_bot_travel_context_bots_a *c, qa_bot_move_state *s,
                                         qa_travel_reachability_bots_a r) {
  qa_bot_move_result result;
  qa_bot_move_result_init(&result);
  qa_vec3 dir = qa_normalize3(qa_sub3(r.start, s->origin));
  if (c->check_blocked)
    c->check_blocked(c->ctx, s, dir, true, &result);
  qa_bot_action_move(c->actions, s->client, dir, 400);
  result.move_direction = dir;
  result.ideal_view_angles =
      c->vector_to_angles_fn ? c->vector_to_angles_fn(c->ctx, dir) : qa_vector_to_angles(dir);
  result.flags |= QA_BOT_MRF_SWIMVIEW;
  return result;
}
qa_bot_move_result qa_travel_teleport_bots_a(qa_bot_travel_context_bots_a *c, qa_bot_move_state *s,
                                             qa_travel_reachability_bots_a r) {
  qa_bot_move_result result;
  qa_bot_move_result_init(&result);
  qa_vec3 dir = qa_normalize3(horiz(s->origin, r.start));
  if (c->check_blocked)
    c->check_blocked(c->ctx, s, dir, true, &result);
  qa_bot_action_move(c->actions, s->client, dir, 400);
  result.move_direction = dir;
  return result;
}
