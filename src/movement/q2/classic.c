/* Port of src/movement/q2/classic.ts. GPL-2.0-or-later. */
#include "qa/movement/q2/classic.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "qa/core/numeric.h"
#include "qa/movement/body_shape.h"
#include "qa/movement/q2/dimensions.h"
#include "qa/movement/q2/math.h"
#include "qa/movement/q2/swept.h"
#include "qa/movement/q2/view.h"

/* TS class PmlT plus the pmoveClassic closure state. */
typedef struct classic_move {
  qa_q2_classic_pmove *pm;
  const qa_numeric_operations *n;
  qa_q2_movement_math math;
  qa_q2_vec3 origin;
  qa_q2_vec3 velocity;
  qa_q2_vec3 forward;
  qa_q2_vec3 right;
  qa_q2_vec3 up;
  double frametime;
  bool has_groundsurface;
  qa_q2_csurface groundsurface;
  qa_q2_cplane groundplane;
  double groundcontents;
  qa_q2_vec3 previous_origin;
  bool ladder;
  double pm_stopspeed;
  double pm_maxspeed;
  double pm_duckspeed;
  double pm_accelerate;
  double pm_airaccelerate;
  double pm_wateraccelerate;
  double pm_friction;
  double pm_waterfriction;
  double pm_waterspeed;
  bool strafejump_hack;
  bool flight;
  double speed_multiplier;
  /* movementBounds clear-callback capture (bounds identity). */
  bool clear_called;
  bool clear_result;
} classic_move;

static double equipment_speed(const classic_move *move, double value) {
  return move->speed_multiplier == 1
             ? value
             : qa_q2_n_multiply(move->n, value, move->speed_multiplier);
}

static void copy_plane(classic_move *move, qa_q2_cplane *dst, const qa_q2_cplane *src) {
  qa_q2_math_vector_copy(&move->math, src->normal, dst->normal);
  dst->dist = src->dist;
  dst->type = src->type;
  dst->signbits = src->signbits;
}

/* TS (x << 16) >> 16. */
static double to_short(double x) {
  return (double)(int16_t)(uint16_t)(uint32_t)qa_int32(x);
}

/* JS Int16Array element store. */
static int16_t to_int16(double x) { return (int16_t)(uint16_t)(uint32_t)qa_int32(x); }

static qa_q2_trace pm_trace(classic_move *move, qa_q2_vec3 start, qa_q2_vec3 mins,
                            qa_q2_vec3 maxs, qa_q2_vec3 end) {
  return move->pm->trace(move->pm->ctx, start, mins, maxs, end);
}

static void record_touch(classic_move *move, const qa_q2_trace *trace) {
  qa_q2_classic_pmove *pm = move->pm;
  if (pm->numtouch < QA_Q2_MAXTOUCH && trace->has_ent) {
    const size_t index = (size_t)pm->numtouch;
    pm->touchents[index] = trace->ent;
    pm->touchtraces[index] = *trace;
    pm->numtouch++;
  }
}

#define STOP_EPSILON 0.1

static void pm_clip_velocity(classic_move *move, const qa_q2_vec3 in_vec,
                             const qa_q2_vec3 normal, qa_q2_vec3 out, double overbounce) {
  const qa_numeric_operations *n = move->n;
  if (n->profile.arithmetic.kind != QA_ARITHMETIC_DONOR_BINARY64)
    overbounce = qa_q2_n_store(n, overbounce);
  const double backoff =
      qa_q2_n_multiply(n, qa_q2_math_dot_product(&move->math, in_vec, normal), overbounce);
  for (int k = 0; k < 3; k++) {
    const int i = qa_q2_axes[k];
    const double change = qa_q2_n_multiply(n, qa_q2_element(normal, 3, i), backoff);
    out[i] = qa_q2_n_store(n, qa_q2_n_subtract(n, qa_q2_element(in_vec, 3, i), change));
    if (qa_q2_element(out, 3, i) > -STOP_EPSILON && qa_q2_element(out, 3, i) < STOP_EPSILON)
      out[i] = qa_q2_n_store(n, 0);
  }
}

static qa_q2_trace sweep_trace(void *data, qa_q2_vec3 start, qa_q2_vec3 end) {
  classic_move *move = data;
  return pm_trace(move, start, move->pm->mins, move->pm->maxs, end);
}

static void sweep_clip(void *data, qa_q2_vec3 velocity, qa_q2_vec3 normal, qa_q2_vec3 out) {
  classic_move *move = data;
  qa_q2_math_vec3(&move->math, 0, 0, 0, out);
  pm_clip_velocity(move, velocity, normal, out, 1.01);
}

static void sweep_touch(void *data, qa_q2_trace *trace) {
  classic_move *move = data;
  record_touch(move, trace);
}

static void pm_step_slide_move_(classic_move *move) {
  qa_q2_vec3 primal_velocity;
  qa_q2_math_vec3(&move->math, 0, 0, 0, primal_velocity);
  qa_q2_math_vector_copy(&move->math, move->velocity, primal_velocity);
  qa_q2_body_sweep options;
  memset(&options, 0, sizeof options);
  options.ctx = move;
  options.origin = &move->origin;
  options.velocity = &move->velocity;
  options.elapsed = move->frametime;
  options.numeric = move->n;
  options.trace = sweep_trace;
  options.clip = sweep_clip;
  options.touch = sweep_touch;
  const qa_sweep_stop stop = qa_sweep_q2_body(&options);
  if (stop != QA_SWEEP_SOLID && qa_q2_js_truthy(move->pm->s.pm_time))
    qa_q2_math_vector_copy(&move->math, primal_velocity, move->velocity);
}

#define MIN_STEP_NORMAL 0.7
#define CLASSIC_STEPSIZE 18

