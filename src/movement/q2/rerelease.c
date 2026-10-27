/* Port of src/movement/q2/rerelease.ts. GPL-2.0-or-later. */
#include "qa/movement/q2/rerelease.h"

#include <math.h>
#include <string.h>

#include "qa/core/numeric.h"
#include "qa/movement/body_shape.h"
#include "qa/movement/q2/dimensions.h"
#include "qa/movement/q2/swept.h"
#include "qa/movement/q2/view.h"

void qa_q2_rerelease_movement_context_init(qa_q2_rerelease_movement_context *context) {
  context->pml_origin[0] = 0;
  context->pml_origin[1] = 0;
  context->pml_origin[2] = 0;
}

qa_vec3 qa_q2_rerelease_movement_context_capture(
    const qa_q2_rerelease_movement_context *context) {
  const qa_vec3 result = {context->pml_origin[0], context->pml_origin[1],
                          context->pml_origin[2]};
  return result;
}

void qa_q2_rerelease_movement_context_restore(qa_q2_rerelease_movement_context *context,
                                              qa_vec3 origin) {
  context->pml_origin[0] = origin.x;
  context->pml_origin[1] = origin.y;
  context->pml_origin[2] = origin.z;
}

static double equipment_speed(const qa_q2_rerelease_movement *movement, double value) {
  return movement->speed_multiplier == 1
             ? value
             : qa_q2_n_multiply(movement->numeric, value, movement->speed_multiplier);
}

/* TS sourceFloat(). */
static double source_float(const qa_q2_rerelease_movement *movement, double value) {
  return qa_q2_math_scalar(&movement->math, value);
}

void qa_q2_create_rerelease_movement(qa_q2_rerelease_movement *movement,
                                     const qa_numeric_operations *numeric_ops,
                                     qa_q2_rerelease_movement_context *context, bool flight,
                                     double speed_multiplier) {
  movement->numeric = numeric_ops;
  movement->context = context;
  movement->flight = flight;
  movement->speed_multiplier = speed_multiplier;
  qa_q2_movement_math_init(&movement->math, numeric_ops, QA_Q2_MATH_RERELEASE);
  movement->pm_maxspeed = equipment_speed(movement, 300);
  movement->pm_duckspeed = equipment_speed(movement, 100);
}

#define RR_PM_STOPSPEED 100
#define RR_PM_ACCELERATE 10
#define RR_PM_WATERACCELERATE 10
#define RR_PM_FRICTION 6
#define RR_PM_WATERFRICTION 1
#define RR_PM_WATERSPEED 400

typedef struct side_check {
  double normal[3];
  double mins[3];
  double maxs[3];
} side_check;

static const side_check side_checks[6] = {
    {{0, 0, 1}, {-1, -1, 0}, {1, 1, 0}},  {{0, 0, -1}, {-1, -1, 0}, {1, 1, 0}},
    {{1, 0, 0}, {0, -1, -1}, {0, 1, 1}},  {{-1, 0, 0}, {0, -1, -1}, {0, 1, 1}},
    {{0, 1, 0}, {-1, 0, -1}, {1, 0, 1}},  {{0, -1, 0}, {-1, 0, -1}, {1, 0, 1}},
};

typedef struct good_position {
  double distance;
  qa_q2_vec3 origin;
} good_position;

double qa_q2_rerelease_movement_fix_stuck_object_generic(qa_q2_rerelease_movement *movement,
                                                         qa_q2_vec3 origin, qa_q2_vec3 own_mins,
                                                         qa_q2_vec3 own_maxs,
                                                         qa_q2_stuck_object_trace_fn trace,
                                                         void *trace_ctx) {
  const qa_numeric_operations *n = movement->numeric;
  qa_q2_movement_math *m = &movement->math;
  if (!trace(trace_ctx, origin, own_mins, own_maxs, origin).startsolid)
    return QA_Q2_STUCK_GOOD_POSITION;
  good_position good_positions[6];
  size_t good_count = 0;
  for (int sn = 0; sn < 6; sn++) {
    const side_check *side = &side_checks[sn];
    qa_q2_vec3 start, mins, maxs;
    qa_q2_math_vec3(m, qa_q2_element(origin, 3, 0), qa_q2_element(origin, 3, 1),
                    qa_q2_element(origin, 3, 2), start);
    qa_q2_math_vec3(m, 0, 0, 0, mins);
    qa_q2_math_vec3(m, 0, 0, 0, maxs);
    for (int k = 0; k < 3; k++) {
      const int axis = qa_q2_axes[k];
      if (side->normal[axis] < 0)
        start[axis] = qa_q2_n_store(
            n, qa_q2_n_add(n, qa_q2_element(start, 3, axis), qa_q2_element(own_mins, 3, axis)));
      else if (side->normal[axis] > 0)
        start[axis] = qa_q2_n_store(
            n, qa_q2_n_add(n, qa_q2_element(start, 3, axis), qa_q2_element(own_maxs, 3, axis)));
      if (side->mins[axis] == -1)
        mins[axis] = qa_q2_n_store(n, qa_q2_element(own_mins, 3, axis));
      else if (side->mins[axis] == 1)
        mins[axis] = qa_q2_n_store(n, qa_q2_element(own_maxs, 3, axis));
      if (side->maxs[axis] == -1)
        maxs[axis] = qa_q2_n_store(n, qa_q2_element(own_mins, 3, axis));
      else if (side->maxs[axis] == 1)
        maxs[axis] = qa_q2_n_store(n, qa_q2_element(own_maxs, 3, axis));
    }
    qa_q2_trace tr = trace(trace_ctx, start, mins, maxs, start);
    int needed_epsilon_fix = -1;
    double needed_epsilon_dir = 0;
    if (tr.startsolid) {
      for (int k = 0; k < 3; k++) {
        const int e = qa_q2_axes[k];
        if (side->normal[e] != 0) continue;
        qa_q2_vec3 ep_start;
        qa_q2_math_vec3(m, qa_q2_element(start, 3, 0), qa_q2_element(start, 3, 1),
                        qa_q2_element(start, 3, 2), ep_start);
        ep_start[e] = qa_q2_n_store(n, qa_q2_n_add(n, qa_q2_element(ep_start, 3, e), 1));
        tr = trace(trace_ctx, ep_start, mins, maxs, ep_start);
        if (!tr.startsolid) {
          memcpy(start, ep_start, sizeof start);
          needed_epsilon_fix = e;
          needed_epsilon_dir = 1;
          break;
        }
        ep_start[e] = qa_q2_n_store(n, qa_q2_n_subtract(n, qa_q2_element(ep_start, 3, e), 2));
        tr = trace(trace_ctx, ep_start, mins, maxs, ep_start);
        if (!tr.startsolid) {
          memcpy(start, ep_start, sizeof start);
          needed_epsilon_fix = e;
          needed_epsilon_dir = -1;
          break;
        }
      }
    }
    if (tr.startsolid) continue;
    qa_q2_vec3 opposite_start;
    qa_q2_math_vec3(m, qa_q2_element(origin, 3, 0), qa_q2_element(origin, 3, 1),
                    qa_q2_element(origin, 3, 2), opposite_start);
    const side_check *other_side = &side_checks[sn ^ 1];
    for (int k = 0; k < 3; k++) {
      const int axis = qa_q2_axes[k];
      if (other_side->normal[axis] < 0)
        opposite_start[axis] = qa_q2_n_store(
            n, qa_q2_n_add(n, qa_q2_element(opposite_start, 3, axis),
                           qa_q2_element(own_mins, 3, axis)));
      else if (other_side->normal[axis] > 0)
        opposite_start[axis] = qa_q2_n_store(
            n, qa_q2_n_add(n, qa_q2_element(opposite_start, 3, axis),
                           qa_q2_element(own_maxs, 3, axis)));
    }
    if (needed_epsilon_fix >= 0)
      opposite_start[needed_epsilon_fix] = qa_q2_n_store(
          n, qa_q2_n_add(n, qa_q2_element(opposite_start, 3, needed_epsilon_fix),
                         needed_epsilon_dir));
    tr = trace(trace_ctx, start, mins, maxs, opposite_start);
    if (tr.startsolid) continue;
    qa_q2_vec3 end;
    qa_q2_math_vec3(m, qa_q2_element(tr.endpos, 3, 0), qa_q2_element(tr.endpos, 3, 1),
                    qa_q2_element(tr.endpos, 3, 2), end);
    end[0] = qa_q2_n_store(n, qa_q2_n_add(n, qa_q2_element(end, 3, 0),
                                          qa_q2_n_multiply(n, side->normal[0],
                                                           source_float(movement, 0.125))));
    end[1] = qa_q2_n_store(n, qa_q2_n_add(n, qa_q2_element(end, 3, 1),
                                          qa_q2_n_multiply(n, side->normal[1],
                                                           source_float(movement, 0.125))));
    end[2] = qa_q2_n_store(n, qa_q2_n_add(n, qa_q2_element(end, 3, 2),
                                          qa_q2_n_multiply(n, side->normal[2],
                                                           source_float(movement, 0.125))));
    qa_q2_vec3 delta, new_origin;
    qa_q2_math_vec3_sub(m, end, opposite_start, delta);
    qa_q2_math_vec3_add(m, origin, delta, new_origin);
    if (needed_epsilon_fix >= 0)
      new_origin[needed_epsilon_fix] = qa_q2_n_store(
          n, qa_q2_n_add(n, qa_q2_element(new_origin, 3, needed_epsilon_fix),
                         needed_epsilon_dir));
    tr = trace(trace_ctx, new_origin, own_mins, own_maxs, new_origin);
    if (tr.startsolid) continue;
    good_positions[good_count].distance = qa_q2_math_vec3_length_squared(m, delta);
    memcpy(good_positions[good_count].origin, new_origin, sizeof new_origin);
    good_count++;
  }
  if (good_count > 0) {
    const size_t count = good_count;
    if (count > 1) {
      /* TS sorts slice(0, n - 1) only, with the stable JS sort whose
       * comparator is numericOps.subtract (NaN compares as 0). */
      const size_t sortable = (size_t)qa_q2_n_subtract(n, (double)count, 1);
      for (size_t i = 1; i < sortable; i++) {
        const good_position key = good_positions[i];
        size_t j = i;
        while (j > 0) {
          double diff = qa_q2_n_subtract(n, good_positions[j - 1].distance, key.distance);
          if (isnan(diff)) diff = 0;
          if (diff <= 0) break;
          good_positions[j] = good_positions[j - 1];
          j--;
        }
        good_positions[j] = key;
      }
    }
    qa_q2_math_vector_copy(m, good_positions[0].origin, origin);
    return QA_Q2_STUCK_FIXED;
  }
  return QA_Q2_STUCK_NO_GOOD_POSITION;
}

