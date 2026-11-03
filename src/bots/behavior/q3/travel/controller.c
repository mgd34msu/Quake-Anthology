/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/travel/controller.h"
#include "qa/bots/behavior/library/actions.h"
#include "qa/bots/behavior/q3/navigation_types.h"
#include "qa/bots/behavior/q3/travel/special.h"
#include "qa/bots/navigation/aas.h"
#include "qa/bots/navigation/runtime.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/bots/navigation/graph.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"

enum { SOLID = 1, WATER = 32, PLAYERCLIP = 0x10000, BODY = 0x2000000 };
/* botlib AAS_InitSettings default for the phys_maxvelocity libvar. */
#define PHYS_MAX_VELOCITY 320
static double fr(double v) { return (double)(float)v; }
static noreturn void fail(const char *format, const char *text) {
  fprintf(stderr, format, text);
  fputc('\n', stderr);
  abort();
}
static qa_vec3 ma(qa_vec3 origin, double distance, qa_vec3 direction) {
  return qa_add3(origin, qa_scale3(direction, distance));
}
static qa_vec3 with_z(qa_vec3 v, double z) { return qa_math_vec3(v.x, v.y, z); }
static bool clear(const qa_bot_server_trace_result *t) { return strcmp(t->solidity, "clear") == 0; }
static double reachability_time(const qa_travel_reachability *r) {
  switch (r->reach.travel_type & QA_TRAVEL_MASK) {
  case QA_TRAVEL_WALK: case QA_TRAVEL_CROUCH: case QA_TRAVEL_BARRIERJUMP:
  case QA_TRAVEL_WALKOFFLEDGE: case QA_TRAVEL_JUMP: case QA_TRAVEL_SWIM:
  case QA_TRAVEL_WATERJUMP: case QA_TRAVEL_TELEPORT:
    return 5;
  case QA_TRAVEL_LADDER: case QA_TRAVEL_ROCKETJUMP: case QA_TRAVEL_BFGJUMP:
    return 6;
  case QA_TRAVEL_ELEVATOR: case QA_TRAVEL_JUMPPAD: case QA_TRAVEL_FUNCBOB:
    return 10;
  default:
    return 8;
  }
}

/* One decision borrows the current actor runtime; all persistent state stays in BotMoveState. */
typedef struct travel_step {
  qa_source_bot_travel_host *host;
  qa_bot_move_state *state;
  qa_travel_graph graph;
  qa_bot_travel_context context;
} travel_step;
static const qa_navigation_profile *profile_of(const travel_step *t) {
  return &t->graph.runtime->graph->profile;
}
static bool capable(const travel_step *t, qa_travel_mode mode) {
  return (profile_of(t)->capabilities & (1u << mode)) != 0;
}
static qa_bounds bounds_for(travel_step *t, int64_t presence) {
  return t->host->presence_bounds(t->host->ctx, presence == 2 ? 2 : 4);
}
static qa_bot_server_trace_result host_trace(travel_step *t, qa_vec3 start, qa_vec3 end,
                                             const qa_bounds *bounds, int64_t entity, int64_t mask) {
  return t->host->trace(t->host->ctx, start, end, bounds, entity, mask);
}
static qa_bot_server_trace_result trace_box(travel_step *t, qa_vec3 start, qa_vec3 end,
                                            int64_t presence, int64_t entity) {
  qa_bounds bounds = bounds_for(t, presence);
  return host_trace(t, start, end, &bounds, entity, SOLID | PLAYERCLIP);
}

