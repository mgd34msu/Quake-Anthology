/*
 * Port of src/movement/q3/slide-move.ts. GPL-2.0-or-later.
 * Math.fround staging: double compute, cast to float at TS points.
 */
#include "qa/movement/q3/slide_move.h"

#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/movement/swept_body.h"

static double f32(double v) { return (double)(float)v; }
static qa_vec3 v3(double x, double y, double z) { qa_vec3 r = {x, y, z}; return r; }

qa_vec3 qa_q3_clip_velocity(qa_vec3 velocity, qa_vec3 normal, double overbounce) {
  double dot = f32(qa_dot3(velocity, normal));
  double bounce = f32(overbounce);
  double backoff = f32(dot < 0 ? dot * bounce : dot / bounce);
  return v3(velocity.x - f32(normal.x * backoff), velocity.y - f32(normal.y * backoff),
            velocity.z - f32(normal.z * backoff));
}

typedef struct slide_adapter {
  qa_q3_slide_move_context *context;
  qa_vec3 end_velocity;
  qa_trace_result native;
  int contacts;
} slide_adapter;

static bool ad_read(void *ctx, qa_swept_body_state *out) {
  slide_adapter *a = ctx;
  out->origin = a->context->state->origin; out->velocity = a->context->state->velocity;
  return true;
}
static void ad_write_origin(void *ctx, qa_vec3 origin) { ((slide_adapter *)ctx)->context->state->origin = origin; }
static void ad_write_velocity(void *ctx, qa_vec3 velocity, qa_swept_velocity_components components) {
  slide_adapter *a = ctx;
  a->context->state->velocity = components == QA_SWEPT_VELOCITY_VERTICAL
      ? v3(velocity.x, velocity.y, velocity.z) : velocity;
}
static void ad_trace(void *ctx, qa_vec3 start, qa_vec3 end, qa_swept_body_trace *out) {
  slide_adapter *a = ctx;
  a->native = a->context->trace(a->context->ctx, start, end, a->context->bounds,
                                 a->context->state->actor, a->context->mask);
  out->fraction = a->native.fraction; out->end = a->native.end;
  out->all_solid = a->native.all_solid; out->start_solid = a->native.start_solid;
  out->native = &a->native;
}
static void ad_touch(void *ctx, const qa_swept_body_trace *trace) {
  slide_adapter *a = ctx; a->contacts++;
  a->context->touch(a->context->ctx, *(const qa_trace_result *)trace->native);
}
static void ad_impact(void *ctx, const qa_swept_body_trace *trace, qa_vec3 normal) {
  (void)ctx; (void)trace; (void)normal;
}
static qa_vec3 ad_normal(void *ctx, const qa_swept_body_trace *trace) {
  (void)ctx;
  const qa_trace_result *t = trace->native;
  if (!t->contact.has_plane) qa_range_error("Movement impact trace requires a collision plane");
  return t->contact.plane.normal;
}
static bool ad_same_plane(void *ctx, qa_vec3 first, qa_vec3 second, bool same_slot) {
  (void)ctx; (void)first; (void)second; return same_slot;
}
static void ad_recover(void *ctx, qa_vec3 normal) {
  slide_adapter *a = ctx;
  a->context->state->velocity = qa_add3(a->context->state->velocity, normal);
}
static qa_vec3 ad_end_velocity(void *ctx) { return ((slide_adapter *)ctx)->end_velocity; }
static void ad_write_end(void *ctx, qa_vec3 v) { ((slide_adapter *)ctx)->end_velocity = v; }
static qa_vec3 ad_normalize(void *ctx, qa_vec3 d) { (void)ctx; return qa_normalize3(d); }
static void ad_impact_speed(void *ctx, double speed) {
  slide_adapter *a = ctx;
  if (speed > a->context->impact_speed) a->context->impact_speed = speed;
}
static qa_vec3 m_advance(void *ctx, qa_vec3 origin, double time, qa_vec3 velocity) {
  (void)ctx; return qa_add3(origin, qa_scale3(velocity, time));
}
static double m_remaining(void *ctx, double time, double fraction) {
  (void)ctx; return f32(time - f32(time * fraction));
}
static qa_vec3 m_clip(void *ctx, qa_vec3 velocity, qa_vec3 normal) {
  (void)ctx; return qa_q3_clip_velocity(velocity, normal, 1.001);
}
static double m_dot(void *ctx, qa_vec3 a, qa_vec3 b) { (void)ctx; return qa_dot3(a, b); }
static qa_vec3 m_cross(void *ctx, qa_vec3 a, qa_vec3 b) { (void)ctx; return qa_cross3(a, b); }
static qa_vec3 m_scale(void *ctx, qa_vec3 v, double s) { (void)ctx; return qa_scale3(v, s); }