static void pm_record_trace(qa_q2_kex_touch_list *touch, const qa_q2_trace *tr) {
  if (touch->num == QA_Q2_MAXTOUCH) return;
  for (int i = 0; i < touch->num; i++) {
    const qa_q2_trace *held = &touch->traces[i];
    const bool same =
        held->has_ent == tr->has_ent && (!held->has_ent || qa_q2_same_hit(held->ent, tr->ent));
    if (same) return;
  }
  touch->traces[(size_t)touch->num] = *tr;
  touch->num++;
}

/* Captures for PM_StepSlideMove_Generic's sweepQ2Body call. */
typedef struct generic_sweep {
  qa_q2_rerelease_movement *movement;
  qa_q2_vec3 *velocity;
  qa_q2_vec3 mins;
  qa_q2_vec3 maxs;
  qa_q2_kex_touch_list *touch;
  qa_q2_pm_trace_fn trace_func;
  void *trace_ctx;
} generic_sweep;

static qa_q2_trace generic_trace(void *data, qa_q2_vec3 start, qa_q2_vec3 end) {
  generic_sweep *sweep = data;
  return sweep->trace_func(sweep->trace_ctx, start, sweep->mins, sweep->maxs, end);
}

static void generic_clip(void *data, qa_q2_vec3 velocity, qa_q2_vec3 normal, qa_q2_vec3 out) {
  generic_sweep *sweep = data;
  qa_q2_math_slide_clip_velocity(&sweep->movement->math, velocity, normal,
                                 source_float(sweep->movement, 1.01), out);
}

static void generic_touch(void *data, qa_q2_trace *trace) {
  generic_sweep *sweep = data;
  pm_record_trace(sweep->touch, trace);
}

static void generic_prepare_trace(void *data, qa_q2_trace *trace) {
  generic_sweep *sweep = data;
  qa_q2_rerelease_movement *movement = sweep->movement;
  if (trace->has_surface2) {
    qa_q2_vec3 clipped_a, clipped_b;
    qa_q2_math_slide_clip_velocity(&movement->math, *sweep->velocity, trace->plane.normal,
                                   source_float(movement, 1.01), clipped_a);
    qa_q2_math_slide_clip_velocity(&movement->math, *sweep->velocity, trace->plane2.normal,
                                   source_float(movement, 1.01), clipped_b);
    bool smaller = false;
    for (int k = 0; k < 3; k++) {
      const int i = qa_q2_axes[k];
      if (fabs(qa_q2_element(clipped_a, 3, i)) < fabs(qa_q2_element(clipped_b, 3, i)))
        smaller = true;
    }
    if (smaller) {
      trace->plane = trace->plane2;
      trace->surface = trace->surface2;
      trace->has_surface = trace->has_surface2;
      trace->surface_ref = trace->surface2_ref;
    }
  }
}

static void generic_duplicate_recover(void *data, qa_q2_vec3 normal) {
  generic_sweep *sweep = data;
  qa_q2_rerelease_movement *movement = sweep->movement;
  const qa_numeric_operations *n = movement->numeric;
  qa_q2_vec3 *pml_origin = &movement->context->pml_origin;
  /* Native recovery addresses pml.origin even for probe origins. */
  (*pml_origin)[0] = qa_q2_n_store(
      n, qa_q2_n_add(n, qa_q2_element(*pml_origin, 3, 0),
                     qa_q2_n_multiply(n, qa_q2_element(normal, 3, 0),
                                      source_float(movement, 0.01))));
  (*pml_origin)[1] = qa_q2_n_store(
      n, qa_q2_n_add(n, qa_q2_element(*pml_origin, 3, 1),
                     qa_q2_n_multiply(n, qa_q2_element(normal, 3, 1),
                                      source_float(movement, 0.01))));
  qa_q2_rerelease_movement_fix_stuck_object_generic(movement, *pml_origin, sweep->mins,
                                                    sweep->maxs, sweep->trace_func,
                                                    sweep->trace_ctx);
}

void qa_q2_rerelease_movement_step_slide_move_generic(
    qa_q2_rerelease_movement *movement, qa_q2_vec3 *origin, qa_q2_vec3 *velocity,
    double frametime, qa_q2_vec3 mins, qa_q2_vec3 maxs, qa_q2_kex_touch_list *touch,
    bool has_time, qa_q2_pm_trace_fn trace_func, void *trace_ctx) {
  qa_q2_vec3 primal_velocity;
  qa_q2_math_vec3(&movement->math, qa_q2_element(*velocity, 3, 0), qa_q2_element(*velocity, 3, 1),
                  qa_q2_element(*velocity, 3, 2), primal_velocity);
  generic_sweep sweep;
  memset(&sweep, 0, sizeof sweep);
  sweep.movement = movement;
  sweep.velocity = velocity;
  memcpy(sweep.mins, mins, sizeof sweep.mins);
  memcpy(sweep.maxs, maxs, sizeof sweep.maxs);
  sweep.touch = touch;
  sweep.trace_func = trace_func;
  sweep.trace_ctx = trace_ctx;
  qa_q2_body_sweep options;
  memset(&options, 0, sizeof options);
  options.ctx = &sweep;
  options.origin = origin;
  options.velocity = velocity;
  options.elapsed = frametime;
  options.numeric = movement->numeric;
  options.trace = generic_trace;
  options.clip = generic_clip;
  options.touch = generic_touch;
  options.solid = generic_touch;
  options.prepare_trace = generic_prepare_trace;
  options.has_duplicate_plane = true;
  options.duplicate_plane_threshold = source_float(movement, 0.99);
  options.duplicate_plane_recover = generic_duplicate_recover;
  const qa_sweep_stop stop = qa_sweep_q2_body(&options);
  if (stop != QA_SWEEP_SOLID && has_time)
    qa_q2_math_vector_copy(&movement->math, primal_velocity, *velocity);
}

/* TS PmlT plus the Pmove closure state. origin aliases the shared
 * context pml_origin, exactly as in the TS. */
typedef struct rerelease_move {
  qa_q2_rerelease_movement *movement;
  const qa_numeric_operations *n;
  qa_q2_movement_math *m;
  qa_q2_kex_pmove *pm;
  qa_q2_pm_config config;
  qa_q2_vec3 *origin;
  qa_q2_vec3 velocity;
  qa_q2_vec3 forward;
  qa_q2_vec3 right;
  qa_q2_vec3 up;
  double frametime;
  bool has_groundsurface;
  qa_q2_csurface groundsurface;
  double groundcontents;
  qa_q2_vec3 previous_origin;
  qa_q2_vec3 start_velocity;
  bool has_accepted_body_bounds;
  qa_bounds accepted_body_bounds;
  double accepted_duck;
  double accepted_height;
  bool clear_called;
  bool clear_result;
} rerelease_move;

static qa_q2_trace pm_clip(rerelease_move *move, qa_q2_vec3 start, qa_q2_vec3 mins,
                           qa_q2_vec3 maxs, qa_q2_vec3 end, double mask) {
  return move->pm->clip(move->pm->ctx, start, mins, maxs, end, mask);
}

static qa_q2_trace pm_trace_mask(rerelease_move *move, qa_q2_vec3 start, qa_q2_vec3 mins,
                                 qa_q2_vec3 maxs, qa_q2_vec3 end, double mask_arg) {
  qa_q2_kex_pmove *pm = move->pm;
  if (pm->s.pm_type == QA_Q2_KEX_PM_SPECTATOR)
    return pm_clip(move, start, mins, maxs, end, QA_Q2_MASK_SOLID);
  double mask = mask_arg;
  if (mask == QA_Q2_CONTENTS_NONE) {
    if (pm->s.pm_type == QA_Q2_KEX_PM_DEAD || pm->s.pm_type == QA_Q2_KEX_PM_GIB)
      mask = QA_Q2_MASK_DEADSOLID;
    else
      mask = QA_Q2_MASK_PLAYERSOLID;
    if (qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_IGNORE_PLAYER_COLLISION))
      mask = (double)(qa_int32(mask) & ~QA_Q2_CONTENTS_PLAYER);
  }
  return pm->trace(pm->ctx, start, mins, maxs, end, pm->has_player, pm->player, mask);
}

static qa_q2_trace pm_trace(rerelease_move *move, qa_q2_vec3 start, qa_q2_vec3 mins,
                            qa_q2_vec3 maxs, qa_q2_vec3 end) {
  return pm_trace_mask(move, start, mins, maxs, end, QA_Q2_CONTENTS_NONE);
}

static qa_q2_trace trace_auto(void *data, qa_q2_vec3 start, qa_q2_vec3 mins, qa_q2_vec3 maxs,
                              qa_q2_vec3 end) {
  return pm_trace(data, start, mins, maxs, end);
}

static void pm_step_slide_move_(rerelease_move *move) {
  qa_q2_rerelease_movement_step_slide_move_generic(
      move->movement, move->origin, &move->velocity, move->frametime, move->pm->mins,
      move->pm->maxs, &move->pm->touch, move->pm->s.pm_time != 0, trace_auto, move);
}