static void pm_step_slide_move(classic_move *move) {
  const qa_numeric_operations *n = move->n;
  qa_q2_movement_math *m = &move->math;
  qa_q2_classic_pmove *pm = move->pm;
  qa_q2_vec3 start_o, start_v, down_o, down_v, up, down;
  qa_q2_math_vec3(m, 0, 0, 0, start_o);
  qa_q2_math_vec3(m, 0, 0, 0, start_v);
  qa_q2_math_vec3(m, 0, 0, 0, down_o);
  qa_q2_math_vec3(m, 0, 0, 0, down_v);
  qa_q2_math_vec3(m, 0, 0, 0, up);
  qa_q2_math_vec3(m, 0, 0, 0, down);
  qa_q2_math_vector_copy(m, move->origin, start_o);
  qa_q2_math_vector_copy(m, move->velocity, start_v);
  pm_step_slide_move_(move);
  qa_q2_math_vector_copy(m, move->origin, down_o);
  qa_q2_math_vector_copy(m, move->velocity, down_v);
  qa_q2_math_vector_copy(m, start_o, up);
  up[2] = qa_q2_n_store(n, qa_q2_n_add(n, qa_q2_element(up, 3, 2), CLASSIC_STEPSIZE));
  qa_q2_trace trace = pm_trace(move, up, pm->mins, pm->maxs, up);
  if (trace.allsolid) return;
  qa_q2_math_vector_copy(m, up, move->origin);
  qa_q2_math_vector_copy(m, start_v, move->velocity);
  pm_step_slide_move_(move);
  qa_q2_math_vector_copy(m, move->origin, down);
  down[2] = qa_q2_n_store(n, qa_q2_n_subtract(n, qa_q2_element(down, 3, 2), CLASSIC_STEPSIZE));
  trace = pm_trace(move, move->origin, pm->mins, pm->maxs, down);
  if (!trace.allsolid) qa_q2_math_vector_copy(m, trace.endpos, move->origin);
  qa_q2_math_vector_copy(m, move->origin, up);
  const double down_dist = qa_q2_n_add(
      n,
      qa_q2_n_multiply(n, qa_q2_n_subtract(n, qa_q2_element(down_o, 3, 0), qa_q2_element(start_o, 3, 0)),
                       qa_q2_n_subtract(n, qa_q2_element(down_o, 3, 0), qa_q2_element(start_o, 3, 0))),
      qa_q2_n_multiply(n, qa_q2_n_subtract(n, qa_q2_element(down_o, 3, 1), qa_q2_element(start_o, 3, 1)),
                       qa_q2_n_subtract(n, qa_q2_element(down_o, 3, 1), qa_q2_element(start_o, 3, 1))));
  const double up_dist = qa_q2_n_add(
      n,
      qa_q2_n_multiply(n, qa_q2_n_subtract(n, qa_q2_element(up, 3, 0), qa_q2_element(start_o, 3, 0)),
                       qa_q2_n_subtract(n, qa_q2_element(up, 3, 0), qa_q2_element(start_o, 3, 0))),
      qa_q2_n_multiply(n, qa_q2_n_subtract(n, qa_q2_element(up, 3, 1), qa_q2_element(start_o, 3, 1)),
                       qa_q2_n_subtract(n, qa_q2_element(up, 3, 1), qa_q2_element(start_o, 3, 1))));
  if (down_dist > up_dist || qa_q2_element(trace.plane.normal, 3, 2) < MIN_STEP_NORMAL) {
    qa_q2_math_vector_copy(m, down_o, move->origin);
    qa_q2_math_vector_copy(m, down_v, move->velocity);
    return;
  }
  move->velocity[2] = qa_q2_n_store(n, qa_q2_element(down_v, 3, 2));
}

