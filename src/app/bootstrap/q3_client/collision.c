/* Port of src/app/bootstrap/q3-client/collision.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q3_client/collision.h"
#include <string.h>
#include "qa/contracts/numeric.h"
#include "qa/foundation/support.h"
static const qa_vec3 zero_v = {0, 0, 0};
static qa_numeric_profile numeric_profile(void) {
  qa_numeric_profile p = {"q3:cgame", {QA_ARITHMETIC_BINARY32, NULL, 0, QA_FLOATING_ROUNDING_NEAREST_EVEN, false, false},
                          QA_FLOAT_TO_INT_QVM_INDEFINITE};
  return p;
}
static qa_q3_trace_result do_trace(void *ctx, qa_q3_trace_query query, qa_vec3 origin, qa_vec3 angles) {
  qa_app_q3_client_collision *s = ctx;
  s->counters.c_traces++;
  qa_trace_query q;
  q.start = query.start;
  q.end = query.end;
  q.shape.kind = query.shape.kind;
  q.shape.bounds.min = query.shape.mins;
  q.shape.bounds.max = query.shape.maxs;
  q.target.is_model = true;
  q.target.model = query.has_model_index ? query.model_index : 0;
  q.target.origin = origin;
  q.target.angles = angles;
  q.policy = (qa_trace_policy){"q3", NULL, false, 0, query.mask, NULL,
    !qa_collision_map_settings_no_curves(s->settings) && (query.has_curves ? query.curves : true),
    qa_collision_map_settings_player_curve_clip(s->settings) && (query.has_player_curve_clip ? query.player_curve_clip : true)};
  q.numeric = numeric_profile();
  q.pass_actor = NULL;
  qa_trace_result result = s->queries.trace(s->queries.ctx, q);
  if (result.kind == NULL || strcmp(result.kind, "q3") != 0)
    qa_range_error("Shared map did not apply cgame Q3 collision policy");
  qa_q3_trace_result out;
  out.fraction = result.fraction;
  out.end = result.end;
  out.solidity = result.all_solid ? "all-solid" : result.start_solid ? "start-solid" : "clear";
  out.has_plane = result.contact.has_plane;
  out.plane = result.contact.plane;
  out.contents = result.contents;
  out.surface_flags = result.surface_flags;
  return out;
}
static double do_contents(void *ctx, qa_vec3 point, double model, bool has_model, qa_vec3 origin, qa_vec3 angles) {
  qa_app_q3_client_collision *s = ctx;
  s->counters.c_pointcontents++;
  qa_point_contents_query q;
  q.point = point;
  q.target.is_model = true;
  q.target.model = has_model ? model : 0;
  q.target.origin = origin;
  q.target.angles = angles;
  q.policy = (qa_trace_policy){"q3", NULL, false, 0, -1, NULL,
    !qa_collision_map_settings_no_curves(s->settings),
    qa_collision_map_settings_player_curve_clip(s->settings)};
  q.numeric = numeric_profile();
  q.pass_actor = NULL;
  qa_point_contents_result result = s->queries.point_contents(s->queries.ctx, q);
  if (result.kind == NULL || strcmp(result.kind, "q3") != 0)
    qa_range_error("Shared map did not apply cgame Q3 contents policy");
  return result.contents;
}
static qa_q3_trace_result trace_plain(void *ctx, qa_q3_trace_query query) { return do_trace(ctx, query, zero_v, zero_v); }
static qa_q3_trace_result trace_transformed(void *ctx, qa_q3_trace_query query, qa_vec3 origin, qa_vec3 angles) {
  return do_trace(ctx, query, origin, angles);
}
static double contents_plain(void *ctx, qa_vec3 point, double model) {
  return do_contents(ctx, point, model, true, zero_v, zero_v);
}
static double contents_transformed(void *ctx, qa_vec3 point, double model, qa_vec3 origin, qa_vec3 angles) {
  return do_contents(ctx, point, model, true, origin, angles);
}
qa_q3_pres_collision_world qa_app_q3_client_collision_init(
    qa_app_q3_client_collision *state, qa_scene_queries queries, qa_collision_map_settings *settings) {
  state->queries = queries;
  state->settings = settings;
  qa_collision_counters_init(&state->counters);
  qa_q3_pres_collision_world world = {state, trace_plain, contents_plain,
                                      trace_transformed, contents_transformed, &state->counters};
  return world;
}
