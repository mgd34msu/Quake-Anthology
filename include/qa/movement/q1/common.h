/*
 * Port of src/movement/q1/common.ts: MovementMath and MovementContext,
 * the shared NetQuake/QuakeWorld kernel state (WinQuake sv_phys.c,
 * sv_user.c and QW pmove.c). Copyright (C) 1996-1997 Id Software, Inc.
 * GPL-2.0-or-later.
 * C accommodations:
 *  - The context owns malloc-grown contact/effect arrays; a finished
 *    movement result takes them over (qa_q1_context_take_arrays) and
 *    the caller frees them, mirroring the TS result's live arrays.
 *  - qa_q1_movement_math must be initialized in place and not copied
 *    afterwards (its operation pointers address its own storage).
 *  - JS Math.max/Math.min (NaN-propagating) and ToInt32 flag edits get
 *    the qa_q1_js_* / qa_q1_flag_* helpers below; numeric-operation
 *    calls go through the qa_q1_n_* wrappers.
 */
#ifndef QA_MOVEMENT_Q1_COMMON_H
#define QA_MOVEMENT_Q1_COMMON_H

#include <math.h>
#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/math.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/numeric.h"
#include "qa/contracts/scene.h"
#include "qa/contracts/time.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/movement/q1/types.h"

extern const qa_vec3 qa_q1_zero;      /* TS ZERO */
extern const qa_trace_hit qa_q1_none; /* TS NONE */
extern const qa_trace_hit qa_q1_world; /* TS WORLD */

qa_bounds qa_q1_shape_bounds(qa_trace_shape shape);
double qa_q1_seconds(qa_source_time time);
/* TS trace.sourcePlane.normal across the trace union. */
qa_vec3 qa_q1_trace_source_normal(const qa_trace_result *trace);

static inline double qa_q1_js_max(double a, double b) {
  if (isnan(a) || isnan(b)) return NAN;
  if (a == b) return a == 0.0 && signbit(a) && !signbit(b) ? b : a;
  return a > b ? a : b;
}
static inline double qa_q1_js_min(double a, double b) {
  if (isnan(a) || isnan(b)) return NAN;
  if (a == b) return a == 0.0 && !signbit(a) && signbit(b) ? b : a;
  return a < b ? a : b;
}
static inline bool qa_q1_flag_test(double flags, int mask) { return (qa_int32(flags) & mask) != 0; }
static inline double qa_q1_flag_set(double flags, int mask) { return (double)(qa_int32(flags) | mask); }
static inline double qa_q1_flag_clear(double flags, int mask) { return (double)(qa_int32(flags) & ~mask); }

static inline double qa_q1_n_store(const qa_numeric_operations *n, double v) { return n->store(n->ctx, v); }
static inline double qa_q1_n_add(const qa_numeric_operations *n, double a, double b) { return n->add(n->ctx, a, b); }
static inline double qa_q1_n_subtract(const qa_numeric_operations *n, double a, double b) {
  return n->subtract(n->ctx, a, b);
}
static inline double qa_q1_n_multiply(const qa_numeric_operations *n, double a, double b) {
  return n->multiply(n->ctx, a, b);
}
static inline double qa_q1_n_divide(const qa_numeric_operations *n, double a, double b) {
  return n->divide(n->ctx, a, b);
}
static inline double qa_q1_n_square_root(const qa_numeric_operations *n, double v) {
  return n->square_root(n->ctx, v);
}
static inline double qa_q1_n_to_int32(const qa_numeric_operations *n, double v) { return n->to_int32(n->ctx, v); }

/** TS MovementMath (TS field `n` is math.n here). */
typedef struct qa_q1_movement_math {
  qa_numeric_operations n_storage;
  const qa_numeric_operations *n;
  qa_mutable_vector_math v;
} qa_q1_movement_math;

void qa_q1_movement_math_init(qa_q1_movement_math *math, const qa_numeric_operations *operations);
qa_vec3 qa_q1_math_vec(const qa_q1_movement_math *math, double x, double y, double z);
qa_vec3 qa_q1_math_add(const qa_q1_movement_math *math, qa_vec3 a, qa_vec3 b);
qa_vec3 qa_q1_math_sub(const qa_q1_movement_math *math, qa_vec3 a, qa_vec3 b);
qa_vec3 qa_q1_math_scale(const qa_q1_movement_math *math, qa_vec3 a, double scale);
qa_vec3 qa_q1_math_ma(const qa_q1_movement_math *math, qa_vec3 a, double scale, qa_vec3 c);
double qa_q1_math_dot(const qa_q1_movement_math *math, qa_vec3 a, qa_vec3 b);
double qa_q1_math_length(const qa_q1_movement_math *math, qa_vec3 a);
typedef struct qa_q1_normalized {
  qa_vec3 direction;
  double length;
} qa_q1_normalized;
qa_q1_normalized qa_q1_math_normalize(const qa_q1_movement_math *math, qa_vec3 a);
qa_vec3 qa_q1_math_cross(const qa_q1_movement_math *math, qa_vec3 a, qa_vec3 b);
qa_angle_vectors qa_q1_math_angles(const qa_q1_movement_math *math, qa_vec3 value);
qa_vec3 qa_q1_math_clip(const qa_q1_movement_math *math, qa_vec3 velocity, qa_vec3 normal, double overbounce);
double qa_q1_math_horizontal(const qa_q1_movement_math *math, qa_vec3 a);