static void pm_step_slide_move(rerelease_move *move) {
  const qa_numeric_operations *n = move->n;
  qa_q2_movement_math *m = move->m;
  qa_q2_kex_pmove *pm = move->pm;
  qa_q2_vec3 *origin = move->origin;
  qa_q2_vec3 start_o, start_v;
  qa_q2_math_vec3(m, qa_q2_element(*origin, 3, 0), qa_q2_element(*origin, 3, 1),
                  qa_q2_element(*origin, 3, 2), start_o);
  qa_q2_math_vec3(m, qa_q2_element(move->velocity, 3, 0), qa_q2_element(move->velocity, 3, 1),
                  qa_q2_element(move->velocity, 3, 2), start_v);
  pm_step_slide_move_(move);
  qa_q2_vec3 down_o, down_v;
  qa_q2_math_vec3(m, qa_q2_element(*origin, 3, 0), qa_q2_element(*origin, 3, 1),
                  qa_q2_element(*origin, 3, 2), down_o);
  qa_q2_math_vec3(m, qa_q2_element(move->velocity, 3, 0), qa_q2_element(move->velocity, 3, 1),
                  qa_q2_element(move->velocity, 3, 2), down_v);
  qa_q2_vec3 up;
  qa_q2_math_vec3(m, qa_q2_element(start_o, 3, 0), qa_q2_element(start_o, 3, 1),
                  qa_q2_n_add(n, qa_q2_element(start_o, 3, 2), QA_Q2_STEPSIZE), up);
  qa_q2_trace trace = pm_trace(move, start_o, pm->mins, pm->maxs, up);
  if (trace.allsolid) return;
  const double step_size =
      qa_q2_n_subtract(n, qa_q2_element(trace.endpos, 3, 2), qa_q2_element(start_o, 3, 2));
  qa_q2_math_vector_copy(m, trace.endpos, *origin);
  qa_q2_math_vector_copy(m, start_v, move->velocity);
  pm_step_slide_move_(move);
  qa_q2_vec3 down, original_down;
  qa_q2_math_vec3(m, qa_q2_element(*origin, 3, 0), qa_q2_element(*origin, 3, 1),
                  qa_q2_n_subtract(n, qa_q2_element(*origin, 3, 2), step_size), down);
  qa_q2_math_vec3(m, qa_q2_element(down, 3, 0), qa_q2_element(down, 3, 1),
                  qa_q2_element(down, 3, 2), original_down);
  if (qa_q2_element(start_o, 3, 2) < qa_q2_element(down, 3, 2))
    down[2] = qa_q2_n_store(n, qa_q2_n_subtract(n, qa_q2_element(start_o, 3, 2), 1));
  trace = pm_trace(move, *origin, pm->mins, pm->maxs, down);
  if (!trace.allsolid) {
    const qa_q2_trace real_trace = pm_trace(move, *origin, pm->mins, pm->maxs, original_down);
    qa_q2_math_vector_copy(m, real_trace.endpos, *origin);
    if (qa_q2_element(move->velocity, 3, 2) > 0) pm->step_clip = true;
  }
  qa_q2_vec3 up_end;
  qa_q2_math_vec3(m, qa_q2_element(*origin, 3, 0), qa_q2_element(*origin, 3, 1),
                  qa_q2_element(*origin, 3, 2), up_end);
  const double down_dist = qa_q2_n_add(
      n,
      qa_q2_n_multiply(n, qa_q2_n_subtract(n, qa_q2_element(down_o, 3, 0), qa_q2_element(start_o, 3, 0)),
                       qa_q2_n_subtract(n, qa_q2_element(down_o, 3, 0), qa_q2_element(start_o, 3, 0))),
      qa_q2_n_multiply(n, qa_q2_n_subtract(n, qa_q2_element(down_o, 3, 1), qa_q2_element(start_o, 3, 1)),
                       qa_q2_n_subtract(n, qa_q2_element(down_o, 3, 1), qa_q2_element(start_o, 3, 1))));
  const double up_dist = qa_q2_n_add(
      n,
      qa_q2_n_multiply(n, qa_q2_n_subtract(n, qa_q2_element(up_end, 3, 0), qa_q2_element(start_o, 3, 0)),
                       qa_q2_n_subtract(n, qa_q2_element(up_end, 3, 0), qa_q2_element(start_o, 3, 0))),
      qa_q2_n_multiply(n, qa_q2_n_subtract(n, qa_q2_element(up_end, 3, 1), qa_q2_element(start_o, 3, 1)),
                       qa_q2_n_subtract(n, qa_q2_element(up_end, 3, 1), qa_q2_element(start_o, 3, 1))));
  if (down_dist > up_dist ||
      qa_q2_element(trace.plane.normal, 3, 2) < source_float(move->movement, 0.7)) {
    qa_q2_math_vector_copy(m, down_o, *origin);
    qa_q2_math_vector_copy(m, down_v, move->velocity);
  } else {
    move->velocity[2] = qa_q2_n_store(n, qa_q2_element(down_v, 3, 2));
  }
  if (qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_ON_GROUND) &&
      !qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_ON_LADDER) &&
      (pm->waterlevel < QA_Q2_WATER_WAIST ||
       (!(qa_int32(pm->cmd.buttons) & QA_Q2_BUTTON_JUMP) &&
        qa_q2_element(move->velocity, 3, 2) <= 0))) {
    qa_q2_vec3 down_stairs;
    qa_q2_math_vec3(m, qa_q2_element(*origin, 3, 0), qa_q2_element(*origin, 3, 1),
                    qa_q2_n_subtract(n, qa_q2_element(*origin, 3, 2), QA_Q2_STEPSIZE), down_stairs);
    trace = pm_trace(move, *origin, pm->mins, pm->maxs, down_stairs);
    if (trace.fraction < 1) qa_q2_math_vector_copy(m, trace.endpos, *origin);
  }
}

static void pm_friction(rerelease_move *move) {
  const qa_numeric_operations *n = move->n;
  qa_q2_kex_pmove *pm = move->pm;
  qa_q2_vec3 *vel = &move->velocity;
  const double speed = qa_q2_n_square_root(
      n, qa_q2_n_add(n,
                     qa_q2_n_add(n, qa_q2_n_multiply(n, qa_q2_element(*vel, 3, 0), qa_q2_element(*vel, 3, 0)),
                                 qa_q2_n_multiply(n, qa_q2_element(*vel, 3, 1), qa_q2_element(*vel, 3, 1))),
                     qa_q2_n_multiply(n, qa_q2_element(*vel, 3, 2), qa_q2_element(*vel, 3, 2))));
  if (speed < 1) {
    (*vel)[0] = qa_q2_n_store(n, 0);
    (*vel)[1] = qa_q2_n_store(n, 0);
    return;
  }
  double drop = 0;
  if ((pm->has_groundentity && move->has_groundsurface &&
       !(qa_int32(move->groundsurface.flags) & QA_Q2_SURF_SLICK)) ||
      qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_ON_LADDER)) {
    const double friction = RR_PM_FRICTION;
    const double control = speed < RR_PM_STOPSPEED ? RR_PM_STOPSPEED : speed;
    drop = qa_q2_n_add(n, drop,
                       qa_q2_n_multiply(n, qa_q2_n_multiply(n, control, friction), move->frametime));
  }
  if (qa_q2_js_truthy(pm->waterlevel) &&
      !qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_ON_LADDER)) {
    drop = qa_q2_n_add(
        n, drop,
        qa_q2_n_multiply(n, qa_q2_n_multiply(n, qa_q2_n_multiply(n, speed, RR_PM_WATERFRICTION),
                                             pm->waterlevel),
                         move->frametime));
  }
  double newspeed = qa_q2_n_subtract(n, speed, drop);
  if (newspeed < 0) newspeed = 0;
  newspeed = qa_q2_n_divide(n, newspeed, speed);
  (*vel)[0] = qa_q2_n_store(n, qa_q2_n_multiply(n, qa_q2_element(*vel, 3, 0), newspeed));
  (*vel)[1] = qa_q2_n_store(n, qa_q2_n_multiply(n, qa_q2_element(*vel, 3, 1), newspeed));
  (*vel)[2] = qa_q2_n_store(n, qa_q2_n_multiply(n, qa_q2_element(*vel, 3, 2), newspeed));
}

static void pm_accelerate(rerelease_move *move, const qa_q2_vec3 wishdir, double wishspeed,
                          double accel) {
  const qa_numeric_operations *n = move->n;
  const double currentspeed = qa_q2_math_vec3_dot(move->m, move->velocity, wishdir);
  const double addspeed = qa_q2_n_subtract(n, wishspeed, currentspeed);
  if (addspeed <= 0) return;
  double accelspeed = qa_q2_n_multiply(n, qa_q2_n_multiply(n, accel, move->frametime), wishspeed);
  if (accelspeed > addspeed) accelspeed = addspeed;
  move->velocity[0] = qa_q2_n_store(
      n, qa_q2_n_add(n, qa_q2_element(move->velocity, 3, 0),
                     qa_q2_n_multiply(n, accelspeed, qa_q2_element(wishdir, 3, 0))));
  move->velocity[1] = qa_q2_n_store(
      n, qa_q2_n_add(n, qa_q2_element(move->velocity, 3, 1),
                     qa_q2_n_multiply(n, accelspeed, qa_q2_element(wishdir, 3, 1))));
  move->velocity[2] = qa_q2_n_store(
      n, qa_q2_n_add(n, qa_q2_element(move->velocity, 3, 2),
                     qa_q2_n_multiply(n, accelspeed, qa_q2_element(wishdir, 3, 2))));
}

static void pm_air_accelerate(rerelease_move *move, const qa_q2_vec3 wishdir, double wishspeed,
                              double accel) {
  const qa_numeric_operations *n = move->n;
  double wishspd = wishspeed;
  if (wishspd > 30) wishspd = 30;
  const double currentspeed = qa_q2_math_vec3_dot(move->m, move->velocity, wishdir);
  const double addspeed = qa_q2_n_subtract(n, wishspd, currentspeed);
  if (addspeed <= 0) return;
  double accelspeed = qa_q2_n_multiply(n, qa_q2_n_multiply(n, accel, wishspeed), move->frametime);
  if (accelspeed > addspeed) accelspeed = addspeed;
  move->velocity[0] = qa_q2_n_store(
      n, qa_q2_n_add(n, qa_q2_element(move->velocity, 3, 0),
                     qa_q2_n_multiply(n, accelspeed, qa_q2_element(wishdir, 3, 0))));
  move->velocity[1] = qa_q2_n_store(
      n, qa_q2_n_add(n, qa_q2_element(move->velocity, 3, 1),
                     qa_q2_n_multiply(n, accelspeed, qa_q2_element(wishdir, 3, 1))));
  move->velocity[2] = qa_q2_n_store(
      n, qa_q2_n_add(n, qa_q2_element(move->velocity, 3, 2),
                     qa_q2_n_multiply(n, accelspeed, qa_q2_element(wishdir, 3, 2))));
}

