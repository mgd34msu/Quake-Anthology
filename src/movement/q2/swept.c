/* Port of src/movement/q2/swept.ts. GPL-2.0-or-later. */
#include "qa/movement/q2/swept.h"

#include <string.h>

#include "qa/movement/q2/math.h"

typedef struct sweep_adapter {
  const qa_q2_body_sweep *options;
  qa_q2_movement_math math;
  qa_q2_trace native;
} sweep_adapter;

static qa_vec3 scene_of(const qa_q2_vec3 value) {
  const qa_vec3 result = {value[0], value[1], value[2]};
  return result;
}

static void source_of(qa_vec3 value, qa_q2_vec3 out) {
  out[0] = value.x;
  out[1] = value.y;
  out[2] = value.z;
}

static bool adapter_read(void *data, qa_swept_body_state *out) {
  sweep_adapter *adapter = data;
  out->origin = scene_of(*adapter->options->origin);
  out->velocity = scene_of(*adapter->options->velocity);
  return true;
}

static void adapter_write_origin(void *data, qa_vec3 origin) {
  sweep_adapter *adapter = data;
  qa_q2_vec3 source;
  source_of(origin, source);
  qa_q2_math_vector_copy(&adapter->math, source, *adapter->options->origin);
}

static void adapter_write_velocity(void *data, qa_vec3 velocity,
                                   qa_swept_velocity_components components) {
  sweep_adapter *adapter = data;
  const qa_q2_body_sweep *options = adapter->options;
  if (components == QA_SWEPT_VELOCITY_VERTICAL) {
    (*options->velocity)[2] = qa_q2_n_store(options->numeric, velocity.z);
  } else {
    qa_q2_vec3 source;
    source_of(velocity, source);
    qa_q2_math_vector_copy(&adapter->math, source, *options->velocity);
  }
}

static void adapter_trace(void *data, qa_vec3 start, qa_vec3 end, qa_swept_body_trace *out) {
  sweep_adapter *adapter = data;
  const qa_q2_body_sweep *options = adapter->options;
  (void)start; /* TS traces from the live origin, not the solver start. */
  qa_q2_vec3 source_end;
  source_of(end, source_end);
  adapter->native = options->trace(options->ctx, *options->origin, source_end);
  out->fraction = adapter->native.fraction;
  out->end = scene_of(adapter->native.endpos);
  out->all_solid = adapter->native.allsolid;
  out->start_solid = adapter->native.startsolid;
  out->native = &adapter->native;
}

static void adapter_prepare_trace(void *data, qa_swept_body_trace *trace) {
  sweep_adapter *adapter = data;
  if (adapter->options->prepare_trace != NULL)
    adapter->options->prepare_trace(adapter->options->ctx, trace->native);
}

static void adapter_solid(void *data, const qa_swept_body_trace *trace) {
  sweep_adapter *adapter = data;
  if (adapter->options->solid != NULL)
    adapter->options->solid(adapter->options->ctx, trace->native);
}

static qa_vec3 adapter_normal(void *data, const qa_swept_body_trace *trace) {
  sweep_adapter *adapter = data;
  const qa_q2_trace *native = trace->native;
  qa_q2_vec3 stored;
  qa_q2_math_vec3(&adapter->math, native->plane.normal[0], native->plane.normal[1],
                  native->plane.normal[2], stored);
  return scene_of(stored);
}

static void adapter_impact(void *data, const qa_swept_body_trace *trace, qa_vec3 normal) {
  sweep_adapter *adapter = data;
  (void)normal;
  adapter->options->touch(adapter->options->ctx, trace->native);
}

static bool adapter_same_plane(void *data, qa_vec3 first, qa_vec3 second, bool same_slot) {
  (void)data;
  (void)first;
  (void)second;
  /* TS (a, b) => a === b: normals are fresh objects, so only the
   * same solver slot is identical. */
  return same_slot;
}

static void adapter_duplicate_recover(void *data, qa_vec3 normal) {
  sweep_adapter *adapter = data;
  qa_q2_vec3 source;
  source_of(normal, source);
  adapter->options->duplicate_plane_recover(adapter->options->ctx, source);
}