/** TS MovementContext. Initialize in place; destroy frees arrays
 * unless a result took them over. */
typedef struct qa_q1_movement_context {
  qa_q1_movement_math math;
  qa_movement_contact *contacts;
  size_t contact_count;
  size_t contact_capacity;
  qa_ordered_movement_effect *effects;
  size_t effect_count;
  size_t effect_capacity;
  qa_q1_player_input input;
  qa_movement_services services;
  qa_q1_movement_options options;
  double source_mode;
  bool has_owned_bounds;
  qa_bounds owned_bounds;
  bool mode_projected;
  bool removed;
  double substep;
  bool arrays_taken;
} qa_q1_movement_context;

void qa_q1_movement_context_init(qa_q1_movement_context *context, qa_q1_player_input input,
                                 qa_movement_services services, qa_q1_movement_options options);
void qa_q1_movement_context_destroy(qa_q1_movement_context *context);

double qa_q1_context_view_height(const qa_q1_movement_context *context);
void qa_q1_context_project_client_mode(qa_q1_movement_context *context);
qa_movement_state qa_q1_context_source_state(const qa_q1_movement_context *context, qa_movement_state state);
double qa_q1_context_speed(qa_q1_movement_context *context, double value);
qa_trace_shape qa_q1_context_shape(qa_q1_movement_context *context);
void qa_q1_context_update_body_shape(qa_q1_movement_context *context, qa_movement_state state);
qa_bounds qa_q1_context_bounds(qa_q1_movement_context *context);
/* shape NULL = context shape; move NULL = "normal"
 * ("normal" | "no-monsters" | "missile"). */
qa_trace_result qa_q1_context_trace_ex(qa_q1_movement_context *context, qa_vec3 start, qa_vec3 end,
                                       const qa_trace_shape *shape, const char *move);
static inline qa_trace_result qa_q1_context_trace(qa_q1_movement_context *context, qa_vec3 start, qa_vec3 end) {
  return qa_q1_context_trace_ex(context, start, end, NULL, NULL);
}
double qa_q1_context_contents(qa_q1_movement_context *context, qa_vec3 point);
bool qa_q1_context_position_free(qa_q1_movement_context *context, qa_vec3 origin);
bool qa_q1_context_ladder_ahead(qa_q1_movement_context *context, qa_vec3 origin, qa_vec3 forward);
double qa_q1_ladder_wish_z(double wish_z, double forward_move, double up_move, double velocity_z);
void qa_q1_context_effect(qa_q1_movement_context *context, qa_movement_effect effect);
/* Records a contact without invoking touch (QW impact recording). */
void qa_q1_context_record_contact(qa_q1_movement_context *context, qa_trace_result trace);
qa_movement_state qa_q1_context_touch(qa_q1_movement_context *context, qa_trace_result trace,
                                      qa_movement_state state, bool record);
qa_movement_state qa_q1_context_link(qa_q1_movement_context *context, qa_movement_state state, bool touch_triggers);
/* TS lifecycle phases: "beforePhysics" | "think" | "afterPhysics". */
typedef enum qa_q1_lifecycle_phase {
  QA_Q1_LIFECYCLE_BEFORE_PHYSICS,
  QA_Q1_LIFECYCLE_THINK,
  QA_Q1_LIFECYCLE_AFTER_PHYSICS
} qa_q1_lifecycle_phase;
/* input NULL = the context input. */
qa_movement_state qa_q1_context_lifecycle(qa_q1_movement_context *context, qa_movement_state state,
                                          qa_q1_lifecycle_phase phase, const qa_q1_player_input *input);
bool qa_q1_context_is_bsp(qa_q1_movement_context *context, qa_trace_hit hit);

/* Transfers the context arrays into a movement result (caller frees).
 * take_effects frees the contacts the removed result does not carry. */
void qa_q1_context_take_results(qa_q1_movement_context *context, qa_movement_result *result);
void qa_q1_context_take_effects(qa_q1_movement_context *context, qa_movement_result *result);

#endif