static void pm_add_currents(rerelease_move *move, qa_q2_vec3 wishvel) {
  const qa_numeric_operations *n = move->n;
  qa_q2_movement_math *m = move->m;
  qa_q2_kex_pmove *pm = move->pm;
  qa_q2_vec3 *origin = move->origin;
  if (qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_ON_LADDER)) {
    if (qa_int32(pm->cmd.buttons) & (QA_Q2_BUTTON_JUMP | QA_Q2_BUTTON_CROUCH)) {
      const double ladder_speed =
          pm->waterlevel >= QA_Q2_WATER_WAIST ? move->movement->pm_maxspeed : 200;
      if (qa_int32(pm->cmd.buttons) & QA_Q2_BUTTON_JUMP)
        wishvel[2] = qa_q2_n_store(n, ladder_speed);
      else if (qa_int32(pm->cmd.buttons) & QA_Q2_BUTTON_CROUCH)
        wishvel[2] = qa_q2_n_store(n, -ladder_speed);
    } else if (qa_q2_js_truthy(pm->cmd.forwardmove)) {
      const double ladder_speed = qa_q2_math_clamp(m, pm->cmd.forwardmove, -200, 200);
      if (pm->cmd.forwardmove > 0) {
        if (qa_q2_element(pm->viewangles, 3, QA_Q2_PITCH) < 15)
          wishvel[2] = qa_q2_n_store(n, ladder_speed);
        else
          wishvel[2] = qa_q2_n_store(n, -ladder_speed);
      } else if (pm->cmd.forwardmove < 0) {
        if (!pm->has_groundentity) {
          wishvel[0] = qa_q2_n_store(n, 0);
          wishvel[1] = qa_q2_n_store(n, 0);
        }
        wishvel[2] = qa_q2_n_store(n, ladder_speed);
      }
    } else {
      wishvel[2] = qa_q2_n_store(n, 0);
    }
    if (!pm->has_groundentity) {
      if (qa_q2_js_truthy(pm->cmd.sidemove)) {
        double ladder_speed = qa_q2_math_clamp(m, pm->cmd.sidemove, -150, 150);
        if (pm->waterlevel < QA_Q2_WATER_WAIST)
          ladder_speed =
              qa_q2_n_multiply(n, ladder_speed, source_float(move->movement, 0.5));
        qa_q2_vec3 flatforward, spot, scaled, right, up_axis, product;
        qa_q2_math_vec3(m, qa_q2_element(move->forward, 3, 0), qa_q2_element(move->forward, 3, 1),
                        0, flatforward);
        qa_q2_math_vec3_normalize(m, flatforward);
        qa_q2_math_vec3_muls(m, flatforward, 1, scaled);
        qa_q2_math_vec3_add(m, *origin, scaled, spot);
        const qa_q2_trace trace =
            pm_trace_mask(move, *origin, pm->mins, pm->maxs, spot, QA_Q2_CONTENTS_LADDER);
        if (trace.fraction != 1 && (qa_int32(trace.contents) & QA_Q2_CONTENTS_LADDER)) {
          qa_q2_math_vec3(m, 0, 0, 1, up_axis);
          qa_q2_math_vec3_cross(m, trace.plane.normal, up_axis, right);
          wishvel[0] = qa_q2_n_store(n, 0);
          wishvel[1] = qa_q2_n_store(n, 0);
          qa_q2_math_vec3_muls(m, right, -ladder_speed, product);
          qa_q2_math_vec3_add_eq(m, wishvel, product);
        }
      } else {
        if (qa_q2_element(wishvel, 3, 0) < -25)
          wishvel[0] = qa_q2_n_store(n, -25);
        else if (qa_q2_element(wishvel, 3, 0) > 25)
          wishvel[0] = qa_q2_n_store(n, 25);
        if (qa_q2_element(wishvel, 3, 1) < -25)
          wishvel[1] = qa_q2_n_store(n, -25);
        else if (qa_q2_element(wishvel, 3, 1) > 25)
          wishvel[1] = qa_q2_n_store(n, 25);
      }
    }
  }
  if (qa_int32(pm->watertype) & QA_Q2_MASK_CURRENT) {
    qa_q2_vec3 v, scaled;
    qa_q2_math_vec3(m, 0, 0, 0, v);
    if (qa_int32(pm->watertype) & QA_Q2_CONTENTS_CURRENT_0)
      v[0] = qa_q2_n_store(n, qa_q2_n_add(n, qa_q2_element(v, 3, 0), 1));
    if (qa_int32(pm->watertype) & QA_Q2_CONTENTS_CURRENT_90)
      v[1] = qa_q2_n_store(n, qa_q2_n_add(n, qa_q2_element(v, 3, 1), 1));
    if (qa_int32(pm->watertype) & QA_Q2_CONTENTS_CURRENT_180)
      v[0] = qa_q2_n_store(n, qa_q2_n_subtract(n, qa_q2_element(v, 3, 0), 1));
    if (qa_int32(pm->watertype) & QA_Q2_CONTENTS_CURRENT_270)
      v[1] = qa_q2_n_store(n, qa_q2_n_subtract(n, qa_q2_element(v, 3, 1), 1));
    if (qa_int32(pm->watertype) & QA_Q2_CONTENTS_CURRENT_UP)
      v[2] = qa_q2_n_store(n, qa_q2_n_add(n, qa_q2_element(v, 3, 2), 1));
    if (qa_int32(pm->watertype) & QA_Q2_CONTENTS_CURRENT_DOWN)
      v[2] = qa_q2_n_store(n, qa_q2_n_subtract(n, qa_q2_element(v, 3, 2), 1));
    double s = RR_PM_WATERSPEED;
    if (pm->waterlevel == QA_Q2_WATER_FEET && pm->has_groundentity)
      s = qa_q2_n_divide(n, s, 2);
    qa_q2_math_vec3_muls(m, v, s, scaled);
    qa_q2_math_vec3_add_eq(m, wishvel, scaled);
  }
  if (pm->has_groundentity) {
    qa_q2_vec3 v, scaled;
    qa_q2_math_vec3(m, 0, 0, 0, v);
    if (qa_int32(move->groundcontents) & QA_Q2_CONTENTS_CURRENT_0)
      v[0] = qa_q2_n_store(n, qa_q2_n_add(n, qa_q2_element(v, 3, 0), 1));
    if (qa_int32(move->groundcontents) & QA_Q2_CONTENTS_CURRENT_90)
      v[1] = qa_q2_n_store(n, qa_q2_n_add(n, qa_q2_element(v, 3, 1), 1));
    if (qa_int32(move->groundcontents) & QA_Q2_CONTENTS_CURRENT_180)
      v[0] = qa_q2_n_store(n, qa_q2_n_subtract(n, qa_q2_element(v, 3, 0), 1));
    if (qa_int32(move->groundcontents) & QA_Q2_CONTENTS_CURRENT_270)
      v[1] = qa_q2_n_store(n, qa_q2_n_subtract(n, qa_q2_element(v, 3, 1), 1));
    if (qa_int32(move->groundcontents) & QA_Q2_CONTENTS_CURRENT_UP)
      v[2] = qa_q2_n_store(n, qa_q2_n_add(n, qa_q2_element(v, 3, 2), 1));
    if (qa_int32(move->groundcontents) & QA_Q2_CONTENTS_CURRENT_DOWN)
      v[2] = qa_q2_n_store(n, qa_q2_n_subtract(n, qa_q2_element(v, 3, 2), 1));
    qa_q2_math_vec3_muls(m, v, 100, scaled);
    qa_q2_math_vec3_add_eq(m, wishvel, scaled);
  }
}

static void pm_water_move(rerelease_move *move) {
  const qa_numeric_operations *n = move->n;
  qa_q2_movement_math *m = move->m;
  qa_q2_kex_pmove *pm = move->pm;
  qa_q2_vec3 wishvel, wishdir;
  qa_q2_math_vec3(
      m,
      qa_q2_n_add(n, qa_q2_n_multiply(n, qa_q2_element(move->forward, 3, 0),
                                      equipment_speed(move->movement, pm->cmd.forwardmove)),
                  qa_q2_n_multiply(n, qa_q2_element(move->right, 3, 0),
                                   equipment_speed(move->movement, pm->cmd.sidemove))),
      qa_q2_n_add(n, qa_q2_n_multiply(n, qa_q2_element(move->forward, 3, 1),
                                      equipment_speed(move->movement, pm->cmd.forwardmove)),
                  qa_q2_n_multiply(n, qa_q2_element(move->right, 3, 1),
                                   equipment_speed(move->movement, pm->cmd.sidemove))),
      qa_q2_n_add(n, qa_q2_n_multiply(n, qa_q2_element(move->forward, 3, 2),
                                      equipment_speed(move->movement, pm->cmd.forwardmove)),
                  qa_q2_n_multiply(n, qa_q2_element(move->right, 3, 2),
                                   equipment_speed(move->movement, pm->cmd.sidemove))),
      wishvel);
  if (!qa_q2_js_truthy(pm->cmd.forwardmove) && !qa_q2_js_truthy(pm->cmd.sidemove) &&
      !(qa_int32(pm->cmd.buttons) & (QA_Q2_BUTTON_JUMP | QA_Q2_BUTTON_CROUCH))) {
    if (!pm->has_groundentity)
      wishvel[2] = qa_q2_n_store(n, qa_q2_n_subtract(n, qa_q2_element(wishvel, 3, 2), 60));
  } else {
    if (qa_int32(pm->cmd.buttons) & QA_Q2_BUTTON_CROUCH)
      wishvel[2] = qa_q2_n_store(
          n, qa_q2_n_subtract(n, qa_q2_element(wishvel, 3, 2),
                              equipment_speed(move->movement,
                                              qa_q2_n_multiply(n, RR_PM_WATERSPEED,
                                                               source_float(move->movement, 0.5)))));
    else if (qa_int32(pm->cmd.buttons) & QA_Q2_BUTTON_JUMP)
      wishvel[2] = qa_q2_n_store(
          n, qa_q2_n_add(n, qa_q2_element(wishvel, 3, 2),
                         equipment_speed(move->movement,
                                         qa_q2_n_multiply(n, RR_PM_WATERSPEED,
                                                          source_float(move->movement, 0.5)))));
  }
  pm_add_currents(move, wishvel);
  qa_q2_math_vec3(m, qa_q2_element(wishvel, 3, 0), qa_q2_element(wishvel, 3, 1),
                  qa_q2_element(wishvel, 3, 2), wishdir);
  double wishspeed = qa_q2_math_vec3_normalize(m, wishdir);
  if (wishspeed > move->movement->pm_maxspeed) {
    qa_q2_math_vec3_mul_eqs(m, wishvel,
                            qa_q2_n_divide(n, move->movement->pm_maxspeed, wishspeed));
    wishspeed = move->movement->pm_maxspeed;
  }
  wishspeed = qa_q2_n_multiply(n, wishspeed, source_float(move->movement, 0.5));
  if (qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_DUCKED) &&
      wishspeed > move->movement->pm_duckspeed) {
    qa_q2_math_vec3_mul_eqs(m, wishvel,
                            qa_q2_n_divide(n, move->movement->pm_duckspeed, wishspeed));
    wishspeed = move->movement->pm_duckspeed;
  }
  pm_accelerate(move, wishdir, wishspeed, RR_PM_WATERACCELERATE);
  pm_step_slide_move(move);
}