static qa_vec3 math_advance(void *data, qa_vec3 origin, double time, qa_vec3 velocity) {
  sweep_adapter *adapter = data;
  qa_q2_vec3 source_origin, source_velocity, out;
  source_of(origin, source_origin);
  source_of(velocity, source_velocity);
  qa_q2_math_vec3(&adapter->math, 0, 0, 0, out);
  qa_q2_math_vector_ma(&adapter->math, source_origin, time, source_velocity, out);
  return scene_of(out);
}

static double math_remaining(void *data, double time, double fraction) {
  sweep_adapter *adapter = data;
  const qa_numeric_operations *n = adapter->options->numeric;
  return qa_q2_n_subtract(n, time, qa_q2_n_multiply(n, time, fraction));
}

static qa_vec3 math_clip(void *data, qa_vec3 velocity, qa_vec3 normal) {
  sweep_adapter *adapter = data;
  const qa_q2_body_sweep *options = adapter->options;
  qa_q2_vec3 source_velocity, source_normal, clipped, out;
  source_of(velocity, source_velocity);
  source_of(normal, source_normal);
  options->clip(options->ctx, source_velocity, source_normal, clipped);
  qa_q2_math_vec3(&adapter->math, 0, 0, 0, out);
  qa_q2_math_vector_copy(&adapter->math, clipped, out);
  return scene_of(out);
}

static double math_dot(void *data, qa_vec3 a, qa_vec3 b) {
  sweep_adapter *adapter = data;
  qa_q2_vec3 source_a, source_b;
  source_of(a, source_a);
  source_of(b, source_b);
  return qa_q2_math_dot_product(&adapter->math, source_a, source_b);
}

static qa_vec3 math_cross(void *data, qa_vec3 a, qa_vec3 b) {
  sweep_adapter *adapter = data;
  qa_q2_vec3 source_a, source_b, out;
  source_of(a, source_a);
  source_of(b, source_b);
  qa_q2_math_vec3_cross(&adapter->math, source_a, source_b, out);
  return scene_of(out);
}

static qa_vec3 math_scale(void *data, qa_vec3 vector, double amount) {
  sweep_adapter *adapter = data;
  qa_q2_vec3 source, out;
  source_of(vector, source);
  qa_q2_math_vec3_muls(&adapter->math, source, amount, out);
  return scene_of(out);
}

qa_sweep_stop qa_sweep_q2_body(const qa_q2_body_sweep *options) {
  sweep_adapter adapter;
  memset(&adapter, 0, sizeof adapter);
  adapter.options = options;
  qa_q2_movement_math_init(&adapter.math, options->numeric, QA_Q2_MATH_CLASSIC);

  qa_swept_body_services services;
  memset(&services, 0, sizeof services);
  services.ctx = &adapter;
  services.read = adapter_read;
  services.write_origin = adapter_write_origin;
  services.write_velocity = adapter_write_velocity;
  services.trace = adapter_trace;
  services.prepare_trace = adapter_prepare_trace;
  services.solid = adapter_solid;
  services.normal = adapter_normal;
  services.impact = adapter_impact;
  services.stop_when_still = false;
  services.has_collision_policy = true;
  services.stop_on_start_solid = false;
  services.original_velocity = QA_SWEPT_ORIGINAL_INITIAL;
  services.crease_velocity = QA_SWEPT_CREASE_LAST_CANDIDATE;
  services.candidate_velocity = QA_SWEPT_CANDIDATE_SEQUENTIAL;
  services.all_solid_velocity = QA_SWEPT_ALL_SOLID_ZERO_Z;
  services.same_plane = adapter_same_plane;
  if (options->has_duplicate_plane) {
    services.has_duplicate_plane = true;
    services.duplicate_threshold = options->duplicate_plane_threshold;
    services.duplicate_recover = adapter_duplicate_recover;
  }
  services.math.ctx = &adapter;
  services.math.advance = math_advance;
  services.math.remaining = math_remaining;
  services.math.clip = math_clip;
  services.math.dot = math_dot;
  services.math.cross = math_cross;
  services.math.scale = math_scale;
  return qa_sweep_body(&services, options->elapsed);
}