static void pm_friction(classic_move *move) {
  const qa_numeric_operations *n = move->n;
  qa_q2_classic_pmove *pm = move->pm;
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
       (qa_int32(move->groundsurface.flags) & QA_Q2_SURF_SLICK) == 0) ||
      move->ladder) {
    const double friction = move->pm_friction;
    const double control = speed < move->pm_stopspeed ? move->pm_stopspeed : speed;
    drop = qa_q2_n_add(n, drop,
                       qa_q2_n_multiply(n, qa_q2_n_multiply(n, control, friction), move->frametime));
  }
  if (qa_q2_js_truthy(pm->waterlevel) && !move->ladder) {
    drop = qa_q2_n_add(
        n, drop,
        qa_q2_n_multiply(n, qa_q2_n_multiply(n, qa_q2_n_multiply(n, speed, move->pm_waterfriction),
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

static void pm_accelerate(classic_move *move, const qa_q2_vec3 wishdir, double wishspeed,
                          double accel) {
  const qa_numeric_operations *n = move->n;
  const double currentspeed = qa_q2_math_dot_product(&move->math, move->velocity, wishdir);
  const double addspeed = qa_q2_n_subtract(n, wishspeed, currentspeed);
  if (addspeed <= 0) return;
  double accelspeed = qa_q2_n_multiply(n, qa_q2_n_multiply(n, accel, move->frametime), wishspeed);
  if (accelspeed > addspeed) accelspeed = addspeed;
  for (int k = 0; k < 3; k++) {
    const int i = qa_q2_axes[k];
    move->velocity[i] = qa_q2_n_store(
        n, qa_q2_n_add(n, qa_q2_element(move->velocity, 3, i),
                       qa_q2_n_multiply(n, accelspeed, qa_q2_element(wishdir, 3, i))));
  }
}

static void pm_air_accelerate(classic_move *move, const qa_q2_vec3 wishdir, double wishspeed,
                              double accel) {
  const qa_numeric_operations *n = move->n;
  double wishspd = wishspeed;
  if (wishspd > 30) wishspd = 30;
  const double currentspeed = qa_q2_math_dot_product(&move->math, move->velocity, wishdir);
  const double addspeed = qa_q2_n_subtract(n, wishspd, currentspeed);
  if (addspeed <= 0) return;
  double accelspeed = qa_q2_n_multiply(n, qa_q2_n_multiply(n, accel, wishspeed), move->frametime);
  if (accelspeed > addspeed) accelspeed = addspeed;
  for (int k = 0; k < 3; k++) {
    const int i = qa_q2_axes[k];
    move->velocity[i] = qa_q2_n_store(
        n, qa_q2_n_add(n, qa_q2_element(move->velocity, 3, i),
                       qa_q2_n_multiply(n, accelspeed, qa_q2_element(wishdir, 3, i))));
  }
}

static void pm_add_currents(classic_move *move, qa_q2_vec3 wishvel) {
  const qa_numeric_operations *n = move->n;
  qa_q2_movement_math *m = &move->math;
  qa_q2_classic_pmove *pm = move->pm;
  qa_q2_vec3 v;
  qa_q2_math_vec3(m, 0, 0, 0, v);
  if (move->ladder && fabs(qa_q2_element(move->velocity, 3, 2)) <= 200) {
    if (qa_q2_element(pm->viewangles, 3, QA_Q2_PITCH) <= -15 && pm->cmd.forwardmove > 0)
      wishvel[2] = qa_q2_n_store(n, 200);
    else if (qa_q2_element(pm->viewangles, 3, QA_Q2_PITCH) >= 15 && pm->cmd.forwardmove > 0)
      wishvel[2] = qa_q2_n_store(n, -200);
    else if (pm->cmd.upmove > 0)
      wishvel[2] = qa_q2_n_store(n, 200);
    else if (pm->cmd.upmove < 0)
      wishvel[2] = qa_q2_n_store(n, -200);
    else
      wishvel[2] = qa_q2_n_store(n, 0);
    if (qa_q2_element(wishvel, 3, 0) < -25)
      wishvel[0] = qa_q2_n_store(n, -25);
    else if (qa_q2_element(wishvel, 3, 0) > 25)
      wishvel[0] = qa_q2_n_store(n, 25);
    if (qa_q2_element(wishvel, 3, 1) < -25)
      wishvel[1] = qa_q2_n_store(n, -25);
    else if (qa_q2_element(wishvel, 3, 1) > 25)
      wishvel[1] = qa_q2_n_store(n, 25);
  }
  if (qa_int32(pm->watertype) & QA_Q2_MASK_CURRENT) {
    qa_q2_math_vector_clear(m, v);
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
    double s = move->pm_waterspeed;
    if (pm->waterlevel == 1 && pm->has_groundentity) s = qa_q2_n_divide(n, s, 2);
    qa_q2_math_vector_ma(m, wishvel, s, v, wishvel);
  }
  if (pm->has_groundentity) {
    qa_q2_math_vector_clear(m, v);
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
    qa_q2_math_vector_ma(m, wishvel, 100, v, wishvel);
  }
}

static void pm_water_move(classic_move *move) {
  const qa_numeric_operations *n = move->n;
  qa_q2_movement_math *m = &move->math;
  qa_q2_classic_pmove *pm = move->pm;
  qa_q2_vec3 wishvel, wishdir;
  qa_q2_math_vec3(m, 0, 0, 0, wishvel);
  qa_q2_math_vec3(m, 0, 0, 0, wishdir);
  for (int k = 0; k < 3; k++) {
    const int i = qa_q2_axes[k];
    wishvel[i] = qa_q2_n_store(
        n, qa_q2_n_add(n, qa_q2_n_multiply(n, qa_q2_element(move->forward, 3, i),
                                           equipment_speed(move, pm->cmd.forwardmove)),
                       qa_q2_n_multiply(n, qa_q2_element(move->right, 3, i),
                                        equipment_speed(move, pm->cmd.sidemove))));
  }
  if (!qa_q2_js_truthy(pm->cmd.forwardmove) && !qa_q2_js_truthy(pm->cmd.sidemove) &&
      !qa_q2_js_truthy(pm->cmd.upmove))
    wishvel[2] = qa_q2_n_store(n, qa_q2_n_subtract(n, qa_q2_element(wishvel, 3, 2), 60));
  else
    wishvel[2] = qa_q2_n_store(
        n, qa_q2_n_add(n, qa_q2_element(wishvel, 3, 2), equipment_speed(move, pm->cmd.upmove)));
  pm_add_currents(move, wishvel);
  qa_q2_math_vector_copy(m, wishvel, wishdir);
  double wishspeed = qa_q2_math_vector_normalize(m, wishdir);
  if (wishspeed > move->pm_maxspeed) {
    qa_q2_math_vector_scale(m, wishvel, qa_q2_n_divide(n, move->pm_maxspeed, wishspeed), wishvel);
    wishspeed = move->pm_maxspeed;
  }
  wishspeed = qa_q2_n_multiply(n, wishspeed, 0.5);
  pm_accelerate(move, wishdir, wishspeed, move->pm_wateraccelerate);
  pm_step_slide_move(move);
}

static void pm_air_move(classic_move *move) {
  const qa_numeric_operations *n = move->n;
  qa_q2_movement_math *m = &move->math;
  qa_q2_classic_pmove *pm = move->pm;
  qa_q2_vec3 wishvel, wishdir;
  qa_q2_math_vec3(m, 0, 0, 0, wishvel);
  qa_q2_math_vec3(m, 0, 0, 0, wishdir);
  const double fmove = equipment_speed(move, pm->cmd.forwardmove);
  const double smove = equipment_speed(move, pm->cmd.sidemove);
  for (int i = 0; i < 2; i++)
    wishvel[i] = qa_q2_n_store(
        n, qa_q2_n_add(n, qa_q2_n_multiply(n, qa_q2_element(move->forward, 3, i), fmove),
                       qa_q2_n_multiply(n, qa_q2_element(move->right, 3, i), smove)));
  wishvel[2] = qa_q2_n_store(n, 0);
  pm_add_currents(move, wishvel);
  qa_q2_math_vector_copy(m, wishvel, wishdir);
  double wishspeed = qa_q2_math_vector_normalize(m, wishdir);
  const double maxspeed =
      qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_DUCKED) ? move->pm_duckspeed : move->pm_maxspeed;
  if (wishspeed > maxspeed) {
    qa_q2_math_vector_scale(m, wishvel, qa_q2_n_divide(n, maxspeed, wishspeed), wishvel);
    wishspeed = maxspeed;
  }
  if (move->ladder) {
    pm_accelerate(move, wishdir, wishspeed, move->pm_accelerate);
    if (qa_q2_element(wishvel, 3, 2) == 0) {
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
    pm_accelerate(move, wishdir, wishspeed, move->pm_accelerate);
    if (pm->s.gravity > 0)
      move->velocity[2] = qa_q2_n_store(n, 0);
    else
      move->velocity[2] = qa_q2_n_store(
          n, qa_q2_n_subtract(n, qa_q2_element(move->velocity, 3, 2),
                              qa_q2_n_multiply(n, pm->s.gravity, move->frametime)));
    if (qa_q2_element(move->velocity, 3, 0) == 0 && qa_q2_element(move->velocity, 3, 1) == 0)
      return;
    pm_step_slide_move(move);
  } else {
    if (qa_q2_js_truthy(move->pm_airaccelerate))
      pm_air_accelerate(move, wishdir, wishspeed, move->pm_accelerate);
    else
      pm_accelerate(move, wishdir, wishspeed, 1);
    move->velocity[2] = qa_q2_n_store(
        n, qa_q2_n_subtract(n, qa_q2_element(move->velocity, 3, 2),
                            qa_q2_n_multiply(n, pm->s.gravity, move->frametime)));
    pm_step_slide_move(move);
  }
}

static void pm_catagorize_position(classic_move *move) {
  const qa_numeric_operations *n = move->n;
  qa_q2_movement_math *m = &move->math;
  qa_q2_classic_pmove *pm = move->pm;
  qa_q2_vec3 point;
  qa_q2_math_vec3(m, 0, 0, 0, point);
  point[0] = qa_q2_n_store(n, qa_q2_element(move->origin, 3, 0));
  point[1] = qa_q2_n_store(n, qa_q2_element(move->origin, 3, 1));
  point[2] = qa_q2_n_store(n, qa_q2_n_subtract(n, qa_q2_element(move->origin, 3, 2), 0.25));
  if (qa_q2_element(move->velocity, 3, 2) > 180) {
    pm->s.pm_flags = qa_q2_flag_clear(pm->s.pm_flags, QA_Q2_PMF_ON_GROUND);
    pm->has_groundentity = false;
  } else {
    const qa_q2_trace trace = pm_trace(move, move->origin, pm->mins, pm->maxs, point);
    copy_plane(move, &move->groundplane, &trace.plane);
    move->has_groundsurface = trace.has_surface;
    move->groundsurface = trace.surface;
    move->groundcontents = trace.contents;
    if (!trace.has_ent || (qa_q2_element(trace.plane.normal, 3, 2) < 0.7 && !trace.startsolid)) {
      pm->has_groundentity = false;
      pm->s.pm_flags = qa_q2_flag_clear(pm->s.pm_flags, QA_Q2_PMF_ON_GROUND);
    } else {
      pm->has_groundentity = true;
      pm->groundentity = trace.ent;
      if (qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_TIME_WATERJUMP)) {
        pm->s.pm_flags = qa_q2_flag_clear(pm->s.pm_flags, QA_Q2_PMF_TIME_WATERJUMP |
                                                              QA_Q2_PMF_TIME_LAND |
                                                              QA_Q2_PMF_TIME_TELEPORT);
        pm->s.pm_time = 0;
      }
      if (!qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_ON_GROUND)) {
        pm->s.pm_flags = qa_q2_flag_set(pm->s.pm_flags, QA_Q2_PMF_ON_GROUND);
        if (qa_q2_element(move->velocity, 3, 2) < -200 && !move->strafejump_hack) {
          pm->s.pm_flags = qa_q2_flag_set(pm->s.pm_flags, QA_Q2_PMF_TIME_LAND);
          if (qa_q2_element(move->velocity, 3, 2) < -400)
            pm->s.pm_time = 25;
          else
            pm->s.pm_time = 18;
        }
      }
    }
    record_touch(move, &trace);
  }
  pm->waterlevel = 0;
  pm->watertype = 0;
  const double sample2 = trunc(qa_q2_n_subtract(n, pm->viewheight, qa_q2_element(pm->mins, 3, 2)));
  const double sample1 = (double)qa_int32(qa_q2_n_divide(n, sample2, 2));
  point[2] = qa_q2_n_store(
      n, qa_q2_n_add(n, qa_q2_n_add(n, qa_q2_element(move->origin, 3, 2), qa_q2_element(pm->mins, 3, 2)), 1));
  double cont = pm->pointcontents(pm->ctx, point);
  if (qa_int32(cont) & QA_Q2_MASK_WATER) {
    pm->watertype = cont;
    pm->waterlevel = 1;
    point[2] = qa_q2_n_store(n, qa_q2_n_add(n, qa_q2_n_add(n, qa_q2_element(move->origin, 3, 2),
                                                           qa_q2_element(pm->mins, 3, 2)),
                                            sample1));
    cont = pm->pointcontents(pm->ctx, point);
    if (qa_int32(cont) & QA_Q2_MASK_WATER) {
      pm->waterlevel = 2;
      point[2] = qa_q2_n_store(n, qa_q2_n_add(n, qa_q2_n_add(n, qa_q2_element(move->origin, 3, 2),
                                                             qa_q2_element(pm->mins, 3, 2)),
                                              sample2));
      cont = pm->pointcontents(pm->ctx, point);
      if (qa_int32(cont) & QA_Q2_MASK_WATER) pm->waterlevel = 3;
    }
  }
}

static void pm_check_jump(classic_move *move) {
  const qa_numeric_operations *n = move->n;
  qa_q2_classic_pmove *pm = move->pm;
  if (qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_TIME_LAND)) return;
  if (pm->cmd.upmove < 10) {
    pm->s.pm_flags = qa_q2_flag_clear(pm->s.pm_flags, QA_Q2_PMF_JUMP_HELD);
    return;
  }
  if (qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_JUMP_HELD)) return;
  if (pm->s.pm_type == QA_Q2_PM_DEAD) return;
  if (pm->waterlevel >= 2) {
    pm->has_groundentity = false;
    if (qa_q2_element(move->velocity, 3, 2) <= -300) return;
    if (pm->watertype == QA_Q2_CONTENTS_WATER)
      move->velocity[2] = qa_q2_n_store(n, 100);
    else if (pm->watertype == QA_Q2_CONTENTS_SLIME)
      move->velocity[2] = qa_q2_n_store(n, 80);
    else
      move->velocity[2] = qa_q2_n_store(n, 50);
    return;
  }
  if (!pm->has_groundentity) return;
  pm->s.pm_flags = qa_q2_flag_set(pm->s.pm_flags, QA_Q2_PMF_JUMP_HELD);
  pm->has_groundentity = false;
  move->velocity[2] =
      qa_q2_n_store(n, qa_q2_n_add(n, qa_q2_element(move->velocity, 3, 2), 270));
  if (qa_q2_element(move->velocity, 3, 2) < 270) move->velocity[2] = qa_q2_n_store(n, 270);
}