static void pm_air_move(rerelease_move *move) {
  const qa_numeric_operations *n = move->n;
  qa_q2_movement_math *m = move->m;
  qa_q2_kex_pmove *pm = move->pm;
  const double fmove = equipment_speed(move->movement, pm->cmd.forwardmove);
  const double smove = equipment_speed(move->movement, pm->cmd.sidemove);
  qa_q2_vec3 wishvel, wishdir;
  qa_q2_math_vec3(
      m,
      qa_q2_n_add(n, qa_q2_n_multiply(n, qa_q2_element(move->forward, 3, 0), fmove),
                  qa_q2_n_multiply(n, qa_q2_element(move->right, 3, 0), smove)),
      qa_q2_n_add(n, qa_q2_n_multiply(n, qa_q2_element(move->forward, 3, 1), fmove),
                  qa_q2_n_multiply(n, qa_q2_element(move->right, 3, 1), smove)),
      0, wishvel);
  pm_add_currents(move, wishvel);
  qa_q2_math_vec3(m, qa_q2_element(wishvel, 3, 0), qa_q2_element(wishvel, 3, 1),
                  qa_q2_element(wishvel, 3, 2), wishdir);
  double wishspeed = qa_q2_math_vec3_normalize(m, wishdir);
  const double maxspeed = qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_DUCKED)
                              ? move->movement->pm_duckspeed
                              : move->movement->pm_maxspeed;
  if (wishspeed > maxspeed) {
    qa_q2_math_vec3_mul_eqs(m, wishvel, qa_q2_n_divide(n, maxspeed, wishspeed));
    wishspeed = maxspeed;
  }
  if (qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_ON_LADDER)) {
    pm_accelerate(move, wishdir, wishspeed, RR_PM_ACCELERATE);
    if (!qa_q2_js_truthy(qa_q2_element(wishvel, 3, 2))) {
      if (qa_q2_element(move->velocity, 3, 2) > 0) {
        move->velocity[2] = qa_q2_n_store(
            n, qa_q2_n_subtract(n, qa_q2_element(move->velocity, 3, 2),
                                qa_q2_n_multiply(n, pm->s.gravity, move->frametime)));
        if (qa_q2_element(move->velocity, 3, 2) < 0) move->velocity[2] = qa_q2_n_store(n, 0);
      } else {
        move->velocity[2] = qa_q2_n_store(
            n, qa_q2_n_add(n, qa_q2_element(move->velocity, 3, 2),
                           qa_q2_n_multiply(n, pm->s.gravity, move->frametime)));
        if (qa_q2_element(move->velocity, 3, 2) > 0) move->velocity[2] = qa_q2_n_store(n, 0);
      }
    }
    pm_step_slide_move(move);
  } else if (pm->has_groundentity) {
    move->velocity[2] = qa_q2_n_store(n, 0);
    pm_accelerate(move, wishdir, wishspeed, RR_PM_ACCELERATE);
    if (pm->s.gravity > 0)
      move->velocity[2] = qa_q2_n_store(n, 0);
    else
      move->velocity[2] = qa_q2_n_store(
          n, qa_q2_n_subtract(n, qa_q2_element(move->velocity, 3, 2),
                              qa_q2_n_multiply(n, pm->s.gravity, move->frametime)));
    if (!qa_q2_js_truthy(qa_q2_element(move->velocity, 3, 0)) &&
        !qa_q2_js_truthy(qa_q2_element(move->velocity, 3, 1)))
      return;
    pm_step_slide_move(move);
  } else {
    if (qa_q2_js_truthy(move->config.airaccel))
      pm_air_accelerate(move, wishdir, wishspeed, move->config.airaccel);
    else
      pm_accelerate(move, wishdir, wishspeed, 1);
    if (pm->s.pm_type != QA_Q2_KEX_PM_GRAPPLE)
      move->velocity[2] = qa_q2_n_store(
          n, qa_q2_n_subtract(n, qa_q2_element(move->velocity, 3, 2),
                              qa_q2_n_multiply(n, pm->s.gravity, move->frametime)));
    pm_step_slide_move(move);
  }
}

typedef struct water_level {
  double level;
  double type;
} water_level;

static water_level pm_get_water_level(rerelease_move *move, const qa_q2_vec3 position) {
  const qa_numeric_operations *n = move->n;
  qa_q2_movement_math *m = move->m;
  qa_q2_kex_pmove *pm = move->pm;
  qa_q2_vec3 *origin = move->origin;
  water_level result = {QA_Q2_WATER_NONE, QA_Q2_CONTENTS_NONE};
  const double sample2 =
      trunc(qa_q2_n_subtract(n, pm->s.viewheight, qa_q2_element(pm->mins, 3, 2)));
  const double sample1 = trunc(qa_q2_n_divide(n, sample2, 2));
  qa_q2_vec3 point;
  qa_q2_math_vec3(m, qa_q2_element(position, 3, 0), qa_q2_element(position, 3, 1),
                  qa_q2_n_add(n, qa_q2_n_add(n, qa_q2_element(position, 3, 2),
                                             qa_q2_element(pm->mins, 3, 2)),
                              1),
                  point);
  double cont = pm->pointcontents(pm->ctx, point);
  if (qa_int32(cont) & QA_Q2_MASK_WATER) {
    result.type = cont;
    result.level = QA_Q2_WATER_FEET;
    point[2] = qa_q2_n_store(n, qa_q2_n_add(n, qa_q2_n_add(n, qa_q2_element(*origin, 3, 2),
                                                           qa_q2_element(pm->mins, 3, 2)),
                                            sample1));
    cont = pm->pointcontents(pm->ctx, point);
    if (qa_int32(cont) & QA_Q2_MASK_WATER) {
      result.level = QA_Q2_WATER_WAIST;
      point[2] = qa_q2_n_store(n, qa_q2_n_add(n, qa_q2_n_add(n, qa_q2_element(*origin, 3, 2),
                                                             qa_q2_element(pm->mins, 3, 2)),
                                              sample2));
      cont = pm->pointcontents(pm->ctx, point);
      if (qa_int32(cont) & QA_Q2_MASK_WATER) result.level = QA_Q2_WATER_UNDER;
    }
  }
  return result;
}

static void pm_catagorize_position(rerelease_move *move) {
  const qa_numeric_operations *n = move->n;
  qa_q2_movement_math *m = move->m;
  qa_q2_kex_pmove *pm = move->pm;
  qa_q2_vec3 *origin = move->origin;
  qa_q2_vec3 point;
  qa_q2_math_vec3(m, qa_q2_element(*origin, 3, 0), qa_q2_element(*origin, 3, 1),
                  qa_q2_n_subtract(n, qa_q2_element(*origin, 3, 2),
                                   source_float(move->movement, 0.25)),
                  point);
  if (qa_q2_element(move->velocity, 3, 2) > 180 || pm->s.pm_type == QA_Q2_KEX_PM_GRAPPLE) {
    pm->s.pm_flags = qa_q2_flag_clear(pm->s.pm_flags, QA_Q2_PMF_ON_GROUND);
    pm->has_groundentity = false;
  } else {
    const qa_q2_trace trace = pm_trace(move, *origin, pm->mins, pm->maxs, point);
    pm->groundplane = trace.plane;
    move->has_groundsurface = trace.has_surface;
    move->groundsurface = trace.surface;
    move->groundcontents = trace.contents;
    bool slanted_ground = trace.fraction < 1 &&
                          qa_q2_element(trace.plane.normal, 3, 2) <
                              source_float(move->movement, 0.7);
    if (slanted_ground) {
      qa_q2_vec3 slant_end;
      qa_q2_math_vec3_add(m, *origin, trace.plane.normal, slant_end);
      const qa_q2_trace slant = pm_trace(move, *origin, pm->mins, pm->maxs, slant_end);
      if (slant.fraction < 1 && !slant.startsolid) slanted_ground = false;
    }
    if (trace.fraction == 1 || (slanted_ground && !trace.startsolid)) {
      pm->has_groundentity = false;
      pm->s.pm_flags = qa_q2_flag_clear(pm->s.pm_flags, QA_Q2_PMF_ON_GROUND);
    } else {
      pm->has_groundentity = trace.has_ent;
      pm->groundentity = trace.ent;
      if (qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_TIME_WATERJUMP)) {
        pm->s.pm_flags = qa_q2_flag_clear(pm->s.pm_flags, QA_Q2_PMF_TIME_WATERJUMP |
                                                              QA_Q2_PMF_TIME_LAND |
                                                              QA_Q2_PMF_TIME_TELEPORT |
                                                              QA_Q2_PMF_TIME_TRICK);
        pm->s.pm_time = 0;
      }
      if (!qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_ON_GROUND)) {
        if (!move->config.n64_physics && qa_q2_element(move->velocity, 3, 2) >= 100 &&
            qa_q2_element(pm->groundplane.normal, 3, 2) >= source_float(move->movement, 0.9) &&
            !qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_DUCKED)) {
          pm->s.pm_flags = qa_q2_flag_set(pm->s.pm_flags, QA_Q2_PMF_TIME_TRICK);
          pm->s.pm_time = 64;
        }
        qa_q2_vec3 clipped_velocity;
        qa_q2_math_slide_clip_velocity(m, move->velocity, pm->groundplane.normal,
                                       source_float(move->movement, 1.01), clipped_velocity);
        pm->impact_delta = qa_q2_n_subtract(n, qa_q2_element(move->start_velocity, 3, 2),
                                            qa_q2_element(clipped_velocity, 3, 2));
        pm->s.pm_flags = qa_q2_flag_set(pm->s.pm_flags, QA_Q2_PMF_ON_GROUND);
        if (move->config.n64_physics || qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_DUCKED)) {
          pm->s.pm_flags = qa_q2_flag_set(pm->s.pm_flags, QA_Q2_PMF_TIME_LAND);
          pm->s.pm_time = 128;
        }
      }
    }
    pm_record_trace(&pm->touch, &trace);
  }
  const water_level water = pm_get_water_level(move, *origin);
  pm->waterlevel = water.level;
  pm->watertype = water.type;
}

static void pm_check_jump(rerelease_move *move) {
  const qa_numeric_operations *n = move->n;
  qa_q2_kex_pmove *pm = move->pm;
  if (qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_TIME_LAND)) return;
  if (!(qa_int32(pm->cmd.buttons) & QA_Q2_BUTTON_JUMP)) {
    pm->s.pm_flags = qa_q2_flag_clear(pm->s.pm_flags, QA_Q2_PMF_JUMP_HELD);
    return;
  }
  if (qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_JUMP_HELD)) return;
  if (pm->s.pm_type == QA_Q2_KEX_PM_DEAD) return;
  if (pm->waterlevel >= QA_Q2_WATER_WAIST) {
    pm->has_groundentity = false;
    return;
  }
  if (!pm->has_groundentity) return;
  pm->s.pm_flags = qa_q2_flag_set(pm->s.pm_flags, QA_Q2_PMF_JUMP_HELD);
  pm->jump_sound = true;
  pm->has_groundentity = false;
  pm->s.pm_flags = qa_q2_flag_clear(pm->s.pm_flags, QA_Q2_PMF_ON_GROUND);
  const double jump_height = 270;
  move->velocity[2] =
      qa_q2_n_store(n, qa_q2_n_add(n, qa_q2_element(move->velocity, 3, 2), jump_height));
  if (qa_q2_element(move->velocity, 3, 2) < jump_height)
    move->velocity[2] = qa_q2_n_store(n, jump_height);
}