bool qa_q3_slide_move(qa_q3_slide_move_context *context, bool gravity) {
  qa_q3_motion *ps = context->state;
  qa_vec3 primal_velocity = ps->velocity;
  qa_vec3 end_velocity = ps->velocity;
  if (gravity) {
    end_velocity = v3(ps->velocity.x, ps->velocity.y, ps->velocity.z - f32(ps->gravity * context->frame_time));
    ps->velocity = v3(ps->velocity.x, ps->velocity.y, (ps->velocity.z + end_velocity.z) * 0.5);
    primal_velocity = v3(primal_velocity.x, primal_velocity.y, end_velocity.z);
    if (context->has_ground_normal) ps->velocity = qa_q3_clip_velocity(ps->velocity, context->ground_normal, 1.001);
  }
  qa_vec3 planes[2]; size_t plane_count = 0;
  if (context->has_ground_normal) planes[plane_count++] = context->ground_normal;
  planes[plane_count++] = qa_normalize3_or_zero(ps->velocity);
  slide_adapter adapter = {context, end_velocity, {0}, 0};
  qa_swept_body_services services = {0};
  services.ctx = &adapter;
  services.read = ad_read; services.write_origin = ad_write_origin;
  services.write_velocity = ad_write_velocity; services.trace = ad_trace;
  services.touch = ad_touch; services.impact = ad_impact; services.normal = ad_normal;
  services.stop_when_still = false; services.same_plane = ad_same_plane;
  services.has_collision_policy = true; services.stop_on_start_solid = false;
  services.original_velocity = QA_SWEPT_ORIGINAL_INITIAL;
  services.crease_velocity = QA_SWEPT_CREASE_CURRENT;
  services.all_solid_velocity = QA_SWEPT_ALL_SOLID_ZERO_Z;
  services.has_duplicate_plane = true; services.duplicate_threshold = f32(0.99);
  services.duplicate_recover = ad_recover;
  services.has_plane_response = true;
  services.plane_response.ctx = &adapter;
  services.plane_response.seeds = planes; services.plane_response.seed_count = plane_count;
  services.plane_response.enter_threshold = 0.1;
  services.plane_response.end_velocity = ad_end_velocity;
  services.plane_response.write_end_velocity = ad_write_end;
  services.plane_response.normalize = ad_normalize;
  services.plane_response.impact_speed = ad_impact_speed;
  services.math.ctx = NULL;
  services.math.advance = m_advance; services.math.remaining = m_remaining;
  services.math.clip = m_clip; services.math.dot = m_dot;
  services.math.cross = m_cross; services.math.scale = m_scale;
  qa_sweep_stop stop = qa_sweep_body(&services, context->frame_time);
  if (stop != QA_SWEEP_COMPLETE) return true;
  if (gravity) ps->velocity = adapter.end_velocity;
  if (ps->pm_time != 0) ps->velocity = primal_velocity;
  return adapter.contacts != 0;
}

void qa_q3_step_slide_move(qa_q3_slide_move_context *context, bool gravity) {
  qa_q3_motion *ps = context->state;
  qa_vec3 start_origin = ps->origin, start_velocity = ps->velocity;
  if (!qa_q3_slide_move(context, gravity)) return;
  qa_vec3 down = v3(start_origin.x, start_origin.y, start_origin.z - 18);
  qa_trace_result ground = context->trace(context->ctx, start_origin, down, context->bounds, ps->actor, context->mask);
  if (ps->velocity.z > 0 && (ground.fraction == 1 || !ground.contact.has_plane ||
                             ground.contact.plane.normal.z < f32(0.7)))
    return;
  qa_vec3 up = v3(start_origin.x, start_origin.y, start_origin.z + 18);
  qa_trace_result raised = context->trace(context->ctx, start_origin, up, context->bounds, ps->actor, context->mask);
  if (raised.all_solid) {
    if (context->debug != NULL) context->debug(context->ctx, "bend can't step");
    return;
  }
  double step_size = f32(raised.end.z - start_origin.z);
  ps->origin = raised.end; ps->velocity = start_velocity;
  (void)qa_q3_slide_move(context, gravity);
  qa_trace_result dropped = context->trace(context->ctx, ps->origin,
      v3(ps->origin.x, ps->origin.y, ps->origin.z - step_size), context->bounds, ps->actor, context->mask);
  if (!dropped.all_solid) ps->origin = dropped.end;
  if (dropped.fraction < 1 && dropped.contact.has_plane)
    ps->velocity = qa_q3_clip_velocity(ps->velocity, dropped.contact.plane.normal, 1.001);
  double delta = f32(ps->origin.z - start_origin.z);
  if (delta > 2)
    context->event(context->ctx, delta < 7 ? QA_Q3_EV_STEP_4 : delta < 11 ? QA_Q3_EV_STEP_8 :
                                  delta < 15 ? QA_Q3_EV_STEP_12 : QA_Q3_EV_STEP_16);
  if (context->debug != NULL) context->debug(context->ctx, "stepped");
}
