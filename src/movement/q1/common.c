/* Port of src/movement/q1/common.ts. GPL-2.0-or-later. */
#include "qa/movement/q1/common.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/movement/body_shape.h"

const qa_vec3 qa_q1_zero = {0, 0, 0};
const qa_trace_hit qa_q1_none = {QA_TRACE_HIT_NONE, 0, {{NULL, NULL}, 0, 0, NULL}};
const qa_trace_hit qa_q1_world = {QA_TRACE_HIT_WORLD, 0, {{NULL, NULL}, 0, 0, NULL}};

static bool kind_is(const char *kind, const char *name) {
  return kind != NULL && strcmp(kind, name) == 0;
}

double qa_q1_js_max(double a, double b) {
  if (isnan(a) || isnan(b)) return NAN;
  if (a == b) return a == 0.0 && signbit(a) && !signbit(b) ? b : a;
  return a > b ? a : b;
}
double qa_q1_js_min(double a, double b) {
  if (isnan(a) || isnan(b)) return NAN;
  if (a == b) return a == 0.0 && !signbit(a) && signbit(b) ? b : a;
  return a < b ? a : b;
}
bool qa_q1_flag_test(double flags, int mask) { return (qa_int32(flags) & mask) != 0; }
double qa_q1_flag_set(double flags, int mask) { return (double)(qa_int32(flags) | mask); }
double qa_q1_flag_clear(double flags, int mask) { return (double)(qa_int32(flags) & ~mask); }

double qa_q1_n_store(const qa_numeric_operations *n, double v) { return n->store(n->ctx, v); }
double qa_q1_n_add(const qa_numeric_operations *n, double a, double b) { return n->add(n->ctx, a, b); }
double qa_q1_n_subtract(const qa_numeric_operations *n, double a, double b) {
  return n->subtract(n->ctx, a, b);
}
double qa_q1_n_multiply(const qa_numeric_operations *n, double a, double b) {
  return n->multiply(n->ctx, a, b);
}
double qa_q1_n_divide(const qa_numeric_operations *n, double a, double b) {
  return n->divide(n->ctx, a, b);
}
double qa_q1_n_square_root(const qa_numeric_operations *n, double v) {
  return n->square_root(n->ctx, v);
}
double qa_q1_n_to_int32(const qa_numeric_operations *n, double v) { return n->to_int32(n->ctx, v); }

qa_trace_result qa_q1_context_trace(qa_q1_movement_context *context, qa_vec3 start, qa_vec3 end) {
  return qa_q1_context_trace_ex(context, start, end, NULL, NULL);
}

qa_bounds qa_q1_shape_bounds(qa_trace_shape shape) {
  if (kind_is(shape.kind, "point")) {
    const qa_bounds bounds = {qa_q1_zero, qa_q1_zero};
    return bounds;
  }
  return shape.bounds;
}

double qa_q1_seconds(qa_source_time time) {
  return time.kind == QA_SOURCE_TIME_SECONDS ? time.value : time.value / 1000;
}

qa_vec3 qa_q1_trace_source_normal(const qa_trace_result *trace) {
  return kind_is(trace->kind, "q1") ? trace->source_plane_q1.normal : trace->source_plane.plane.normal;
}

void qa_q1_movement_math_init(qa_q1_movement_math *math, const qa_numeric_operations *operations) {
  math->n_storage = *operations;
  math->n = &math->n_storage;
  qa_create_mutable_vector_math(&math->v, math->n, QA_ZERO_LENGTH_PRESERVE);
}

qa_vec3 qa_q1_math_vec(const qa_q1_movement_math *math, double x, double y, double z) {
  const qa_vec3 out = {qa_q1_n_store(math->n, x), qa_q1_n_store(math->n, y), qa_q1_n_store(math->n, z)};
  return out;
}

qa_vec3 qa_q1_math_add(const qa_q1_movement_math *math, qa_vec3 a, qa_vec3 b) {
  qa_vec3 out = qa_q1_zero;
  qa_mvm_vector_add(&math->v, qa_vec3_access_plain(&a), qa_vec3_access_plain(&b), qa_vec3_access_plain(&out));
  return out;
}

qa_vec3 qa_q1_math_sub(const qa_q1_movement_math *math, qa_vec3 a, qa_vec3 b) {
  qa_vec3 out = qa_q1_zero;
  qa_mvm_vector_subtract(&math->v, qa_vec3_access_plain(&a), qa_vec3_access_plain(&b), qa_vec3_access_plain(&out));
  return out;
}