static void pm_check_special_movement(rerelease_move *move) {
  const qa_numeric_operations *n = move->n;
  qa_q2_movement_math *m = move->m;
  qa_q2_kex_pmove *pm = move->pm;
  qa_q2_vec3 *origin = move->origin;
  if (qa_q2_js_truthy(pm->s.pm_time)) return;
  pm->s.pm_flags = qa_q2_flag_clear(pm->s.pm_flags, QA_Q2_PMF_ON_LADDER);
  qa_q2_vec3 flatforward, scaled, spot;
  qa_q2_math_vec3(m, qa_q2_element(move->forward, 3, 0), qa_q2_element(move->forward, 3, 1), 0,
                  flatforward);
  qa_q2_math_vec3_normalize(m, flatforward);
  qa_q2_math_vec3_muls(m, flatforward, 1, scaled);
  qa_q2_math_vec3_add(m, *origin, scaled, spot);
  qa_q2_trace trace = pm_trace_mask(move, *origin, pm->mins, pm->maxs, spot, QA_Q2_CONTENTS_LADDER);
  if (trace.fraction < 1 && (qa_int32(trace.contents) & QA_Q2_CONTENTS_LADDER) &&
      pm->waterlevel < QA_Q2_WATER_WAIST) {
    pm->s.pm_flags = qa_q2_flag_set(pm->s.pm_flags, QA_Q2_PMF_ON_LADDER);
  }
  if (!qa_q2_js_truthy(pm->s.gravity)) return;
  if (!(qa_int32(pm->cmd.buttons) & QA_Q2_BUTTON_JUMP) && pm->cmd.forwardmove <= 0) return;
  if (pm->waterlevel != QA_Q2_WATER_WAIST)
    return;
  else if (qa_int32(pm->watertype) & QA_Q2_CONTENTS_NO_WATERJUMP)
    return;
  qa_q2_vec3 scaled40, spot40;
  qa_q2_math_vec3_muls(m, flatforward, 40, scaled40);
  qa_q2_math_vec3_add(m, *origin, scaled40, spot40);
  trace = pm_trace_mask(move, *origin, pm->mins, pm->maxs, spot40, QA_Q2_MASK_SOLID);
  if (trace.fraction == 1 ||
      qa_q2_element(trace.plane.normal, 3, 2) >= source_float(move->movement, 0.7))
    return;
  qa_q2_vec3 waterjump_vel, waterjump_origin;
  qa_q2_math_vec3_muls(m, flatforward, 50, waterjump_vel);
  waterjump_vel[2] = qa_q2_n_store(n, 350);
  qa_q2_kex_touch_list touches;
  memset(&touches, 0, sizeof touches);
  touches.num = 0;
  qa_q2_math_vec3(m, qa_q2_element(*origin, 3, 0), qa_q2_element(*origin, 3, 1),
                  qa_q2_element(*origin, 3, 2), waterjump_origin);
  const double time = source_float(move->movement, 0.1);
  bool has_time = true;
  const double iter_count =
      qa_q2_js_min(50, trunc(qa_q2_n_multiply(n, 10, qa_q2_n_divide(n, 800, pm->s.gravity))));
  for (double i = 0; i < iter_count; i++) {
    waterjump_vel[2] = qa_q2_n_store(
        n, qa_q2_n_subtract(n, qa_q2_element(waterjump_vel, 3, 2),
                            qa_q2_n_multiply(n, pm->s.gravity, time)));
    if (qa_q2_element(waterjump_vel, 3, 2) < 0) has_time = false;
    qa_q2_rerelease_movement_step_slide_move_generic(move->movement, &waterjump_origin,
                                                     &waterjump_vel, time, pm->mins, pm->maxs,
                                                     &touches, has_time, trace_auto, move);
  }
  qa_q2_vec3 drop2, probe_end;
  qa_q2_math_vec3(m, 0, 0, 2, drop2);
  qa_q2_math_vec3_sub(m, waterjump_origin, drop2, probe_end);
  trace = pm_trace_mask(move, waterjump_origin, pm->mins, pm->maxs, probe_end, QA_Q2_MASK_SOLID);
  if (trace.fraction == 1 ||
      qa_q2_element(trace.plane.normal, 3, 2) < source_float(move->movement, 0.7) ||
      qa_q2_element(trace.endpos, 3, 2) < qa_q2_element(*origin, 3, 2))
    return;
  if (pm->has_groundentity &&
      fabs(qa_q2_n_subtract(n, qa_q2_element(*origin, 3, 2), qa_q2_element(trace.endpos, 3, 2))) <=
          QA_Q2_STEPSIZE)
    return;
  const water_level water = pm_get_water_level(move, trace.endpos);
  if (water.level >= QA_Q2_WATER_WAIST) return;
  move->velocity[0] =
      qa_q2_n_store(n, qa_q2_n_multiply(n, qa_q2_element(flatforward, 3, 0), 50));
  move->velocity[1] =
      qa_q2_n_store(n, qa_q2_n_multiply(n, qa_q2_element(flatforward, 3, 1), 50));
  move->velocity[2] = qa_q2_n_store(n, 350);
  pm->s.pm_flags = qa_q2_flag_set(pm->s.pm_flags, QA_Q2_PMF_TIME_WATERJUMP);
  pm->s.pm_time = 2048;
}

static void pm_fly_move(rerelease_move *move, bool doclip) {
  const qa_numeric_operations *n = move->n;
  qa_q2_movement_math *m = move->m;
  qa_q2_kex_pmove *pm = move->pm;
  qa_q2_vec3 *origin = move->origin;
  pm->s.viewheight = doclip ? 0 : 22;
  const double speed = qa_q2_math_vec3_length(m, move->velocity);
  if (speed < 1) {
    move->velocity[0] = qa_q2_n_store(n, 0);
    move->velocity[1] = qa_q2_n_store(n, 0);
    move->velocity[2] = qa_q2_n_store(n, 0);
  } else {
    const double friction = qa_q2_n_multiply(n, RR_PM_FRICTION, 1.5);
    const double control = speed < RR_PM_STOPSPEED ? RR_PM_STOPSPEED : speed;
    const double drop =
        qa_q2_n_multiply(n, qa_q2_n_multiply(n, control, friction), move->frametime);
    double newspeed = qa_q2_n_subtract(n, speed, drop);
    if (newspeed < 0) newspeed = 0;
    newspeed = qa_q2_n_divide(n, newspeed, speed);
    qa_q2_math_vec3_mul_eqs(m, move->velocity, newspeed);
  }
  const double fmove = equipment_speed(move->movement, pm->cmd.forwardmove);
  const double smove = equipment_speed(move->movement, pm->cmd.sidemove);
  qa_q2_math_vec3_normalize(m, move->forward);
  qa_q2_math_vec3_normalize(m, move->right);
  qa_q2_vec3 wishvel, wishdir;
  qa_q2_math_vec3(
      m,
      qa_q2_n_add(n, qa_q2_n_multiply(n, qa_q2_element(move->forward, 3, 0), fmove),
                  qa_q2_n_multiply(n, qa_q2_element(move->right, 3, 0), smove)),
      qa_q2_n_add(n, qa_q2_n_multiply(n, qa_q2_element(move->forward, 3, 1), fmove),
                  qa_q2_n_multiply(n, qa_q2_element(move->right, 3, 1), smove)),
      qa_q2_n_add(n, qa_q2_n_multiply(n, qa_q2_element(move->forward, 3, 2), fmove),
                  qa_q2_n_multiply(n, qa_q2_element(move->right, 3, 2), smove)),
      wishvel);
  if (qa_int32(pm->cmd.buttons) & QA_Q2_BUTTON_JUMP)
    wishvel[2] = qa_q2_n_store(
        n, qa_q2_n_add(n, qa_q2_element(wishvel, 3, 2),
                       equipment_speed(move->movement,
                                       qa_q2_n_multiply(n, RR_PM_WATERSPEED,
                                                        source_float(move->movement, 0.5)))));
  if (qa_int32(pm->cmd.buttons) & QA_Q2_BUTTON_CROUCH)
    wishvel[2] = qa_q2_n_store(
        n, qa_q2_n_subtract(n, qa_q2_element(wishvel, 3, 2),
                            equipment_speed(move->movement,
                                            qa_q2_n_multiply(n, RR_PM_WATERSPEED,
                                                             source_float(move->movement, 0.5)))));
  qa_q2_math_vec3(m, qa_q2_element(wishvel, 3, 0), qa_q2_element(wishvel, 3, 1),
                  qa_q2_element(wishvel, 3, 2), wishdir);
  double wishspeed = qa_q2_math_vec3_normalize(m, wishdir);
  if (wishspeed > move->movement->pm_maxspeed) {
    qa_q2_math_vec3_mul_eqs(m, wishvel,
                            qa_q2_n_divide(n, move->movement->pm_maxspeed, wishspeed));
    wishspeed = move->movement->pm_maxspeed;
  }
  wishspeed = qa_q2_n_multiply(n, wishspeed, 2);
  const double currentspeed = qa_q2_math_vec3_dot(m, move->velocity, wishdir);
  const double addspeed = qa_q2_n_subtract(n, wishspeed, currentspeed);
  if (addspeed > 0) {
    double accelspeed =
        qa_q2_n_multiply(n, qa_q2_n_multiply(n, RR_PM_ACCELERATE, move->frametime), wishspeed);
    if (accelspeed > addspeed) accelspeed = addspeed;
    move->velocity[0] = qa_q2_n_store(
        n, qa_q2_n_add(n, qa_q2_element(move->velocity, 3, 0),
                       qa_q2_n_multiply(n, accelspeed, qa_q2_element(wishdir, 3, 0))));
    move->velocity[1] = qa_q2_n_store(
        n, qa_q2_n_add(n, qa_q2_element(move->velocity, 3, 1),
                       qa_q2_n_multiply(n, accelspeed, qa_q2_element(wishdir, 3, 1))));
    move->velocity[2] = qa_q2_n_store(
        n, qa_q2_n_add(n, qa_q2_element(move->velocity, 3, 2),
                       qa_q2_n_multiply(n, accelspeed, qa_q2_element(wishdir, 3, 2))));
  }
  if (doclip) {
    pm_step_slide_move(move);
  } else {
    (*origin)[0] = qa_q2_n_store(
        n, qa_q2_n_add(n, qa_q2_element(*origin, 3, 0),
                       qa_q2_n_multiply(n, qa_q2_element(move->velocity, 3, 0), move->frametime)));
    (*origin)[1] = qa_q2_n_store(
        n, qa_q2_n_add(n, qa_q2_element(*origin, 3, 1),
                       qa_q2_n_multiply(n, qa_q2_element(move->velocity, 3, 1), move->frametime)));
    (*origin)[2] = qa_q2_n_store(
        n, qa_q2_n_add(n, qa_q2_element(*origin, 3, 2),
                       qa_q2_n_multiply(n, qa_q2_element(move->velocity, 3, 2), move->frametime)));
  }
}

static bool dimensions_clear(void *data, qa_bounds bounds) {
  rerelease_move *move = data;
  qa_q2_vec3 mins = {bounds.min.x, bounds.min.y, bounds.min.z};
  qa_q2_vec3 maxs = {bounds.max.x, bounds.max.y, bounds.max.z};
  const qa_q2_trace trace = pm_trace(move, *move->origin, mins, maxs, *move->origin);
  move->clear_called = true;
  move->clear_result = !trace.allsolid;
  return move->clear_result;
}