/* ---- BotTravelContext members ---- */
static qa_bot_move_variable ctx_variable(void *ctx, qa_bot_movement_variable_name name) {
  travel_step *t = ctx;
  qa_bot_move_state_store *m = t->host->move_states;
  qa_bot_lib_var *const cells[QA_BMV_COUNT] = {
      m->sv_max_step, m->sv_max_barrier, m->sv_gravity, m->rocket_launcher_index, m->bfg_index,
      m->grapple_index, m->missile_entity_type, m->offhand_grapple, m->grapple_on_command,
      m->grapple_off_command};
  if (cells[name] == NULL)
    fail("Source bot travel variable %s requires move-state setup",
         qa_bot_movement_variable_name_string(name));
  qa_bot_move_variable out = {qa_bot_lib_var_string(cells[name]), qa_bot_lib_var_value(cells[name])};
  return out;
}
static double variable_value(travel_step *t, qa_bot_movement_variable_name name) {
  return ctx_variable(t, name).value;
}
static double ctx_time(void *ctx) {
  travel_step *t = ctx;
  return t->host->time(t->host->ctx);
}
static void ctx_print(void *ctx, int64_t severity, const char *text) {
  travel_step *t = ctx;
  t->host->move_states->host.print(t->host->move_states->host.ctx, (int)severity, text);
}
static int64_t ctx_area_presence(void *ctx, int64_t area) {
  travel_step *t = ctx;
  int64_t presence = t->host->area(t->host->ctx, area).presence_type;
  if (t->graph.offset == 0 || presence != 0)
    return presence;
  const qa_navigation_node *node =
      qa_navigation_runtime_node(t->graph.runtime, qa_travel_graph_node(&t->graph, area));
  return node != NULL && node->source.kind == QA_NS_NAV3 && (node->flags & 512) != 0 ? 4 : 2;
}
static int64_t ctx_point_area(void *ctx, qa_vec3 point) {
  travel_step *t = ctx;
  return t->host->point_area(t->host->ctx, point);
}
static int64_t ctx_point_contents(void *ctx, qa_vec3 point) {
  travel_step *t = ctx;
  return t->host->point_contents(t->host->ctx, point);
}
static bool ctx_reachability(void *ctx, int64_t number, qa_travel_reachability *out) {
  return qa_travel_graph_reach(&((travel_step *)ctx)->graph, number, out);
}
static bool ctx_model_info(void *ctx, int64_t model, qa_bot_travel_model *out) {
  travel_step *t = ctx;
  return t->host->model_info(t->host->ctx, model, out);
}
static bool ctx_on_mover(void *ctx, qa_bot_move_state *state, qa_travel_reachability reach) {
  travel_step *t = ctx;
  qa_bot_travel_model model;
  if (!ctx_model_info(t, reach.reach.face & 0xffff, &model))
    return false;
  qa_bounds b = profile_of(t)->shape.bounds;
  qa_vec3 o = qa_bot_v3(state->src->origin);
  if (o.x > model.origin.x + model.bounds.max.x - b.min.x ||
      o.x < model.origin.x + model.bounds.min.x - b.max.x ||
      o.y > model.origin.y + model.bounds.max.y - b.min.y ||
      o.y < model.origin.y + model.bounds.min.y - b.max.y)
    return false;
  qa_bounds flat = b;
  flat.min.z = -8;
  flat.max.z = 8;
  qa_bot_server_trace_result trace = host_trace(t, with_z(o, o.z + 24), with_z(o, o.z - 48), &flat,
                                                state->src->entity_num, SOLID | PLAYERCLIP);
  return clear(&trace) && trace.entity_num != 1023 &&
         t->host->entity_model_index(t->host->ctx, (int64_t)trace.entity_num) ==
             (reach.reach.face & 0xffff);
}
static bool ctx_mover_down(void *ctx, qa_travel_reachability reach) {
  qa_bot_travel_model model;
  return ctx_model_info(ctx, reach.reach.face & 0xffff, &model) &&
         fr(model.origin.z + model.bounds.max.z) < reach.reach.start.z;
}
static int64_t ctx_weapon(void *ctx, int64_t client, const char *mode) {
  travel_step *t = ctx;
  int64_t weapon = 0;
  if (!t->host->travel_weapon(t->host->ctx, client, mode, &weapon))
    fail("Selected arsenal cannot execute %s", mode);
  return weapon;
}
static qa_vec3 ctx_vector_to_angles(void *ctx, qa_vec3 direction) {
  (void)ctx;
  return qa_vector_to_angles_with_rule(direction, QA_VECTOR_TO_ANGLES_BOT_Q3);
}
static double ctx_gap_distance(void *ctx, qa_vec3 origin, qa_vec3 direction, int64_t entity) {
  travel_step *t = ctx;
  qa_bot_server_trace_result trace = trace_box(t, origin, with_z(origin, origin.z - 60), 4, entity);
  if (trace.fraction >= 1)
    return 1;
  double start_z = fr(trace.end.z + 1);
  for (int distance = 8; distance <= 100; distance += 8) {
    qa_vec3 across = ma(origin, distance, direction);
    qa_vec3 start = qa_math_vec3(across.x, across.y, start_z + 24);
    trace = trace_box(t, start, with_z(start, start.z - fr(48 + variable_value(t, QA_BMV_SV_MAX_BARRIER))),
                      4, entity);
    if (clear(&trace)) {
      if (trace.end.z < fr(fr(start_z - variable_value(t, QA_BMV_SV_MAX_STEP)) - 8)) {
        if ((t->host->point_contents(t->host->ctx, with_z(trace.end, trace.end.z - 20)) & WATER) != 0)
          break;
        return distance;
      }
      start_z = trace.end.z;
    }
  }
  return 0;
}
static bool ctx_check_barrier_jump(void *ctx, qa_bot_move_state *state, qa_vec3 direction,
                                   double speed) {
  travel_step *t = ctx;
  if (!capable(t, QA_TM_JUMP))
    return false;
  qa_vec3 o = qa_bot_v3(state->src->origin);
  int64_t entity = state->src->entity_num;
  double step = variable_value(t, QA_BMV_SV_MAX_STEP);
  qa_bot_server_trace_result trace =
      trace_box(t, o, with_z(o, o.z + variable_value(t, QA_BMV_SV_MAX_BARRIER)), 2, entity);
  if (!clear(&trace) || fr(trace.end.z - o.z) < step)
    return false;
  qa_vec3 horizontal = qa_normalize3(qa_math_vec3(direction.x, direction.y, 0));
  double distance = fr((double)state->src->think_time * speed) * 0.5;
  qa_vec3 end = qa_math_vec3(o.x + distance * horizontal.x, o.y + distance * horizontal.y, trace.end.z);
  trace = trace_box(t, trace.end, end, 2, entity);
  if (!clear(&trace))
    return false;
  trace = trace_box(t, trace.end, with_z(trace.end, o.z), 2, entity);
  if (!clear(&trace) || trace.fraction >= 1 || fr(trace.end.z - o.z) < step)
    return false;
  if (t->graph.runtime->graph->aas_asset == NULL) {
    const qa_navigation_world *world = t->graph.runtime->world;
    qa_navigation_route_prediction route = world->begin_route(world->ctx, *profile_of(t));
    qa_traversal_request request;
    memset(&request, 0, sizeof request);
    request.from = o;
    request.to = trace.end;
    request.mode = QA_TM_JUMP;
    qa_traversal_admission admitted = route.admit(route.ctx, request);
    bool ok = admitted.admitted;
    qa_traversal_admission_destroy(&admitted);
    if (!ok)
      return false;
  }
  qa_bot_action_jump(t->host->actions, state->src->client);
  qa_bot_action_move(t->host->actions, state->src->client, horizontal, speed);
  state->src->move_flags |= QA_BOT_MF_BARRIERJUMP;
  return true;
}
static void ctx_check_blocked(void *ctx, qa_bot_move_state *state, qa_vec3 direction, bool bottom,
                              qa_bot_move_result *result) {
  travel_step *t = ctx;
  qa_bot_move_record *r = state->src;
  qa_vec3 o = qa_bot_v3(r->origin);
  qa_bounds bounds = bounds_for(t, r->presence_type);
  if (fabs(direction.z) < 0.7) {
    bounds.min.z += variable_value(t, QA_BMV_SV_MAX_STEP);
    bounds.max.z -= 10;
  }
  qa_bot_server_trace_result trace =
      host_trace(t, o, ma(o, 3, direction), &bounds, r->entity_num, SOLID | PLAYERCLIP | BODY);
  if (clear(&trace) && trace.entity_num != 1022 && trace.entity_num != 1023) {
    result->blocked = true;
    result->block_entity = (int64_t)trace.entity_num;
  } else if (bottom && t->host->area(t->host->ctx, r->area).reachable_area_count == 0) {
    bounds = bounds_for(t, r->presence_type);
    trace = host_trace(t, o, with_z(o, o.z - 3), &bounds, r->entity_num, SOLID | PLAYERCLIP);
    if (clear(&trace) && trace.entity_num != 1022 && trace.entity_num != 1023) {
      result->blocked = true;
      result->block_entity = (int64_t)trace.entity_num;
      result->flags |= QA_BOT_MRF_ONTOPOFOBSTACLE;
    }
  }
}
static qa_bot_movement_prediction prediction_query(const qa_bot_move_state *state, qa_vec3 origin) {
  qa_bot_movement_prediction q;
  memset(&q, 0, sizeof q);
  q.entity_num = state->src->entity_num;
  q.origin = origin;
  q.frame_time = fr(0.1);
  return q;
}
static qa_vec3 ctx_jump_run_start(void *ctx, qa_bot_move_state *state, qa_travel_reachability reach) {
  travel_step *t = ctx;
  qa_vec3 s = reach.reach.start, e = reach.reach.end;
  qa_vec3 direction = qa_normalize3(qa_math_vec3(s.x - e.x, s.y - e.y, 0));
  qa_vec3 start = with_z(s, s.z + 1);
  qa_bot_movement_prediction q = prediction_query(state, start);
  q.presence = 2;
  q.on_ground = true;
  q.command_move = qa_scale3(direction, 400);
  q.command_frames = 1;
  q.max_frames = 2;
  q.stop_events = 4 | 8 | 16 | 32 | 64;
  qa_bot_travel_prediction_result result = t->host->predict(t->host->ctx, q);
  return ((int64_t)result.stop_event & (8 | 16 | 32)) != 0 ? start : result.end;
}
/* AAS_HorizontalVelocityForJump: closed form, failing above phys_maxvelocity. */
static qa_bot_travel_jump_speed ctx_jump_speed(void *ctx, qa_bot_move_state *state, qa_vec3 start,
                                               qa_vec3 end, double initial_vertical_velocity) {
  travel_step *t = ctx;
  (void)state; /* TS _state: the closed form reads no state. */
  qa_bot_travel_jump_speed fail_result = {false, PHYS_MAX_VELOCITY};
  double gravity = variable_value(t, QA_BMV_SV_GRAVITY), ascent = fr(initial_vertical_velocity / gravity);
  double height = fr(fr(start.z + fr(fr(0.5 * gravity) * ascent * ascent)) - end.z);
  if (height < 0)
    return fail_result;
  double time = fr(sqrt(fr(height / fr(0.5 * gravity)))), total = fr(time + ascent);
  if (total == 0)
    return fail_result;
  double dx = fr(end.x - start.x), dy = fr(end.y - start.y);
  double velocity = fr(fr(sqrt(fr(fr(dx * dx) + fr(dy * dy)))) / total);
  if (velocity > PHYS_MAX_VELOCITY)
    return fail_result;
  qa_bot_travel_jump_speed out = {true, velocity};
  return out;
}
/* BotAirControl integrates free flight: no collision, 0.1 s of velocity per step. */
static qa_bot_travel_air_control ctx_air_control(void *ctx, qa_bot_move_state *state, qa_vec3 goal) {
  travel_step *t = ctx;
  double gravity = variable_value(t, QA_BMV_SV_GRAVITY);
  qa_vec3 org = qa_bot_v3(state->src->origin), vel = qa_scale3(qa_bot_v3(state->src->velocity), 0.1);
  qa_bot_travel_air_control out;
  for (int index = 0; index < 50; index++) {
    vel = qa_math_vec3(vel.x, vel.y, fr(vel.z - gravity * 0.01));
    if (vel.z < 0 && fr(org.z + vel.z) < goal.z) {
      vel = qa_scale3(vel, fr(fr(goal.z - org.z) / vel.z));
      org = qa_add3(org, vel);
      qa_vec3 direction = qa_sub3(goal, org);
      double distance = qa_js_min(qa_length3(direction), 32);
      out.controlled = true;
      out.direction = qa_normalize3(direction);
      out.speed = fr(400 - fr(400 - fr(13 * distance)));
      return out;
    }
    org = qa_add3(org, vel);
  }
  out.controlled = false;
  out.direction = qa_math_vec3(0, 0, 0);
  out.speed = 400;
  return out;
}
static void step_init(travel_step *t, qa_source_bot_travel_host *host, qa_bot_move_state *state) {
  memset(t, 0, sizeof *t);
  t->host = host;
  t->state = state;
  qa_travel_graph_init(&t->graph, host->runtime(host->ctx, state->src->client));
  t->graph.time = fr(host->time(host->ctx));
  qa_bot_travel_context *c = &t->context;
  c->host = host;
  c->actions = host->actions;
  c->ctx = t;
  c->variable = ctx_variable;
  c->time = ctx_time;
  c->print = ctx_print;
  c->area_presence = ctx_area_presence;
  c->point_area = ctx_point_area;
  c->point_contents = ctx_point_contents;
  c->reachability = ctx_reachability;
  c->model_info = ctx_model_info;
  c->on_mover = ctx_on_mover;
  c->mover_down = ctx_mover_down;
  c->weapon = ctx_weapon;
  c->vector_to_angles = ctx_vector_to_angles;
  c->gap_distance = ctx_gap_distance;
  c->check_barrier_jump = ctx_check_barrier_jump;
  c->check_blocked = ctx_check_blocked;
  c->jump_run_start = ctx_jump_run_start;
  c->jump_speed = ctx_jump_speed;
  c->air_control = ctx_air_control;
}