static void pm_check_special_movement(classic_move *move) {
  const qa_numeric_operations *n = move->n;
  qa_q2_movement_math *m = &move->math;
  qa_q2_classic_pmove *pm = move->pm;
  qa_q2_vec3 spot, flatforward;
  qa_q2_math_vec3(m, 0, 0, 0, spot);
  qa_q2_math_vec3(m, 0, 0, 0, flatforward);
  if (qa_q2_js_truthy(pm->s.pm_time)) return;
  move->ladder = false;
  flatforward[0] = qa_q2_n_store(n, qa_q2_element(move->forward, 3, 0));
  flatforward[1] = qa_q2_n_store(n, qa_q2_element(move->forward, 3, 1));
  flatforward[2] = qa_q2_n_store(n, 0);
  qa_q2_math_vector_normalize(m, flatforward);
  qa_q2_math_vector_ma(m, move->origin, 1, flatforward, spot);
  const qa_q2_trace trace = pm_trace(move, move->origin, pm->mins, pm->maxs, spot);
  if (trace.fraction < 1 && (qa_int32(trace.contents) & QA_Q2_CONTENTS_LADDER))
    move->ladder = true;
  if (pm->waterlevel != 2) return;
  qa_q2_math_vector_ma(m, move->origin, 30, flatforward, spot);
  spot[2] = qa_q2_n_store(n, qa_q2_n_add(n, qa_q2_element(spot, 3, 2), 4));
  double cont = pm->pointcontents(pm->ctx, spot);
  if (!(qa_int32(cont) & QA_Q2_CONTENTS_SOLID)) return;
  spot[2] = qa_q2_n_store(n, qa_q2_n_add(n, qa_q2_element(spot, 3, 2), 16));
  cont = pm->pointcontents(pm->ctx, spot);
  if (qa_q2_js_truthy(cont)) return;
  qa_q2_math_vector_scale(m, flatforward, 50, move->velocity);
  move->velocity[2] = qa_q2_n_store(n, 350);
  pm->s.pm_flags = qa_q2_flag_set(pm->s.pm_flags, QA_Q2_PMF_TIME_WATERJUMP);
  pm->s.pm_time = 255;
}