static void pm_set_dimensions(rerelease_move *move) {
  const qa_numeric_operations *n = move->n;
  qa_q2_kex_pmove *pm = move->pm;
  pm->mins[0] = qa_q2_n_store(n, pm->character_bounds.min.x);
  pm->mins[1] = qa_q2_n_store(n, pm->character_bounds.min.y);
  pm->maxs[0] = qa_q2_n_store(n, pm->character_bounds.max.x);
  pm->maxs[1] = qa_q2_n_store(n, pm->character_bounds.max.y);
  if (pm->s.pm_type == QA_Q2_KEX_PM_GIB) {
    pm->mins[2] = qa_q2_n_store(n, qa_q2_character_height(pm->character_bounds, 0, n));
    pm->maxs[2] = qa_q2_n_store(n, qa_q2_character_height(pm->character_bounds, 16, n));
    pm->s.viewheight = qa_q2_character_height(pm->character_bounds, 8, n);
    return;
  }
  pm->mins[2] = qa_q2_n_store(n, qa_q2_character_height(pm->character_bounds, -24, n));
  if (qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_DUCKED) || pm->s.pm_type == QA_Q2_KEX_PM_DEAD) {
    pm->maxs[2] = qa_q2_n_store(n, qa_q2_character_height(pm->character_bounds, 4, n));
    pm->s.viewheight = qa_q2_character_height(pm->character_bounds, -2, n);
  } else {
    pm->maxs[2] = qa_q2_n_store(n, qa_q2_character_height(pm->character_bounds, 32, n));
    pm->s.viewheight = qa_q2_character_height(pm->character_bounds, 22, n);
  }
  qa_bounds requested;
  if (pm->has_body_bounds) {
    requested = pm->body_bounds;
  } else {
    requested.min.x = pm->mins[0];
    requested.min.y = pm->mins[1];
    requested.min.z = pm->mins[2];
    requested.max.x = pm->maxs[0];
    requested.max.y = pm->maxs[1];
    requested.max.z = pm->maxs[2];
  }
  const qa_bounds previous = move->has_accepted_body_bounds ? move->accepted_body_bounds : requested;
  move->clear_called = false;
  move->clear_result = false;
  const qa_bounds bounds = qa_movement_bounds(previous, requested, dimensions_clear, move);
  if (move->clear_called && !move->clear_result) {
    pm->s.pm_flags =
        (double)((qa_int32(pm->s.pm_flags) & ~QA_Q2_PMF_DUCKED) | qa_int32(move->accepted_duck));
    pm->s.viewheight = move->accepted_height;
  }
  move->accepted_duck = (double)(qa_int32(pm->s.pm_flags) & QA_Q2_PMF_DUCKED);
  move->accepted_height = pm->s.viewheight;
  move->has_accepted_body_bounds = true;
  move->accepted_body_bounds = bounds;
  pm->mins[0] = bounds.min.x;
  pm->mins[1] = bounds.min.y;
  pm->mins[2] = bounds.min.z;
  pm->maxs[0] = bounds.max.x;
  pm->maxs[1] = bounds.max.y;
  pm->maxs[2] = bounds.max.z;
}

static bool pm_above_water(rerelease_move *move) {
  const qa_numeric_operations *n = move->n;
  qa_q2_movement_math *m = move->m;
  qa_q2_kex_pmove *pm = move->pm;
  qa_q2_vec3 *origin = move->origin;
  qa_q2_vec3 below;
  qa_q2_math_vec3(m, qa_q2_element(*origin, 3, 0), qa_q2_element(*origin, 3, 1),
                  qa_q2_n_subtract(n, qa_q2_element(*origin, 3, 2), 8), below);
  const bool solid_below = pm->trace(pm->ctx, *origin, pm->mins, pm->maxs, below, pm->has_player,
                                     pm->player, QA_Q2_MASK_SOLID)
                               .fraction < 1;
  if (solid_below) return false;
  const bool water_below = pm->trace(pm->ctx, *origin, pm->mins, pm->maxs, below, pm->has_player,
                                     pm->player, QA_Q2_MASK_WATER)
                               .fraction < 1;
  return water_below;
}

static bool pm_check_duck(rerelease_move *move) {
  qa_q2_movement_math *m = move->m;
  qa_q2_kex_pmove *pm = move->pm;
  qa_q2_vec3 *origin = move->origin;
  if (pm->s.pm_type == QA_Q2_KEX_PM_GIB) return false;
  bool flags_changed = false;
  if (pm->s.pm_type == QA_Q2_KEX_PM_DEAD) {
    if (!qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_DUCKED)) {
      pm->s.pm_flags = qa_q2_flag_set(pm->s.pm_flags, QA_Q2_PMF_DUCKED);
      flags_changed = true;
    }
  } else if ((qa_int32(pm->cmd.buttons) & QA_Q2_BUTTON_CROUCH) &&
             (pm->has_groundentity || (pm->waterlevel <= QA_Q2_WATER_FEET && !pm_above_water(move))) &&
             !qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_ON_LADDER) && !move->config.n64_physics) {
    if (!qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_DUCKED)) {
      qa_q2_vec3 check_maxs, check_mins;
      if (!pm->has_body_bounds) {
        qa_q2_math_vec3(m, qa_q2_element(pm->maxs, 3, 0), qa_q2_element(pm->maxs, 3, 1),
                        qa_q2_character_height(pm->character_bounds, 4, move->n), check_maxs);
        memcpy(check_mins, pm->mins, sizeof check_mins);
      } else {
        qa_q2_math_vec3(m, pm->body_bounds.max.x, pm->body_bounds.max.y, pm->body_bounds.max.z,
                        check_maxs);
        qa_q2_math_vec3(m, pm->body_bounds.min.x, pm->body_bounds.min.y, pm->body_bounds.min.z,
                        check_mins);
      }
      const qa_q2_trace trace = pm_trace(move, *origin, check_mins, check_maxs, *origin);
      if (!trace.allsolid) {
        pm->s.pm_flags = qa_q2_flag_set(pm->s.pm_flags, QA_Q2_PMF_DUCKED);
        flags_changed = true;
      }
    }
  } else {
    if (qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_DUCKED)) {
      qa_q2_vec3 check_maxs, check_mins;
      if (!pm->has_body_bounds) {
        qa_q2_math_vec3(m, qa_q2_element(pm->maxs, 3, 0), qa_q2_element(pm->maxs, 3, 1),
                        pm->character_bounds.max.z, check_maxs);
        memcpy(check_mins, pm->mins, sizeof check_mins);
      } else {
        qa_q2_math_vec3(m, pm->body_bounds.max.x, pm->body_bounds.max.y, pm->body_bounds.max.z,
                        check_maxs);
        qa_q2_math_vec3(m, pm->body_bounds.min.x, pm->body_bounds.min.y, pm->body_bounds.min.z,
                        check_mins);
      }
      const qa_q2_trace trace = pm_trace(move, *origin, check_mins, check_maxs, *origin);
      if (!trace.allsolid) {
        pm->s.pm_flags = qa_q2_flag_clear(pm->s.pm_flags, QA_Q2_PMF_DUCKED);
        flags_changed = true;
      }
    }
  }
  if (!flags_changed) return false;
  pm_set_dimensions(move);
  return true;
}

static void pm_dead_move(rerelease_move *move) {
  const qa_numeric_operations *n = move->n;
  qa_q2_movement_math *m = move->m;
  if (!move->pm->has_groundentity) return;
  double forward = qa_q2_math_vec3_length(m, move->velocity);
  forward = qa_q2_n_subtract(n, forward, 20);
  if (forward <= 0) {
    move->velocity[0] = qa_q2_n_store(n, 0);
    move->velocity[1] = qa_q2_n_store(n, 0);
    move->velocity[2] = qa_q2_n_store(n, 0);
  } else {
    qa_q2_math_vec3_normalize(m, move->velocity);
    qa_q2_math_vec3_mul_eqs(m, move->velocity, forward);
  }
}

static bool pm_good_position(rerelease_move *move) {
  qa_q2_kex_pmove *pm = move->pm;
  if (pm->s.pm_type == QA_Q2_KEX_PM_NOCLIP) return true;
  const qa_q2_trace trace = pm_trace(move, pm->s.origin, pm->mins, pm->maxs, pm->s.origin);
  return !trace.allsolid;
}

static void pm_snap_position(rerelease_move *move) {
  qa_q2_movement_math *m = move->m;
  qa_q2_kex_pmove *pm = move->pm;
  qa_q2_math_vector_copy(m, move->velocity, pm->s.velocity);
  qa_q2_math_vector_copy(m, *move->origin, pm->s.origin);
  if (pm_good_position(move)) return;
  if (qa_q2_rerelease_movement_fix_stuck_object_generic(move->movement, pm->s.origin, pm->mins,
                                                        pm->maxs, trace_auto, move) ==
      QA_Q2_STUCK_NO_GOOD_POSITION) {
    qa_q2_math_vector_copy(m, move->previous_origin, pm->s.origin);
    return;
  }
}

static void pm_initial_snap_position(rerelease_move *move) {
  const qa_numeric_operations *n = move->n;
  qa_q2_movement_math *m = move->m;
  qa_q2_kex_pmove *pm = move->pm;
  static const double offset[3] = {0, -1, 1};
  qa_q2_vec3 base;
  qa_q2_math_vec3(m, qa_q2_element(pm->s.origin, 3, 0), qa_q2_element(pm->s.origin, 3, 1),
                  qa_q2_element(pm->s.origin, 3, 2), base);
  for (int zk = 0; zk < 3; zk++) {
    const int z = qa_q2_axes[zk];
    pm->s.origin[2] =
        qa_q2_n_store(n, qa_q2_n_add(n, qa_q2_element(base, 3, 2), qa_q2_element(offset, 3, z)));
    for (int yk = 0; yk < 3; yk++) {
      const int y = qa_q2_axes[yk];
      pm->s.origin[1] =
          qa_q2_n_store(n, qa_q2_n_add(n, qa_q2_element(base, 3, 1), qa_q2_element(offset, 3, y)));
      for (int xk = 0; xk < 3; xk++) {
        const int x = qa_q2_axes[xk];
        pm->s.origin[0] = qa_q2_n_store(
            n, qa_q2_n_add(n, qa_q2_element(base, 3, 0), qa_q2_element(offset, 3, x)));
        if (pm_good_position(move)) {
          qa_q2_math_vector_copy(m, pm->s.origin, *move->origin);
          qa_q2_math_vector_copy(m, pm->s.origin, move->previous_origin);
          return;
        }
      }
    }
  }
}

static void pm_clamp_angles(rerelease_move *move) {
  qa_q2_kex_pmove *pm = move->pm;
  qa_q2_rerelease_view_angles(pm->viewangles, pm->cmd.angles, pm->s.delta_angles, pm->s.pm_flags,
                              move->n);
  qa_q2_math_angle_vectors(move->m, pm->viewangles, move->forward, move->right, move->up);
}