/* ---- TravelStep private members ---- */
static bool on_ground(travel_step *t) {
  qa_bot_move_record *r = t->state->src;
  qa_vec3 o = qa_bot_v3(r->origin);
  qa_bot_server_trace_result trace =
      trace_box(t, o, with_z(o, o.z - 10), r->presence_type, r->entity_num);
  return clear(&trace) && trace.fraction < 1 && fr(o.z - trace.end.z) <= 10 && trace.has_plane &&
         trace.plane.normal.z >= profile_of(t)->minimum_floor_normal;
}
static bool against_ladder(travel_step *t) {
  if (!capable(t, QA_TM_LADDER))
    return false;
  const qa_navigation_graph *graph = t->graph.runtime->graph;
  const qa_aas_asset *asset = graph->aas_asset;
  qa_vec3 origin = qa_bot_v3(t->state->src->origin);
  if (asset == NULL) {
    const qa_navigation_world *world = t->graph.runtime->world;
    qa_point_contents_query query;
    memset(&query, 0, sizeof query);
    query.point = origin;
    query.policy = graph->profile.policy;
    query.numeric = graph->profile.movement.numeric;
    query.pass_actor = world->pass_actor;
    qa_point_contents_result result = world->scene.point_contents(world->scene.ctx, query);
    return strcmp(result.kind, "q2") == 0 && ((int64_t)result.merged & 0x20000000) != 0;
  }
  static const double OFFSETS[5][2] = {{0, 0}, {1, 0}, {1, 1}, {-1, 1}, {-1, -1}};
  int64_t number = 0;
  for (int i = 0; i < 5; i++) {
    number = ctx_point_area(t, qa_add3(origin, qa_math_vec3(OFFSETS[i][0], OFFSETS[i][1], 0)));
    if (number != 0)
      break;
  }
  if (number == 0 || (size_t)number >= asset->area_count || (size_t)number >= asset->setting_count)
    return false;
  const qa_aas_area_rec *area = &asset->areas[number];
  const qa_aas_setting_rec *settings = &asset->settings[number];
  if ((settings->flags & 2) == 0 || (settings->presence & 2) == 0)
    return false;
  for (int64_t index = area->first_face; index < area->first_face + area->face_count; index++) {
    if (index < 0 || (size_t)index >= asset->face_index_count)
      fail("%s", "Missing source ladder face index");
    int64_t signed_face = asset->face_indexes[index];
    int64_t absolute = signed_face < 0 ? -signed_face : signed_face;
    if ((size_t)absolute >= asset->face_count)
      fail("%s", "Missing source ladder face");
    const qa_aas_face_rec *face = &asset->faces[absolute];
    if ((face->flags & 2) == 0)
      continue;
    int64_t plane_index = face->plane ^ (signed_face < 0 ? 1 : 0);
    if (plane_index < 0 || (size_t)plane_index >= asset->plane_count || face->plane < 0 ||
        (size_t)face->plane >= asset->plane_count)
      fail("%s", "Missing source ladder face plane");
    const qa_aas_plane_rec *plane = &asset->planes[plane_index];
    const qa_aas_plane_rec *edge_plane = &asset->planes[face->plane];
    if (fabs(trunc(fr(qa_dot3(plane->normal, origin) - plane->distance))) >= 3)
      continue;
    bool inside = true;
    for (int64_t e = face->first_edge; e < face->first_edge + face->edge_count; e++) {
      if (e < 0 || (size_t)e >= asset->edge_index_count)
        fail("%s", "Missing source ladder edge index");
      int64_t signed_edge = asset->edge_indexes[e];
      int64_t edge_abs = signed_edge < 0 ? -signed_edge : signed_edge;
      if ((size_t)edge_abs >= asset->edge_count)
        fail("%s", "Missing source ladder edge");
      const qa_aas_edge_rec *edge = &asset->edges[edge_abs];
      int64_t a = edge->vertices[signed_edge < 0 ? 1 : 0], b = edge->vertices[signed_edge < 0 ? 0 : 1];
      if (a < 0 || b < 0 || (size_t)a >= asset->vertex_count || (size_t)b >= asset->vertex_count)
        fail("%s", "Missing source ladder vertex");
      qa_vec3 first = asset->vertices[a], second = asset->vertices[b];
      if (qa_dot3(qa_sub3(origin, first), qa_cross3(qa_sub3(second, first), edge_plane->normal)) <
          -fr(0.1)) {
        inside = false;
        break;
      }
    }
    if (inside)
      return true;
  }
  return false;
}
static int64_t on_top_of_entity(travel_step *t) {
  qa_bot_move_record *r = t->state->src;
  qa_vec3 o = qa_bot_v3(r->origin);
  qa_bot_server_trace_result trace =
      trace_box(t, o, with_z(o, o.z - 3), r->presence_type, r->entity_num);
  return clear(&trace) && trace.entity_num != 1022 && trace.entity_num != 1023
             ? (int64_t)trace.entity_num
             : -1;
}
static bool step_move_in_direction(travel_step *t, qa_vec3 direction, double speed, int64_t type) {
  qa_bot_move_state *state = t->state;
  qa_bot_move_record *r = state->src;
  qa_bot_action_buffer *actions = t->host->actions;
  speed = fr(speed);
  qa_vec3 origin = qa_bot_v3(r->origin);
  if (t->host->swimming(t->host->ctx, origin)) {
    if (!capable(t, QA_TM_SWIM))
      return false;
    qa_bot_action_move(actions, r->client, qa_normalize3(direction), speed);
    return true;
  }
  if (on_ground(t))
    r->move_flags |= QA_BOT_MF_ONGROUND;
  if ((r->move_flags & QA_BOT_MF_ONGROUND) != 0) {
    if (ctx_check_barrier_jump(t, state, direction, speed))
      return true;
    r->move_flags &= ~QA_BOT_MF_BARRIERJUMP;
    int64_t presence = (type & QA_BOT_MOVE_CROUCH) != 0 && (type & QA_BOT_MOVE_JUMP) == 0 ? 4 : 2;
    qa_vec3 horizontal = qa_normalize3(qa_math_vec3(direction.x, direction.y, 0));
    if ((type & QA_BOT_MOVE_JUMP) == 0 && ctx_gap_distance(t, origin, horizontal, r->entity_num) > 0)
      type |= QA_BOT_MOVE_JUMP;
    bool jumping = (type & QA_BOT_MOVE_JUMP) != 0, crouching = (type & QA_BOT_MOVE_CROUCH) != 0;
    if ((jumping && !capable(t, QA_TM_JUMP)) || (crouching && !capable(t, QA_TM_CROUCH)))
      return false;
    qa_vec3 move = qa_scale3(horizontal, speed);
    int64_t maximum_frames = jumping ? 30 : 2;
    qa_bot_movement_prediction q = prediction_query(state, with_z(origin, origin.z + 0.5));
    q.presence = presence;
    q.on_ground = true;
    q.velocity = qa_bot_v3(r->velocity);
    q.command_move = jumping ? with_z(move, 400) : move;
    q.command_frames = jumping ? 1 : 2;
    q.max_frames = maximum_frames;
    q.stop_events = (jumping ? 1 : 0) | 32 | 4 | 8 | 16;
    qa_bot_travel_prediction_result prediction = t->host->predict(t->host->ctx, q);
    int64_t stop = (int64_t)prediction.stop_event;
    if ((jumping && prediction.frames >= (double)maximum_frames) || (stop & (8 | 16 | 32)) != 0)
      return false;
    if ((stop & 1) != 0 &&
        (ctx_gap_distance(t, prediction.end, qa_normalize3(prediction.velocity), r->entity_num) > 0 ||
         ctx_gap_distance(t, prediction.end, horizontal, r->entity_num) > 0))
      return false;
    if (qa_length3(qa_math_vec3(prediction.end.x - origin.x, prediction.end.y - origin.y, 0)) <
        fr(speed * (double)r->think_time) * 0.5)
      return false;
    if (jumping)
      qa_bot_action_jump(actions, r->client);
    if (crouching)
      qa_bot_action_crouch(actions, r->client);
    qa_bot_action_move(actions, r->client, horizontal, speed);
    return true;
  }
  if ((r->move_flags & QA_BOT_MF_BARRIERJUMP) != 0 && r->velocity.z < 50)
    qa_bot_action_move(actions, r->client, direction, speed);
  return true;
}
static qa_bot_move_result move_in_goal_area(travel_step *t, const qa_bot_goal *goal) {
  qa_bot_move_state *state = t->state;
  qa_bot_move_record *r = state->src;
  qa_bot_move_result result;
  qa_bot_move_result_init(&result);
  bool swimming = (r->move_flags & QA_BOT_MF_SWIMMING) != 0;
  qa_vec3 o = qa_bot_v3(r->origin);
  qa_vec3 delta =
      qa_math_vec3(goal->origin.x - o.x, goal->origin.y - o.y, swimming ? goal->origin.z - o.z : 0);
  result.travel_type = swimming ? QA_TRAVEL_SWIM : QA_TRAVEL_WALK;
  qa_vec3 direction = qa_normalize3(delta);
  double distance = qa_js_min(qa_length3(delta), 100);
  double speed = fr(400 - fr(400 - fr(4 * distance)));
  if (speed < 10)
    speed = 0;
  ctx_check_blocked(t, state, direction, true, &result);
  qa_bot_action_move(t->host->actions, r->client, direction, speed);
  result.move_direction = direction;
  if (swimming) {
    result.ideal_view_angles = ctx_vector_to_angles(t, direction);
    result.flags |= QA_BOT_MRF_SWIMVIEW;
  }
  qa_bot_move_state_set_last_reachability(state, 0);
  r->last_area = 0;
  r->last_goal_area = (int32_t)goal->area;
  r->last_origin = r->origin;
  return result;
}
/* TS travel; false is its null return. */
static bool travel(travel_step *t, qa_travel_reachability reach, bool airborne,
                   qa_bot_move_result *out) {
  qa_bot_travel_context *c = &t->context;
  qa_bot_move_state *state = t->state;
  qa_navigation_runtime *rt = t->graph.runtime;
  qa_travel_mode mode = reach.graph_edge.mode;
  qa_bot_move_result_init(out);
  if (!capable(t, mode)) {
    out->failure = true;
    return true;
  }
  bool weapon_required =
      mode == QA_TM_ROCKET_JUMP || mode == QA_TM_BFG_JUMP ||
      (mode == QA_TM_GRAPPLE && trunc(variable_value(t, QA_BMV_OFFHAND_GRAPPLE)) == 0);
  int64_t weapon = 0;
  if (weapon_required && !t->host->travel_weapon(t->host->ctx, state->src->client,
                                                 qa_travel_mode_name(mode), &weapon)) {
    out->failure = true;
    return true;
  }
  qa_navigation_boarding_elevator boarding;
  switch (reach.reach.travel_type & QA_TRAVEL_MASK) {
  case QA_TRAVEL_WALK:
    if (!airborne && qa_navigation_runtime_boarding_elevator(rt, reach.graph_edge.to, &boarding) &&
        boarding.phase != 0) {
      qa_travel_reachability elevator = qa_travel_graph_describe(&t->graph, boarding.edge);
      if (!ctx_on_mover(t, state, elevator) && !ctx_mover_down(t, elevator)) {
        elevator.reach.start = reach.reach.start;
        *out = qa_travel_elevator(c, state, elevator);
        return true;
      }
    }
    *out = qa_travel_walk(c, state, reach);
    return true;
  case QA_TRAVEL_CROUCH:
    if (airborne)
      return false;
    *out = qa_travel_crouch(c, state, reach);
    return true;
  case QA_TRAVEL_BARRIERJUMP:
    *out = airborne ? qa_finish_travel_barrier_jump(c, state, reach)
                    : qa_travel_barrier_jump(c, state, reach);
    return true;
  case QA_TRAVEL_LADDER:
    *out = qa_travel_ladder(c, state, reach);
    return true;
  case QA_TRAVEL_WALKOFFLEDGE:
    *out = airborne ? qa_finish_travel_walk_off_ledge(c, state, reach)
                    : qa_travel_walk_off_ledge(c, state, reach);
    return true;
  case QA_TRAVEL_JUMP:
    *out = airborne ? qa_finish_travel_jump(c, state, reach) : qa_travel_jump(c, state, reach);
    return true;
  case QA_TRAVEL_SWIM:
    *out = qa_travel_swim(c, state, reach);
    return true;
  case QA_TRAVEL_WATERJUMP:
    *out = airborne ? qa_finish_travel_water_jump(c, state, reach)
                    : qa_travel_water_jump(c, state, reach);
    return true;
  case QA_TRAVEL_TELEPORT:
    if (airborne)
      return false;
    *out = qa_travel_teleport(c, state, reach);
    return true;
  case QA_TRAVEL_ELEVATOR: {
    qa_travel_reachability selected = reach;
    if (qa_navigation_runtime_boarding_elevator(rt, reach.graph_edge.from, &boarding) &&
        boarding.phase != 0 && !ctx_on_mover(t, state, reach) && !ctx_mover_down(t, reach) &&
        reach.graph_edge.has_hint)
      selected.reach.start = reach.graph_edge.hint.funnel;
    *out = airborne ? qa_finish_travel_elevator(c, state, selected)
                    : qa_travel_elevator(c, state, selected);
    return true;
  }
  case QA_TRAVEL_GRAPPLEHOOK:
    *out = qa_travel_grapple(c, state, reach);
    return true;
  case QA_TRAVEL_ROCKETJUMP:
    *out = airborne ? qa_finish_travel_weapon_jump(c, state, reach)
                    : qa_travel_rocket_jump(c, state, reach);
    return true;
  case QA_TRAVEL_BFGJUMP:
    *out = airborne ? qa_finish_travel_weapon_jump(c, state, reach)
                    : qa_travel_bfg_jump(c, state, reach);
    return true;
  case QA_TRAVEL_JUMPPAD:
    *out = airborne ? qa_finish_travel_jump_pad(c, state, reach) : qa_travel_jump_pad(c, state, reach);
    return true;
  case QA_TRAVEL_FUNCBOB: {
    qa_bot_travel_model model;
    if (!ctx_model_info(t, reach.reach.face & 0xffff, &model)) {
      /* BotFuncBobStartEnd prints and returns; the port ends the reachability
       * instead of consuming its undefined origin. */
      char text[96];
      snprintf(text, sizeof text, "BotFuncBobStartEnd: no entity with model %lld\n",
               (long long)(reach.reach.face & 0xffff));
      ctx_print(t, 1, text);
      out->failure = true;
      state->src->reachability_time = 0;
      return true;
    }
    *out = airborne ? qa_finish_travel_func_bobbing(c, state, reach)
                    : qa_travel_func_bobbing(c, state, reach);
    return true;
  }
  default:
    fprintf(stderr, "Source bot travel %lld has no implemented action routine\n",
            (long long)(reach.reach.travel_type & QA_TRAVEL_MASK));
    abort();
  }
}
static double now(travel_step *t) { return fr(t->host->time(t->host->ctx)); }
static void step_move_to_goal(travel_step *t, qa_bot_move_result *result, const qa_bot_goal *goal,
                              int64_t travel_flags) {
  qa_bot_move_state *state = t->state;
  qa_bot_move_record *r = state->src;
  qa_source_bot_travel_host *host = t->host;
  qa_reset_grapple(&t->context, state);
  if (goal == NULL) {
    result->failure = true;
    return;
  }
  qa_vec3 origin = qa_bot_v3(r->origin);
  r->move_flags &= ~(QA_BOT_MF_SWIMMING | QA_BOT_MF_AGAINSTLADDER);
  if (on_ground(t))
    r->move_flags |= QA_BOT_MF_ONGROUND;
  if ((r->move_flags & QA_BOT_MF_ONGROUND) != 0) {
    int64_t entity = on_top_of_entity(t);
    if (entity != -1) {
      int64_t model_number = host->entity_model_index(host->ctx, entity);
      qa_bot_travel_model model;
      bool has_model = ctx_model_info(t, model_number, &model);
      const char *kind = has_model ? model.kind : "";
      bool elevator = strcmp(kind, "elevator") == 0, bobbing = strcmp(kind, "bobbing") == 0;
      if (elevator || bobbing) {
        int64_t type = elevator ? QA_TRAVEL_ELEVATOR : QA_TRAVEL_FUNCBOB;
        qa_travel_reachability prior;
        if (!qa_travel_graph_reach(&t->graph, r->last_reachability, &prior) ||
            (prior.reach.travel_type & QA_TRAVEL_MASK) != type ||
            (prior.reach.face & 0xffff) != model_number) {
          const qa_navigation_edge *edge =
              qa_navigation_runtime_mover_edge(t->graph.runtime, model_number);
          if (edge == NULL) {
            result->blocked = true;
            result->block_entity = entity;
            result->flags |= QA_BOT_MRF_ONTOPOFOBSTACLE;
            return;
          }
          qa_bot_move_state_set_last_reachability(state, qa_travel_graph_handle(&t->graph, edge));
          qa_travel_reachability described = qa_travel_graph_describe(&t->graph, edge);
          r->reachability_time = (float)(now(t) + reachability_time(&described));
        }
        result->flags |= elevator ? QA_BOT_MRF_ONTOPOF_ELEVATOR : QA_BOT_MRF_ONTOPOF_FUNCBOB;
      } else if (strcmp(kind, "door") == 0 || strcmp(kind, "static") == 0) {
        r->area = (int32_t)host->fuzzy_point_reachability_area(host->ctx, origin);
        if (host->area(host->ctx, r->area).reachable_area_count == 0) {
          result->blocked = true;
          result->block_entity = entity;
          result->flags |= QA_BOT_MRF_ONTOPOFOBSTACLE;
          return;
        }
      } else {
        result->blocked = true;
        result->block_entity = entity;
        result->flags |= QA_BOT_MRF_ONTOPOFOBSTACLE;
        return;
      }
    }
  }
  if (host->swimming(host->ctx, origin))
    r->move_flags |= QA_BOT_MF_SWIMMING;
  if (against_ladder(t))
    r->move_flags |= QA_BOT_MF_AGAINSTLADDER;
  if ((r->move_flags & (QA_BOT_MF_ONGROUND | QA_BOT_MF_SWIMMING | QA_BOT_MF_AGAINSTLADDER)) != 0) {
    r->area = (int32_t)host->fuzzy_point_reachability_area(host->ctx, origin);
    qa_travel_reachability riding;
    if (qa_travel_graph_reach(&t->graph, r->last_reachability, &riding) &&
        riding.graph_edge.source.kind == QA_NS_NAV3 && riding.graph_edge.source_travel_type == QA_KEX_TRAVEL_ELEVATOR &&
        ctx_on_mover(t, state, riding)) {
      r->area = (int32_t)qa_travel_graph_area(&t->graph, riding.graph_edge.from);
      r->reachability_time = (float)(now(t) + 5);
    }
    if (r->area == 0) {
      result->failure = true;
      result->blocked = true;
      result->block_entity = 0;
      result->type = QA_BOT_MRT_INSOLIDAREA;
      return;
    }
    if (r->area == goal->area) {
      *result = move_in_goal_area(t, goal);
      return;
    }
    int64_t number = r->last_reachability;
    qa_travel_reachability prior;
    if (!qa_travel_graph_reach(&t->graph, number, &prior))
      number = 0;
    else {
      int64_t type = prior.reach.travel_type & QA_TRAVEL_MASK;
      if ((qa_travel_flag_for_type(prior.reach.travel_type) & travel_flags) == 0 ||
          !(t->graph.runtime->graph->aas_asset != NULL
                ? qa_navigation_runtime_edge_selectable(t->graph.runtime, &prior.graph_edge, false, 0)
                : qa_navigation_runtime_edge_allowed(t->graph.runtime, &prior.graph_edge, NULL)))
        number = 0;
      else if (type == QA_TRAVEL_GRAPPLEHOOK) {
        if (r->reachability_time < now(t) || (r->move_flags & QA_BOT_MF_GRAPPLERESET) != 0)
          number = 0;
      } else if (type == QA_TRAVEL_ELEVATOR || type == QA_TRAVEL_FUNCBOB) {
        if ((result->flags & QA_BOT_MRF_ONTOPOF_FUNCBOB) != 0)
          r->reachability_time = (float)(now(t) + 5);
        if (r->area == prior.reach.area || r->reachability_time < now(t))
          number = 0;
      } else if (r->last_goal_area != goal->area || r->reachability_time < now(t) ||
                 r->last_area != r->area)
        number = 0;
    }
    int64_t result_flags = 0;
    if (number == 0) {
      state->has_walk_progress = false;
      qa_travel_selection selected =
          qa_travel_graph_select(&t->graph, state, goal, travel_flags, travel_flags);
      number = selected.reachability;
      result_flags = selected.flags;
      r->reach_area = r->area;
      r->jump_reach = 0;
      r->move_flags &= ~QA_BOT_MF_GRAPPLERESET;
      qa_travel_reachability reach;
      if (qa_travel_graph_reach(&t->graph, number, &reach)) {
        r->reachability_time = (float)(now(t) + reachability_time(&reach));
        qa_bot_move_state_store_add_to_avoid_reach(host->move_states, state, number, 6);
      }
    }
    qa_bot_move_state_set_last_reachability(state, number);
    r->last_goal_area = (int32_t)goal->area;
    r->last_area = r->area;
    if (!qa_travel_graph_reach(&t->graph, number, &prior)) {
      result->failure = true;
      result->flags |= result_flags;
    } else {
      qa_bot_move_result moved;
      if (travel(t, prior, false, &moved))
        *result = moved;
      result->travel_type = prior.reach.travel_type;
      result->flags |= result_flags;
    }
  } else {
    qa_vec3 end = ma(origin, fr(-2 * (double)r->think_time), qa_bot_v3(r->velocity));
    int64_t areas[16];
    qa_vec3 points[16];
    int64_t count = host->trace_areas(host->ctx, origin, end, areas, points, 16);
    for (int64_t i = count - 1; i >= 0; i--) {
      if ((host->area(host->ctx, areas[i]).contents & 128) == 0)
        continue;
      /* C passes the extrapolated `end` as the origin for the avoid-spot test. */
      qa_travel_selection_start from = {areas[i], end, r->last_goal_area, r->last_area, state};
      qa_travel_selection selected =
          qa_travel_graph_select_from(&t->graph, goal, travel_flags, QA_TFL_JUMPPAD, &from);
      int64_t number = selected.reachability;
      if (number == 0) {
        size_t edge_count = 0;
        const qa_navigation_edge *const *edges = qa_navigation_runtime_outgoing(
            t->graph.runtime, qa_travel_graph_node(&t->graph, areas[i]), &edge_count);
        for (size_t e = 0; e < edge_count; e++) {
          qa_travel_reachability reach = qa_travel_graph_describe(&t->graph, edges[e]);
          if ((reach.reach.travel_type & QA_TRAVEL_MASK) == QA_TRAVEL_JUMPPAD) {
            number = qa_travel_graph_handle(&t->graph, edges[e]);
            break;
          }
        }
      }
      if (number != 0) {
        qa_bot_move_state_set_last_reachability(state, number);
        r->last_area = (int32_t)areas[i];
        break;
      }
    }
    qa_travel_reachability reach;
    if (qa_travel_graph_reach(&t->graph, r->last_reachability, &reach)) {
      qa_bot_move_result moved;
      if (travel(t, reach, true, &moved))
        *result = moved;
      result->travel_type = reach.reach.travel_type;
    }
  }
  if (result->blocked)
    r->reachability_time = (float)((double)r->reachability_time - fr(10 * (double)r->think_time));
  r->last_origin = r->origin;
}

void qa_source_bot_travel_init(qa_source_bot_travel *t, qa_source_bot_travel_host *host) {
  t->host = host;
}
bool qa_source_bot_travel_move_in_direction(qa_source_bot_travel *t, int64_t handle,
                                            qa_vec3 direction, double speed, int64_t type) {
  qa_bot_move_state *state = qa_bot_move_state_store_from_handle(t->host->move_states, handle);
  if (state == NULL)
    return false;
  travel_step step;
  step_init(&step, t->host, state);
  return step_move_in_direction(&step, direction, speed, type);
}
void qa_source_bot_travel_move_to_goal(qa_source_bot_travel *t, qa_bot_move_result *result,
                                       int64_t handle, const qa_bot_goal *goal,
                                       int64_t travel_flags) {
  result->failure = false;
  result->type = 0;
  result->blocked = false;
  result->block_entity = 0;
  result->travel_type = 0;
  result->flags = 0;
  qa_bot_move_state *state = qa_bot_move_state_store_from_handle(t->host->move_states, handle);
  if (state == NULL) {
    result->failure = true;
    return;
  }
  travel_step step;
  step_init(&step, t->host, state);
  step_move_to_goal(&step, result, goal, travel_flags);
}