static void pm_fly_move(classic_move *move, bool doclip) {
  const qa_numeric_operations *n = move->n;
  qa_q2_movement_math *m = &move->math;
  qa_q2_classic_pmove *pm = move->pm;
  pm->viewheight = qa_q2_character_height(pm->character_bounds, 22, n);
  const double speed = qa_q2_math_vector_length(m, move->velocity);
  if (speed < 1) {
    qa_q2_math_vector_clear(m, move->velocity);
  } else {
    const double friction = qa_q2_n_multiply(n, move->pm_friction, 1.5);
    const double control = speed < move->pm_stopspeed ? move->pm_stopspeed : speed;
    const double drop =
        qa_q2_n_multiply(n, qa_q2_n_multiply(n, control, friction), move->frametime);
    double newspeed = qa_q2_n_subtract(n, speed, drop);
    if (newspeed < 0) newspeed = 0;
    newspeed = qa_q2_n_divide(n, newspeed, speed);
    qa_q2_math_vector_scale(m, move->velocity, newspeed, move->velocity);
  }
  const double fmove = equipment_speed(move, pm->cmd.forwardmove);
  const double smove = equipment_speed(move, pm->cmd.sidemove);
  qa_q2_math_vector_normalize(m, move->forward);
  qa_q2_math_vector_normalize(m, move->right);
  qa_q2_vec3 wishvel, wishdir;
  qa_q2_math_vec3(m, 0, 0, 0, wishvel);
  qa_q2_math_vec3(m, 0, 0, 0, wishdir);
  for (int k = 0; k < 3; k++) {
    const int i = qa_q2_axes[k];
    wishvel[i] = qa_q2_n_store(
        n, qa_q2_n_add(n, qa_q2_n_multiply(n, qa_q2_element(move->forward, 3, i), fmove),
                       qa_q2_n_multiply(n, qa_q2_element(move->right, 3, i), smove)));
  }
  wishvel[2] = qa_q2_n_store(
      n, qa_q2_n_add(n, qa_q2_element(wishvel, 3, 2), equipment_speed(move, pm->cmd.upmove)));
  qa_q2_math_vector_copy(m, wishvel, wishdir);
  double wishspeed = qa_q2_math_vector_normalize(m, wishdir);
  if (wishspeed > move->pm_maxspeed) {
    qa_q2_math_vector_scale(m, wishvel, qa_q2_n_divide(n, move->pm_maxspeed, wishspeed), wishvel);
    wishspeed = move->pm_maxspeed;
  }
  const double currentspeed = qa_q2_math_dot_product(m, move->velocity, wishdir);
  const double addspeed = qa_q2_n_subtract(n, wishspeed, currentspeed);
  if (addspeed <= 0 && !doclip) return;
  double accelspeed =
      qa_q2_n_multiply(n, qa_q2_n_multiply(n, move->pm_accelerate, move->frametime), wishspeed);
  accelspeed = qa_q2_js_max(0, qa_q2_js_min(accelspeed, addspeed));
  for (int k = 0; k < 3; k++) {
    const int i = qa_q2_axes[k];
    move->velocity[i] = qa_q2_n_store(
        n, qa_q2_n_add(n, qa_q2_element(move->velocity, 3, i),
                       qa_q2_n_multiply(n, accelspeed, qa_q2_element(wishdir, 3, i))));
  }
  if (doclip) {
    pm_step_slide_move(move);
  } else {
    qa_q2_math_vector_ma(m, move->origin, move->frametime, move->velocity, move->origin);
  }
}