qa_vec3 qa_q1_math_scale(const qa_q1_movement_math *math, qa_vec3 a, double scale) {
  qa_vec3 out = qa_q1_zero;
  qa_mvm_vector_scale(&math->v, qa_vec3_access_plain(&a), scale, qa_vec3_access_plain(&out));
  return out;
}

qa_vec3 qa_q1_math_ma(const qa_q1_movement_math *math, qa_vec3 a, double scale, qa_vec3 c) {
  qa_vec3 out = qa_q1_zero;
  qa_mvm_vector_ma(&math->v, qa_vec3_access_plain(&a), scale, qa_vec3_access_plain(&c), qa_vec3_access_plain(&out));
  return out;
}

double qa_q1_math_dot(const qa_q1_movement_math *math, qa_vec3 a, qa_vec3 b) {
  return qa_mvm_dot_product(&math->v, qa_vec3_access_plain(&a), qa_vec3_access_plain(&b));
}

double qa_q1_math_length(const qa_q1_movement_math *math, qa_vec3 a) {
  return qa_mvm_vector_length(&math->v, qa_vec3_access_plain(&a));
}

qa_q1_normalized qa_q1_math_normalize(const qa_q1_movement_math *math, qa_vec3 a) {
  qa_q1_normalized result;
  result.direction = a;
  result.length = qa_mvm_vector_normalize(&math->v, qa_vec3_access_plain(&result.direction));
  return result;
}

qa_vec3 qa_q1_math_cross(const qa_q1_movement_math *math, qa_vec3 a, qa_vec3 b) {
  qa_vec3 out = qa_q1_zero;
  qa_mvm_cross_product(&math->v, qa_vec3_access_plain(&a), qa_vec3_access_plain(&b), qa_vec3_access_plain(&out));
  return out;
}

qa_angle_vectors qa_q1_math_angles(const qa_q1_movement_math *math, qa_vec3 value) {
  if (math->n->profile.arithmetic.kind != QA_ARITHMETIC_DONOR_BINARY64) return qa_math_angle_vectors(value);
  qa_angle_vectors out = {qa_q1_zero, qa_q1_zero, qa_q1_zero};
  qa_donor_angle_vectors(value, &out.forward, &out.right, &out.up);
  return out;
}

qa_vec3 qa_q1_math_clip(const qa_q1_movement_math *math, qa_vec3 velocity, qa_vec3 normal, double overbounce) {
  const qa_numeric_operations *n = math->n;
  const double backoff = qa_q1_n_multiply(n, qa_q1_math_dot(math, velocity, normal), overbounce);
  const qa_vec3 out = qa_q1_math_vec(math, qa_q1_n_subtract(n, velocity.x, qa_q1_n_multiply(n, normal.x, backoff)),
                                     qa_q1_n_subtract(n, velocity.y, qa_q1_n_multiply(n, normal.y, backoff)),
                                     qa_q1_n_subtract(n, velocity.z, qa_q1_n_multiply(n, normal.z, backoff)));
  return qa_q1_math_vec(math, fabs(out.x) < 0.1 ? 0 : out.x, fabs(out.y) < 0.1 ? 0 : out.y,
                        fabs(out.z) < 0.1 ? 0 : out.z);
}

double qa_q1_math_horizontal(const qa_q1_movement_math *math, qa_vec3 a) {
  const qa_numeric_operations *n = math->n;
  return qa_q1_n_square_root(n, qa_q1_n_add(n, qa_q1_n_multiply(n, a.x, a.x), qa_q1_n_multiply(n, a.y, a.y)));
}

void qa_q1_movement_context_init(qa_q1_movement_context *context, qa_q1_player_input input,
                                 qa_movement_services services, qa_q1_movement_options options) {
  memset(context, 0, sizeof *context);
  context->input = input;
  context->services = services;
  context->options = options;
  context->source_mode = input.state.kind == QA_MOVEMENT_STATE_Q1 ? input.state.as.q1.move_type
                                                                    : input.state.as.qw.spectator;
  qa_q1_movement_math_init(&context->math, &services.numeric);
}

void qa_q1_movement_context_destroy(qa_q1_movement_context *context) {
  if (!context->arrays_taken) {
    free(context->contacts);
    free(context->effects);
  }
  context->contacts = NULL;
  context->effects = NULL;
  context->contact_count = context->contact_capacity = 0;
  context->effect_count = context->effect_capacity = 0;
}

