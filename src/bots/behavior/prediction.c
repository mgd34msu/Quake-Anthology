/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/prediction.h"
#include "qa/foundation/support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static noreturn void fail(const char *message) {
  fprintf(stderr, "%s\n", message);
  abort();
}
/* The observed scene records the last collision query the provider executed. */
typedef struct observed_scene {
  qa_scene_queries scene;
  bool has_trace;
  qa_bot_travel_trace trace;
} observed_scene;
static qa_trace_result observed_trace(void *ctx, qa_trace_query query) {
  observed_scene *o = ctx;
  qa_trace_result result = o->scene.trace(o->scene.ctx, query);
  o->has_trace = true;
  o->trace.query = query;
  o->trace.result = result;
  return result;
}
static qa_point_contents_result observed_point_contents(void *ctx, qa_point_contents_query query) {
  observed_scene *o = ctx;
  return o->scene.point_contents(o->scene.ctx, query);
}
static qa_leaf_query_result observed_box_leaves(void *ctx, qa_bounds bounds, double limit) {
  observed_scene *o = ctx;
  return o->scene.box_leaves(o->scene.ctx, bounds, limit);
}
static bool observed_areas_connected(void *ctx, double first, double second) {
  observed_scene *o = ctx;
  return o->scene.areas_connected(o->scene.ctx, first, second);
}
static bool observed_cluster_visible(void *ctx, double from, double to, const char *kind) {
  observed_scene *o = ctx;
  return o->scene.cluster_visible(o->scene.ctx, from, to, kind);
}
static qa_movement_result move(const qa_movement_input *input, const qa_movement_provider *provider,
                               qa_movement_services services) {
  if (strcmp(input->execution, "prediction") != 0 || strcmp(provider->id, input->profile.id) != 0)
    fail("Bot projection requires the selected provider in prediction mode");
  static const char *const KINDS[] = {"q1-netquake", "q1-quakeworld", "q2-classic", "q2-rerelease", "q3"};
  for (size_t i = 0; i < sizeof KINDS / sizeof KINDS[0]; i++)
    if (strcmp(input->kind, KINDS[i]) == 0 && strcmp(provider->kind, input->kind) == 0)
      return provider->move(provider->ctx, *input, services);
  fail("Bot projection movement input and provider disagree");
}
static void state_vectors(const qa_movement_state *state, qa_vec3 *origin, qa_vec3 *velocity) {
  switch (state->kind) {
  case QA_MOVEMENT_STATE_Q1:
    *origin = state->as.q1.origin;
    *velocity = state->as.q1.velocity;
    return;
  case QA_MOVEMENT_STATE_QW:
    *origin = state->as.qw.origin;
    *velocity = state->as.qw.velocity;
    return;
  case QA_MOVEMENT_STATE_Q2:
    *origin = (qa_vec3){state->as.q2.origin_eighths.values[0] / 8, state->as.q2.origin_eighths.values[1] / 8,
                        state->as.q2.origin_eighths.values[2] / 8};
    *velocity = (qa_vec3){state->as.q2.velocity_eighths.values[0] / 8, state->as.q2.velocity_eighths.values[1] / 8,
                          state->as.q2.velocity_eighths.values[2] / 8};
    return;
  case QA_MOVEMENT_STATE_Q2_RERELEASE:
    *origin = state->as.q2_rerelease.origin;
    *velocity = state->as.q2_rerelease.velocity;
    return;
  case QA_MOVEMENT_STATE_Q3:
    *origin = state->as.q3.origin;
    *velocity = state->as.q3.velocity;
    return;
  }
}
qa_bot_travel_prediction_result qa_project_bot_movement(qa_bot_movement_prediction query,
                                                        qa_bot_movement_projection_bots_a projection) {
  qa_bot_travel_prediction_result r;
  memset(&r, 0, sizeof r);
  qa_vec3 end = query.origin, velocity = query.velocity;
  double seconds = 0, water_level = 0;
  int64_t frames = 0, stop_event = 0;
  bool grounded = query.on_ground;
  observed_scene observed;
  memset(&observed, 0, sizeof observed);
  observed.scene = projection.services.scene;
  qa_movement_services services = projection.services;
  services.scene.ctx = &observed;
  services.scene.trace = observed_trace;
  services.scene.point_contents = observed_point_contents;
  services.scene.box_leaves = observed_box_leaves;
  services.scene.areas_connected = observed_areas_connected;
  services.scene.cluster_visible = observed_cluster_visible;
  size_t capacity = (size_t)(query.max_frames > 0 ? query.max_frames : 0) + 1;
  qa_vec3 *trajectory = malloc(capacity * sizeof *trajectory);
  if (trajectory == NULL)
    qa_range_error("Out of memory projecting bot movement");
  size_t count = 0;
  trajectory[count++] = end;
  qa_movement_result previous;
  bool has_previous = false;
  const qa_vec3 no_command = {0, 0, 0};
  for (int64_t frame = 0; frame < query.max_frames; frame++) {
    qa_movement_input input = projection.input(projection.ctx, has_previous ? &previous : NULL, frame,
                                               frame < query.command_frames ? query.command_move
                                                                            : no_command);
    double elapsed = input.frame.elapsed.kind == QA_SOURCE_TIME_MILLISECONDS
                         ? input.frame.elapsed.value / 1000
                         : input.frame.elapsed.value;
    if (!(elapsed > 0))
      qa_range_error("Bot movement projection must advance detached source time");
    qa_movement_result result = move(&input, &projection.provider, services);
    if (strcmp(result.status, "actor-removed") == 0)
      fail("Movement projection attempted to remove the authoritative actor");
    state_vectors(&result.state, &end, &velocity);
    grounded = result.ground.kind != QA_TRACE_HIT_NONE;
    water_level = result.water_level;
    r.has_bounds = true;
    r.bounds = result.bounds;
    qa_bot_movement_stop stop =
        projection.stop(projection.ctx, has_previous ? &previous : NULL, &result, frame);
    r.has_end_area = stop.has_area;
    r.end_area = (double)stop.area;
    stop_event = stop.events & query.stop_events;
    if (stop_event != 0)
      end = stop.origin;
    seconds += elapsed;
    frames++;
    trajectory[count++] = end;
    previous = result;
    has_previous = true;
    if (stop_event != 0)
      break;
  }
  r.end = end;
  r.velocity = velocity;
  r.frames = (double)frames;
  r.stop_event = (double)stop_event;
  r.trajectory = trajectory;
  r.trajectory_count = count;
  r.seconds = seconds;
  r.grounded = grounded;
  r.water_level = water_level;
  r.has_trace = observed.has_trace;
  r.trace = observed.trace;
  return r;
}
void qa_bot_travel_prediction_result_destroy(qa_bot_travel_prediction_result *result) {
  free((void *)result->trajectory);
  result->trajectory = NULL;
  result->trajectory_count = 0;
}