static bool duck_bounds_clear(void *data, qa_bounds bounds) {
  classic_move *move = data;
  qa_q2_vec3 mins = {bounds.min.x, bounds.min.y, bounds.min.z};
  qa_q2_vec3 maxs = {bounds.max.x, bounds.max.y, bounds.max.z};
  const qa_q2_trace trace = pm_trace(move, move->origin, mins, maxs, move->origin);
  move->clear_called = true;
  move->clear_result = !trace.allsolid;
  return move->clear_result;
}

static void pm_check_duck(classic_move *move) {
  const qa_numeric_operations *n = move->n;
  qa_q2_classic_pmove *pm = move->pm;
  const double previous_duck = (double)(qa_int32(pm->s.pm_flags) & QA_Q2_PMF_DUCKED);
  pm->mins[0] = qa_q2_n_store(n, pm->character_bounds.min.x);
  pm->mins[1] = qa_q2_n_store(n, pm->character_bounds.min.y);
  pm->maxs[0] = qa_q2_n_store(n, pm->character_bounds.max.x);
  pm->maxs[1] = qa_q2_n_store(n, pm->character_bounds.max.y);
  if (pm->s.pm_type == QA_Q2_PM_GIB) {
    pm->mins[2] = qa_q2_n_store(n, qa_q2_character_height(pm->character_bounds, 0, n));
    pm->maxs[2] = qa_q2_n_store(n, qa_q2_character_height(pm->character_bounds, 16, n));
    pm->viewheight = qa_q2_character_height(pm->character_bounds, 8, n);
    return;
  }
  pm->mins[2] = qa_q2_n_store(n, qa_q2_character_height(pm->character_bounds, -24, n));
  if (pm->s.pm_type == QA_Q2_PM_DEAD) {
    pm->s.pm_flags = qa_q2_flag_set(pm->s.pm_flags, QA_Q2_PMF_DUCKED);
  } else if (pm->cmd.upmove < 0 && qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_ON_GROUND)) {
    pm->s.pm_flags = qa_q2_flag_set(pm->s.pm_flags, QA_Q2_PMF_DUCKED);
  } else {
    if (qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_DUCKED)) {
      pm->maxs[2] = qa_q2_n_store(n, qa_q2_character_height(pm->character_bounds, 32, n));
      qa_q2_vec3 trace_mins, trace_maxs;
      if (!pm->has_body_bounds) {
        memcpy(trace_mins, pm->mins, sizeof trace_mins);
        memcpy(trace_maxs, pm->maxs, sizeof trace_maxs);
      } else {
        trace_mins[0] = pm->body_bounds.min.x;
        trace_mins[1] = pm->body_bounds.min.y;
        trace_mins[2] = pm->body_bounds.min.z;
        trace_maxs[0] = pm->body_bounds.max.x;
        trace_maxs[1] = pm->body_bounds.max.y;
        trace_maxs[2] = pm->body_bounds.max.z;
      }
      const qa_q2_trace trace = pm_trace(move, move->origin, trace_mins, trace_maxs, move->origin);
      if (!trace.allsolid)
        pm->s.pm_flags = qa_q2_flag_clear(pm->s.pm_flags, QA_Q2_PMF_DUCKED);
    }
  }
  if (qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_DUCKED)) {
    pm->maxs[2] = qa_q2_n_store(n, qa_q2_character_height(pm->character_bounds, 4, n));
    pm->viewheight = qa_q2_character_height(pm->character_bounds, -2, n);
  } else {
    pm->maxs[2] = qa_q2_n_store(n, qa_q2_character_height(pm->character_bounds, 32, n));
    pm->viewheight = qa_q2_character_height(pm->character_bounds, 22, n);
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
  const qa_bounds previous = pm->has_previous_bounds ? pm->previous_bounds : requested;
  move->clear_called = false;
  move->clear_result = false;
  const qa_bounds bounds = qa_movement_bounds(previous, requested, duck_bounds_clear, move);
  /* TS bounds !== requested: the clear callback ran and rejected. */
  if (move->clear_called && !move->clear_result) {
    pm->s.pm_flags =
        (double)((qa_int32(pm->s.pm_flags) & ~QA_Q2_PMF_DUCKED) | qa_int32(previous_duck));
    pm->viewheight =
        qa_q2_character_height(pm->character_bounds, qa_q2_js_truthy(previous_duck) ? -2 : 22, n);
  }
  pm->mins[0] = bounds.min.x;
  pm->mins[1] = bounds.min.y;
  pm->mins[2] = bounds.min.z;
  pm->maxs[0] = bounds.max.x;
  pm->maxs[1] = bounds.max.y;
  pm->maxs[2] = bounds.max.z;
}

static void pm_dead_move(classic_move *move) {
  const qa_numeric_operations *n = move->n;
  qa_q2_movement_math *m = &move->math;
  if (!move->pm->has_groundentity) return;
  double forward = qa_q2_math_vector_length(m, move->velocity);
  forward = qa_q2_n_subtract(n, forward, 20);
  if (forward <= 0) {
    qa_q2_math_vector_clear(m, move->velocity);
  } else {
    qa_q2_math_vector_normalize(m, move->velocity);
    qa_q2_math_vector_scale(m, move->velocity, forward, move->velocity);
  }
}

static bool pm_good_position(classic_move *move) {
  const qa_numeric_operations *n = move->n;
  qa_q2_movement_math *m = &move->math;
  qa_q2_classic_pmove *pm = move->pm;
  if (pm->s.pm_type == QA_Q2_PM_SPECTATOR) return true;
  qa_q2_vec3 origin, end;
  qa_q2_math_vec3(m, 0, 0, 0, origin);
  qa_q2_math_vec3(m, 0, 0, 0, end);
  for (int k = 0; k < 3; k++) {
    const int i = qa_q2_axes[k];
    origin[i] = qa_q2_n_store(
        n, end[i] = qa_q2_n_store(n, qa_q2_n_multiply(n, qa_q2_element(pm->s.origin, 3, i), 0.125)));
  }
  const qa_q2_trace trace = pm_trace(move, origin, pm->mins, pm->maxs, end);
  return !trace.allsolid;
}