static void push_contact(qa_q1_movement_context *context, qa_movement_contact contact) {
  if (context->contact_count == context->contact_capacity) {
    const size_t capacity = context->contact_capacity == 0 ? 8 : context->contact_capacity * 2;
    qa_movement_contact *grown = realloc(context->contacts, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    context->contacts = grown;
    context->contact_capacity = capacity;
  }
  context->contacts[context->contact_count++] = contact;
}

static qa_movement_state resumed(qa_q1_movement_context *context, qa_movement_state state) {
  if (state.kind == QA_MOVEMENT_STATE_Q1) context->source_mode = state.as.q1.move_type;
  else if (state.kind == QA_MOVEMENT_STATE_QW) context->source_mode = state.as.qw.spectator;
  return state;
}

double qa_q1_context_view_height(const qa_q1_movement_context *context) {
  if (context->input.environment.pose != NULL) return context->input.environment.pose->view_height;
  return context->options.has_view_height ? context->options.view_height : 22;
}

void qa_q1_context_project_client_mode(qa_q1_movement_context *context) { context->mode_projected = true; }

qa_movement_state qa_q1_context_source_state(const qa_q1_movement_context *context, qa_movement_state state) {
  if (!context->mode_projected) return state;
  if (state.kind == QA_MOVEMENT_STATE_Q1) state.as.q1.move_type = context->source_mode;
  else if (state.kind == QA_MOVEMENT_STATE_QW) state.as.qw.spectator = context->source_mode;
  return state;
}

double qa_q1_context_speed(qa_q1_movement_context *context, double value) {
  const double multiplier =
      context->input.environment.has_speed_multiplier ? context->input.environment.speed_multiplier : 1;
  return multiplier == 1 ? value : qa_q1_n_multiply(context->math.n, value, multiplier);
}

qa_trace_shape qa_q1_context_shape(qa_q1_movement_context *context) {
  if (context->input.environment.pose != NULL) {
    const qa_trace_shape shape = {"box", context->input.environment.pose->bounds};
    return shape;
  }
  const qa_q1_movement_hooks *hooks = context->options.hooks;
  const qa_trace_shape source = hooks != NULL && hooks->shape != NULL ? hooks->shape(hooks->ctx) : context->input.shape;
  if (!context->has_owned_bounds || kind_is(source.kind, "point")) return source;
  const qa_trace_shape shape = {source.kind, context->owned_bounds};
  return shape;
}

typedef struct body_clear {
  qa_q1_movement_context *context;
  qa_vec3 origin;
  const char *kind;
} body_clear;

static bool body_clear_fn(void *data, qa_bounds bounds) {
  body_clear *clear = data;
  const qa_trace_shape shape = {clear->kind, bounds};
  const qa_trace_result trace = qa_q1_context_trace_ex(clear->context, clear->origin, clear->origin, &shape, NULL);
  return !trace.start_solid && !trace.all_solid;
}

void qa_q1_context_update_body_shape(qa_q1_movement_context *context, qa_movement_state state) {
  const qa_mod_client_movement_outputs *outputs = context->input.environment.client_outputs;
  const qa_bounds *requested = outputs != NULL ? outputs->body_bounds : NULL;
  if (requested == NULL || context->input.environment.pose != NULL) {
    context->has_owned_bounds = false;
    return;
  }
  const qa_q1_movement_hooks *hooks = context->options.hooks;
  const qa_trace_shape source = hooks != NULL && hooks->shape != NULL ? hooks->shape(hooks->ctx) : context->input.shape;
  if (kind_is(source.kind, "point")) qa_range_error("A player body output requires a selected collision hull");
  qa_vec3 origin = qa_q1_zero;
  switch (state.kind) {
    case QA_MOVEMENT_STATE_Q1: origin = state.as.q1.origin; break;
    case QA_MOVEMENT_STATE_QW: origin = state.as.qw.origin; break;
    case QA_MOVEMENT_STATE_Q2:
      origin.x = state.as.q2.origin_eighths.values[0] / 8;
      origin.y = state.as.q2.origin_eighths.values[1] / 8;
      origin.z = state.as.q2.origin_eighths.values[2] / 8;
      break;
    case QA_MOVEMENT_STATE_Q2_RERELEASE: origin = state.as.q2_rerelease.origin; break;
    case QA_MOVEMENT_STATE_Q3: origin = state.as.q3.origin; break;
  }
  const qa_bounds previous = context->has_owned_bounds ? context->owned_bounds
                             : context->input.current_bounds != NULL ? *context->input.current_bounds
                                                                      : source.bounds;
  body_clear clear = {context, origin, source.kind};
  context->owned_bounds = qa_movement_bounds(previous, *requested, body_clear_fn, &clear);
  context->has_owned_bounds = true;
  if (hooks != NULL && hooks->body_shape != NULL) hooks->body_shape(hooks->ctx, context->owned_bounds);
}

qa_bounds qa_q1_context_bounds(qa_q1_movement_context *context) {
  return qa_q1_shape_bounds(qa_q1_context_shape(context));
}

qa_trace_result qa_q1_context_trace_ex(qa_q1_movement_context *context, qa_vec3 start, qa_vec3 end,
                                       const qa_trace_shape *shape, const char *move) {
  const qa_trace_shape active = shape != NULL ? *shape : qa_q1_context_shape(context);
  qa_trace_query query;
  memset(&query, 0, sizeof query);
  query.start = start;
  query.end = end;
  query.shape = active;
  query.target.is_model = false;
  query.policy.kind = "q1";
  query.policy.move = move != NULL ? move : "normal";
  query.policy.has_hull = false;
  query.numeric = context->input.profile.numeric;
  query.pass_actor = &context->input.actor.id;
  return context->services.scene.trace(context->services.scene.ctx, query);
}

double qa_q1_context_contents(qa_q1_movement_context *context, qa_vec3 point) {
  qa_point_contents_query query;
  memset(&query, 0, sizeof query);
  query.point = point;
  query.target.is_model = false;
  query.policy.kind = "q1";
  query.policy.move = "normal";
  query.policy.has_hull = false;
  query.numeric = context->input.profile.numeric;
  query.pass_actor = &context->input.actor.id;
  const qa_point_contents_result value = context->services.scene.point_contents(context->services.scene.ctx, query);
  if (!kind_is(value.kind, "q1"))
    qa_range_error("Q1 movement requires collision contents translated to its selected policy");
  /* SV_PointContents/PM_PointContents collapse source currents to water. */
  return value.contents <= -9 && value.contents >= -14 ? -3 : value.contents;
}

bool qa_q1_context_position_free(qa_q1_movement_context *context, qa_vec3 origin) {
  const qa_trace_result trace = qa_q1_context_trace(context, origin, origin);
  return !trace.start_solid && !trace.all_solid;
}

bool qa_q1_context_ladder_ahead(qa_q1_movement_context *context, qa_vec3 origin, qa_vec3 forward) {
  const qa_q1_normalized flat = qa_q1_math_normalize(&context->math, qa_q1_math_vec(&context->math, forward.x, forward.y, 0));
  if (flat.length == 0) return false;
  const qa_trace_result trace = qa_q1_context_trace(context, origin, qa_q1_math_add(&context->math, origin, flat.direction));
  const double flags = trace.has_surface_flags ? trace.surface_flags : 0;
  return trace.fraction < 1 && kind_is(trace.kind, "q1") && (qa_int32(flags) & 8) != 0;
}

double qa_q1_ladder_wish_z(double wish_z, double forward_move, double up_move, double velocity_z) {
  if (fabs(velocity_z) > 200) return wish_z;
  if (forward_move > 0 && wish_z > 0) return 200;
  if (forward_move > 0 && wish_z < 0) return -200;
  if (up_move > 0) return 200;
  if (up_move < 0) return -200;
  return wish_z;
}

void qa_q1_context_effect(qa_q1_movement_context *context, qa_movement_effect effect) {
  if (context->effect_count == context->effect_capacity) {
    const size_t capacity = context->effect_capacity == 0 ? 8 : context->effect_capacity * 2;
    qa_ordered_movement_effect *grown = realloc(context->effects, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    context->effects = grown;
    context->effect_capacity = capacity;
  }
  qa_ordered_movement_effect *entry = &context->effects[context->effect_count];
  entry->substep = context->substep;
  entry->sequence = (double)context->effect_count;
  entry->time = context->input.frame.time;
  entry->effect = effect;
  context->effect_count++;
}

void qa_q1_context_record_contact(qa_q1_movement_context *context, qa_trace_result trace) {
  const qa_movement_contact contact = {trace.hit, trace, context->substep};
  push_contact(context, contact);
}

qa_movement_state qa_q1_context_touch(qa_q1_movement_context *context, qa_trace_result trace,
                                      qa_movement_state state, bool record) {
  if (record) {
    const qa_movement_contact contact = {trace.hit, trace, context->substep};
    push_contact(context, contact);
  }
  if (trace.hit.kind == QA_TRACE_HIT_NONE || context->removed) return state;
  qa_movement_effect touch;
  memset(&touch, 0, sizeof touch);
  touch.kind = QA_MOVEMENT_EFFECT_TOUCH;
  touch.target = trace.hit;
  touch.substep = context->substep;
  qa_q1_context_effect(context, touch);
  qa_plane plane = trace.contact.plane;
  qa_touch_surface surface;
  const bool has_surface = kind_is(trace.kind, "q2") && trace.surface != NULL;
  if (has_surface) {
    surface.name = trace.surface->name;
    surface.native_flags = trace.surface->flags;
    surface.native_value = trace.surface->value;
  }
  qa_movement_touch_contact contact;
  memset(&contact, 0, sizeof contact);
  contact.self = context->input.actor;
  contact.other = trace.hit;
  contact.plane = trace.contact.has_plane ? &plane : NULL;
  contact.surface = has_surface ? &surface : NULL;
  const qa_movement_continuation continuation =
      context->services.touch(context->services.ctx, contact, qa_q1_context_source_state(context, state));
  if (!continuation.is_continue) {
    context->removed = true;
    return state;
  }
  return resumed(context, continuation.state);
}

qa_movement_state qa_q1_context_link(qa_q1_movement_context *context, qa_movement_state state, bool touch_triggers) {
  const qa_q1_movement_hooks *hooks = context->options.hooks;
  if (context->removed || hooks == NULL) return state;
  const qa_movement_continuation continuation =
      hooks->link(hooks->ctx, context->input.actor, qa_q1_context_source_state(context, state), touch_triggers);
  if (!continuation.is_continue) {
    context->removed = true;
    return state;
  }
  return resumed(context, continuation.state);
}

qa_movement_state qa_q1_context_lifecycle(qa_q1_movement_context *context, qa_movement_state state,
                                          qa_q1_lifecycle_phase phase, const qa_q1_player_input *input) {
  if (context->removed) return state;
  const qa_q1_movement_hooks *hooks = context->options.hooks;
  qa_movement_continuation (*hook)(void *, qa_q1_player_input, qa_movement_state) = NULL;
  if (hooks != NULL) {
    hook = phase == QA_Q1_LIFECYCLE_BEFORE_PHYSICS ? hooks->before_physics
           : phase == QA_Q1_LIFECYCLE_THINK       ? hooks->think
                                                   : hooks->after_physics;
  }
  if (hook == NULL) {
    if (phase == QA_Q1_LIFECYCLE_BEFORE_PHYSICS) qa_q1_context_update_body_shape(context, state);
    return state;
  }
  const qa_movement_continuation continuation =
      hook(hooks->ctx, input != NULL ? *input : context->input, qa_q1_context_source_state(context, state));
  if (!continuation.is_continue) {
    context->removed = true;
    return state;
  }
  const qa_movement_state result = resumed(context, continuation.state);
  if (phase == QA_Q1_LIFECYCLE_BEFORE_PHYSICS) qa_q1_context_update_body_shape(context, result);
  return result;
}

void qa_q1_context_take_results(qa_q1_movement_context *context, qa_movement_result *result) {
  result->contacts = context->contacts;
  result->contact_count = context->contact_count;
  qa_q1_context_take_effects(context, result);
}

void qa_q1_context_take_effects(qa_q1_movement_context *context, qa_movement_result *result) {
  result->effects = context->effects;
  result->effect_count = context->effect_count;
  if (result->contacts != context->contacts) {
    free(context->contacts);
    context->contacts = NULL;
    context->contact_count = context->contact_capacity = 0;
  }
  context->arrays_taken = true;
}

bool qa_q1_context_is_bsp(qa_q1_movement_context *context, qa_trace_hit hit) {
  const qa_q1_movement_hooks *hooks = context->options.hooks;
  return hit.kind == QA_TRACE_HIT_WORLD || (hooks != NULL && hooks->is_bsp(hooks->ctx, hit));
}