static void pm_screen_effects(rerelease_move *move) {
  const qa_numeric_operations *n = move->n;
  qa_q2_movement_math *m = move->m;
  qa_q2_kex_pmove *pm = move->pm;
  qa_q2_vec3 *origin = move->origin;
  qa_q2_vec3 vieworg;
  qa_q2_math_vec3(
      m,
      qa_q2_n_add(n, qa_q2_element(*origin, 3, 0), qa_q2_element(pm->viewoffset, 3, 0)),
      qa_q2_n_add(n, qa_q2_element(*origin, 3, 1), qa_q2_element(pm->viewoffset, 3, 1)),
      qa_q2_n_add(n, qa_q2_n_add(n, qa_q2_element(*origin, 3, 2),
                                 qa_q2_element(pm->viewoffset, 3, 2)),
                  pm->s.viewheight),
      vieworg);
  const double contents = pm->pointcontents(pm->ctx, vieworg);
  if (qa_int32(contents) & (QA_Q2_CONTENTS_LAVA | QA_Q2_CONTENTS_SLIME | QA_Q2_CONTENTS_WATER)) {
    pm->rdflags = qa_q2_flag_set(pm->rdflags, QA_Q2_RDF_UNDERWATER);
  } else {
    pm->rdflags = qa_q2_flag_clear(pm->rdflags, QA_Q2_RDF_UNDERWATER);
  }
  if (qa_int32(contents) & (QA_Q2_CONTENTS_SOLID | QA_Q2_CONTENTS_LAVA)) {
    qa_q2_math_g_add_blend(m, 1.0, source_float(move->movement, 0.3),
                           source_float(move->movement, 0.0), source_float(move->movement, 0.6),
                           pm->screen_blend);
  } else if (qa_int32(contents) & QA_Q2_CONTENTS_SLIME) {
    qa_q2_math_g_add_blend(m, source_float(move->movement, 0.0), source_float(move->movement, 0.1),
                           source_float(move->movement, 0.05), source_float(move->movement, 0.6),
                           pm->screen_blend);
  } else if (qa_int32(contents) & QA_Q2_CONTENTS_WATER) {
    qa_q2_math_g_add_blend(m, source_float(move->movement, 0.5), source_float(move->movement, 0.3),
                           source_float(move->movement, 0.2), source_float(move->movement, 0.4),
                           pm->screen_blend);
  }
}

void qa_q2_rerelease_movement_pmove(qa_q2_rerelease_movement *movement, qa_q2_kex_pmove *pmove,
                                    qa_q2_pm_config config) {
  const qa_numeric_operations *n = movement->numeric;
  qa_q2_movement_math *m = &movement->math;
  qa_q2_kex_pmove *pm = pmove;
  rerelease_move move;
  memset(&move, 0, sizeof move);
  move.movement = movement;
  move.n = n;
  move.m = m;
  move.pm = pm;
  move.config = config;

  pm->touch.num = 0;
  pm->viewangles[0] = qa_q2_n_store(n, 0);
  pm->viewangles[1] = qa_q2_n_store(n, 0);
  pm->viewangles[2] = qa_q2_n_store(n, 0);
  pm->s.viewheight = 0;
  pm->has_groundentity = false;
  pm->watertype = QA_Q2_CONTENTS_NONE;
  pm->waterlevel = QA_Q2_WATER_NONE;
  pm->screen_blend[0] = qa_q2_n_store(n, 0);
  pm->screen_blend[1] = qa_q2_n_store(n, 0);
  pm->screen_blend[2] = qa_q2_n_store(n, 0);
  pm->screen_blend[3] = qa_q2_n_store(n, 0);
  pm->rdflags = QA_Q2_RDF_NONE;
  pm->jump_sound = false;
  pm->step_clip = false;
  pm->impact_delta = 0;
  qa_q2_math_vector_copy(m, pm->s.origin, movement->context->pml_origin);
  move.origin = &movement->context->pml_origin;
  qa_q2_math_vec3(m, qa_q2_element(pm->s.velocity, 3, 0), qa_q2_element(pm->s.velocity, 3, 1),
                  qa_q2_element(pm->s.velocity, 3, 2), move.velocity);
  qa_q2_math_vec3(m, 0, 0, 0, move.forward);
  qa_q2_math_vec3(m, 0, 0, 0, move.right);
  qa_q2_math_vec3(m, 0, 0, 0, move.up);
  move.frametime = qa_q2_n_multiply(n, pm->cmd.msec, source_float(movement, 0.001));
  move.has_groundsurface = false;
  move.groundcontents = QA_Q2_CONTENTS_NONE;
  qa_q2_math_vec3(m, qa_q2_element(pm->s.origin, 3, 0), qa_q2_element(pm->s.origin, 3, 1),
                  qa_q2_element(pm->s.origin, 3, 2), move.previous_origin);
  qa_q2_math_vec3(m, qa_q2_element(pm->s.velocity, 3, 0), qa_q2_element(pm->s.velocity, 3, 1),
                  qa_q2_element(pm->s.velocity, 3, 2), move.start_velocity);
  qa_q2_math_vec3(m, qa_q2_element(move.velocity, 3, 0), qa_q2_element(move.velocity, 3, 1),
                  qa_q2_element(move.velocity, 3, 2), move.start_velocity);
  move.has_accepted_body_bounds = pm->has_previous_bounds;
  move.accepted_body_bounds = pm->previous_bounds;
  move.accepted_duck = (double)(qa_int32(pm->s.pm_flags) & QA_Q2_PMF_DUCKED);
  move.accepted_height = pm->s.viewheight;

  pm_clamp_angles(&move);
  if (movement->flight && pm->s.pm_type == QA_Q2_KEX_PM_NORMAL) {
    pm->s.pm_flags = qa_q2_flag_clear(pm->s.pm_flags, QA_Q2_PMF_ON_GROUND | QA_Q2_PMF_DUCKED |
                                                          QA_Q2_PMF_TIME_WATERJUMP);
    pm->s.pm_time = 0;
    pm_fly_move(&move, true);
    pm_snap_position(&move);
    return;
  }
  if (pm->s.pm_type == QA_Q2_KEX_PM_SPECTATOR || pm->s.pm_type == QA_Q2_KEX_PM_NOCLIP) {
    pm->s.pm_flags = QA_Q2_PMF_NONE;
    if (pm->s.pm_type == QA_Q2_KEX_PM_SPECTATOR) {
      pm->mins[0] = qa_q2_n_multiply(n, pm->character_bounds.min.x, 0.5);
      pm->mins[1] = qa_q2_n_multiply(n, pm->character_bounds.min.y, 0.5);
      pm->maxs[0] = qa_q2_n_multiply(n, pm->character_bounds.max.x, 0.5);
      pm->maxs[1] = qa_q2_n_multiply(n, pm->character_bounds.max.y, 0.5);
      pm->mins[2] = qa_q2_n_store(n, qa_q2_character_height(pm->character_bounds, -8, n));
      pm->maxs[2] = qa_q2_n_store(n, qa_q2_character_height(pm->character_bounds, 8, n));
    }
    pm_fly_move(&move, pm->s.pm_type == QA_Q2_KEX_PM_SPECTATOR);
    pm_snap_position(&move);
    return;
  }
  if (pm->s.pm_type >= QA_Q2_KEX_PM_DEAD) {
    pm->cmd.forwardmove = 0;
    pm->cmd.sidemove = 0;
    pm->cmd.buttons =
        (double)(qa_int32(pm->cmd.buttons) & ~(QA_Q2_BUTTON_JUMP | QA_Q2_BUTTON_CROUCH));
  }
  if (pm->s.pm_type == QA_Q2_KEX_PM_FREEZE) return;
  pm_set_dimensions(&move);
  pm_catagorize_position(&move);
  if (pm->snapinitial) pm_initial_snap_position(&move);
  if (pm_check_duck(&move)) pm_catagorize_position(&move);
  if (pm->s.pm_type == QA_Q2_KEX_PM_DEAD) pm_dead_move(&move);
  pm_check_special_movement(&move);
  if (qa_q2_js_truthy(pm->s.pm_time)) {
    if (pm->cmd.msec >= pm->s.pm_time) {
      pm->s.pm_flags = qa_q2_flag_clear(pm->s.pm_flags, QA_Q2_PMF_TIME_WATERJUMP |
                                                            QA_Q2_PMF_TIME_LAND |
                                                            QA_Q2_PMF_TIME_TELEPORT |
                                                            QA_Q2_PMF_TIME_TRICK);
      pm->s.pm_time = 0;
    } else {
      pm->s.pm_time = qa_q2_n_subtract(n, pm->s.pm_time, pm->cmd.msec);
    }
  }
  if (qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_TIME_TELEPORT)) {
  } else if (qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_TIME_WATERJUMP)) {
    move.velocity[2] = qa_q2_n_store(
        n, qa_q2_n_subtract(n, qa_q2_element(move.velocity, 3, 2),
                            qa_q2_n_multiply(n, pm->s.gravity, move.frametime)));
    if (qa_q2_element(move.velocity, 3, 2) < 0) {
      pm->s.pm_flags = qa_q2_flag_clear(pm->s.pm_flags, QA_Q2_PMF_TIME_WATERJUMP |
                                                            QA_Q2_PMF_TIME_LAND |
                                                            QA_Q2_PMF_TIME_TELEPORT |
                                                            QA_Q2_PMF_TIME_TRICK);
      pm->s.pm_time = 0;
    }
    pm_step_slide_move(&move);
  } else {
    pm_check_jump(&move);
    pm_friction(&move);
    if (pm->waterlevel >= QA_Q2_WATER_WAIST) {
      pm_water_move(&move);
    } else {
      qa_q2_vec3 angles;
      qa_q2_math_vec3(m, qa_q2_element(pm->viewangles, 3, 0), qa_q2_element(pm->viewangles, 3, 1),
                      qa_q2_element(pm->viewangles, 3, 2), angles);
      if (qa_q2_element(angles, 3, QA_Q2_PITCH) > 180)
        angles[QA_Q2_PITCH] = qa_q2_n_store(
            n, qa_q2_n_subtract(n, qa_q2_element(angles, 3, QA_Q2_PITCH), 360));
      angles[QA_Q2_PITCH] =
          qa_q2_n_store(n, qa_q2_n_divide(n, qa_q2_element(angles, 3, QA_Q2_PITCH), 3));
      qa_q2_math_angle_vectors(m, angles, move.forward, move.right, move.up);
      pm_air_move(&move);
    }
  }
  pm_catagorize_position(&move);
  if (qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_TIME_TRICK)) pm_check_jump(&move);
  pm_screen_effects(&move);
  pm_snap_position(&move);
}

void qa_q2_pmove_rerelease(qa_q2_kex_pmove *pm, const qa_numeric_operations *numeric_ops,
                           qa_q2_pm_config config, qa_q2_rerelease_movement_context *context,
                           bool flight, double speed_multiplier) {
  qa_q2_rerelease_movement movement;
  qa_q2_create_rerelease_movement(&movement, numeric_ops, context, flight, speed_multiplier);
  qa_q2_rerelease_movement_pmove(&movement, pm, config);
}