static const double jitterbits[8] = {0, 4, 1, 2, 3, 5, 6, 7};

static void pm_snap_position(classic_move *move) {
  const qa_numeric_operations *n = move->n;
  qa_q2_classic_pmove *pm = move->pm;
  double sign[3] = {0, 0, 0};
  int16_t base[3] = {0, 0, 0};
  for (int k = 0; k < 3; k++) {
    const int i = qa_q2_axes[k];
    pm->s.velocity[i] = to_short(qa_q2_n_multiply(n, qa_q2_element(move->velocity, 3, i), 8));
  }
  for (int k = 0; k < 3; k++) {
    const int i = qa_q2_axes[k];
    sign[i] = qa_q2_n_store(n, qa_q2_element(move->origin, 3, i) >= 0 ? 1 : -1);
    pm->s.origin[i] = to_short(qa_q2_n_multiply(n, qa_q2_element(move->origin, 3, i), 8));
    if (qa_q2_n_multiply(n, qa_q2_element(pm->s.origin, 3, i), 0.125) ==
        qa_q2_element(move->origin, 3, i))
      sign[i] = qa_q2_n_store(n, 0);
  }
  for (int k = 0; k < 3; k++) {
    const int i = qa_q2_axes[k];
    base[i] = to_int16(qa_q2_n_store(n, qa_q2_element(pm->s.origin, 3, i)));
  }
  for (int j = 0; j < 8; j++) {
    const double bits = qa_q2_element(jitterbits, 8, j);
    for (int k = 0; k < 3; k++) {
      const int i = qa_q2_axes[k];
      pm->s.origin[i] = to_short((double)base[i]);
    }
    for (int k = 0; k < 3; k++) {
      const int i = qa_q2_axes[k];
      if (qa_int32(bits) & (1 << i))
        pm->s.origin[i] =
            to_short(qa_q2_n_add(n, qa_q2_element(pm->s.origin, 3, i), sign[i]));
    }
    if (pm_good_position(move)) return;
  }
  for (int k = 0; k < 3; k++) {
    const int i = qa_q2_axes[k];
    pm->s.origin[i] = to_short(qa_q2_element(move->previous_origin, 3, i));
  }
}

static const double snap_offset[3] = {0, -1, 1};

static void pm_initial_snap_position(classic_move *move) {
  const qa_numeric_operations *n = move->n;
  qa_q2_classic_pmove *pm = move->pm;
  int16_t base[3] = {0, 0, 0};
  for (int k = 0; k < 3; k++) {
    const int i = qa_q2_axes[k];
    base[i] = to_int16(qa_q2_n_store(n, qa_q2_element(pm->s.origin, 3, i)));
  }
  for (int zk = 0; zk < 3; zk++) {
    const int z = qa_q2_axes[zk];
    pm->s.origin[2] =
        to_short(qa_q2_n_add(n, (double)base[2], qa_q2_element(snap_offset, 3, z)));
    for (int yk = 0; yk < 3; yk++) {
      const int y = qa_q2_axes[yk];
      pm->s.origin[1] =
          to_short(qa_q2_n_add(n, (double)base[1], qa_q2_element(snap_offset, 3, y)));
      for (int xk = 0; xk < 3; xk++) {
        const int x = qa_q2_axes[xk];
        pm->s.origin[0] =
            to_short(qa_q2_n_add(n, (double)base[0], qa_q2_element(snap_offset, 3, x)));
        if (pm_good_position(move)) {
          move->origin[0] =
              qa_q2_n_store(n, qa_q2_n_multiply(n, qa_q2_element(pm->s.origin, 3, 0), 0.125));
          move->origin[1] =
              qa_q2_n_store(n, qa_q2_n_multiply(n, qa_q2_element(pm->s.origin, 3, 1), 0.125));
          move->origin[2] =
              qa_q2_n_store(n, qa_q2_n_multiply(n, qa_q2_element(pm->s.origin, 3, 2), 0.125));
          for (int k = 0; k < 3; k++) {
            const int i = qa_q2_axes[k];
            move->previous_origin[i] = qa_q2_n_store(n, qa_q2_element(pm->s.origin, 3, i));
          }
          return;
        }
      }
    }
  }
}

static void pm_clamp_angles(classic_move *move) {
  qa_q2_classic_pmove *pm = move->pm;
  qa_q2_classic_view_angles(pm->viewangles, pm->cmd.angles, pm->s.delta_angles, pm->s.pm_flags,
                            move->n);
  qa_q2_math_angle_vectors(&move->math, pm->viewangles, move->forward, move->right, move->up);
}

static void reset_pml(classic_move *move) {
  qa_q2_movement_math *m = &move->math;
  qa_q2_math_vec3(m, 0, 0, 0, move->origin);
  qa_q2_math_vec3(m, 0, 0, 0, move->velocity);
  qa_q2_math_vec3(m, 0, 0, 0, move->forward);
  qa_q2_math_vec3(m, 0, 0, 0, move->right);
  qa_q2_math_vec3(m, 0, 0, 0, move->up);
  move->frametime = 0;
  move->has_groundsurface = false;
  move->groundplane = qa_q2_plane();
  move->groundcontents = 0;
  qa_q2_math_vec3(m, 0, 0, 0, move->previous_origin);
  move->ladder = false;
}

static void run(classic_move *move) {
  const qa_numeric_operations *n = move->n;
  qa_q2_movement_math *m = &move->math;
  qa_q2_classic_pmove *pm = move->pm;
  pm->numtouch = 0;
  qa_q2_math_vector_clear(m, pm->viewangles);
  pm->viewheight = 0;
  pm->has_groundentity = false;
  pm->watertype = 0;
  pm->waterlevel = 0;
  reset_pml(move);
  move->origin[0] =
      qa_q2_n_store(n, qa_q2_n_multiply(n, qa_q2_element(pm->s.origin, 3, 0), 0.125));
  move->origin[1] =
      qa_q2_n_store(n, qa_q2_n_multiply(n, qa_q2_element(pm->s.origin, 3, 1), 0.125));
  move->origin[2] =
      qa_q2_n_store(n, qa_q2_n_multiply(n, qa_q2_element(pm->s.origin, 3, 2), 0.125));
  move->velocity[0] =
      qa_q2_n_store(n, qa_q2_n_multiply(n, qa_q2_element(pm->s.velocity, 3, 0), 0.125));
  move->velocity[1] =
      qa_q2_n_store(n, qa_q2_n_multiply(n, qa_q2_element(pm->s.velocity, 3, 1), 0.125));
  move->velocity[2] =
      qa_q2_n_store(n, qa_q2_n_multiply(n, qa_q2_element(pm->s.velocity, 3, 2), 0.125));
  for (int k = 0; k < 3; k++) {
    const int i = qa_q2_axes[k];
    move->previous_origin[i] = qa_q2_n_store(n, qa_q2_element(pm->s.origin, 3, i));
  }
  move->frametime = qa_q2_n_multiply(n, pm->cmd.msec, 0.001);
  pm_clamp_angles(move);
  if (move->flight && pm->s.pm_type == QA_Q2_PM_NORMAL) {
    pm->s.pm_flags = qa_q2_flag_clear(pm->s.pm_flags, QA_Q2_PMF_ON_GROUND | QA_Q2_PMF_DUCKED |
                                                          QA_Q2_PMF_TIME_WATERJUMP);
    pm->s.pm_time = 0;
    pm_fly_move(move, true);
    pm_snap_position(move);
    return;
  }
  if (pm->s.pm_type == QA_Q2_PM_SPECTATOR) {
    pm_fly_move(move, false);
    pm_snap_position(move);
    return;
  }
  if (pm->s.pm_type >= QA_Q2_PM_DEAD) {
    pm->cmd.forwardmove = 0;
    pm->cmd.sidemove = 0;
    pm->cmd.upmove = 0;
  }
  if (pm->s.pm_type == QA_Q2_PM_FREEZE) return;
  pm_check_duck(move);
  if (pm->snapinitial) pm_initial_snap_position(move);
  pm_catagorize_position(move);
  if (pm->s.pm_type == QA_Q2_PM_DEAD) pm_dead_move(move);
  pm_check_special_movement(move);
  if (qa_q2_js_truthy(pm->s.pm_time)) {
    double msec = (double)(qa_int32(pm->cmd.msec) >> 3);
    if (!qa_q2_js_truthy(msec)) msec = 1;
    if (msec >= pm->s.pm_time) {
      pm->s.pm_flags = qa_q2_flag_clear(pm->s.pm_flags, QA_Q2_PMF_TIME_WATERJUMP |
                                                            QA_Q2_PMF_TIME_LAND |
                                                            QA_Q2_PMF_TIME_TELEPORT);
      pm->s.pm_time = 0;
    } else {
      pm->s.pm_time = qa_q2_n_subtract(n, pm->s.pm_time, msec);
    }
  }
  if (qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_TIME_TELEPORT)) {
  } else if (qa_q2_flag_test(pm->s.pm_flags, QA_Q2_PMF_TIME_WATERJUMP)) {
    move->velocity[2] = qa_q2_n_store(
        n, qa_q2_n_subtract(n, qa_q2_element(move->velocity, 3, 2),
                            qa_q2_n_multiply(n, pm->s.gravity, move->frametime)));
    if (qa_q2_element(move->velocity, 3, 2) < 0) {
      pm->s.pm_flags = qa_q2_flag_clear(pm->s.pm_flags, QA_Q2_PMF_TIME_WATERJUMP |
                                                            QA_Q2_PMF_TIME_LAND |
                                                            QA_Q2_PMF_TIME_TELEPORT);
      pm->s.pm_time = 0;
    }
    pm_step_slide_move(move);
  } else {
    pm_check_jump(move);
    pm_friction(move);
    if (pm->waterlevel >= 2) {
      pm_water_move(move);
    } else {
      qa_q2_vec3 angles;
      qa_q2_math_vec3(m, 0, 0, 0, angles);
      qa_q2_math_vector_copy(m, pm->viewangles, angles);
      if (qa_q2_element(angles, 3, QA_Q2_PITCH) > 180)
        angles[QA_Q2_PITCH] = qa_q2_n_store(
            n, qa_q2_n_subtract(n, qa_q2_element(angles, 3, QA_Q2_PITCH), 360));
      angles[QA_Q2_PITCH] =
          qa_q2_n_store(n, qa_q2_n_divide(n, qa_q2_element(angles, 3, QA_Q2_PITCH), 3));
      qa_q2_math_angle_vectors(m, angles, move->forward, move->right, move->up);
      pm_air_move(move);
    }
  }
  pm_catagorize_position(move);
  pm_snap_position(move);
}

void qa_q2_pmove_classic(qa_q2_classic_pmove *pm, const qa_numeric_operations *numeric_ops,
                         double air_accelerate, bool strafejump_hack, bool flight,
                         double speed_multiplier) {
  classic_move move;
  memset(&move, 0, sizeof move);
  move.pm = pm;
  move.n = numeric_ops;
  qa_q2_movement_math_init(&move.math, numeric_ops, QA_Q2_MATH_CLASSIC);
  move.strafejump_hack = strafejump_hack;
  move.flight = flight;
  move.speed_multiplier = speed_multiplier;
  reset_pml(&move);
  move.pm_stopspeed = 100;
  move.pm_maxspeed = equipment_speed(&move, 300);
  move.pm_duckspeed = equipment_speed(&move, 100);
  move.pm_accelerate = 10;
  move.pm_airaccelerate = air_accelerate;
  move.pm_wateraccelerate = 10;
  move.pm_friction = 6;
  move.pm_waterfriction = 1;
  move.pm_waterspeed = 400;
  run(&move);
}
