/*
 * Port of src/movement/q2/rerelease.ts: Quake II rerelease pmove.
 * Copyright (c) ZeniMax Media Inc. GPL-2.0-or-later.
 * C accommodations: the TS createRereleaseMovement factory becomes
 * qa_q2_rerelease_movement initialized in place (it borrows the
 * caller's qa_q2_rerelease_movement_context, whose pml_origin keeps
 * the source pml alias). TS default arguments are explicit.
 */
#ifndef QA_MOVEMENT_Q2_RERELEASE_H
#define QA_MOVEMENT_Q2_RERELEASE_H

#include <stdbool.h>

#include "qa/contracts/math.h"
#include "qa/contracts/numeric.h"
#include "qa/movement/q2/math.h"
#include "qa/movement/q2/types.h"

/** TS Q2RereleaseMovementContext. The game DLL shares pml between
 * all Pmove and SV_FlyMove calls; prediction owns a separate one. */
typedef struct qa_q2_rerelease_movement_context {
  qa_q2_vec3 pml_origin;
} qa_q2_rerelease_movement_context;

void qa_q2_rerelease_movement_context_init(qa_q2_rerelease_movement_context *context);
/** TS capture(). */
qa_vec3 qa_q2_rerelease_movement_context_capture(
    const qa_q2_rerelease_movement_context *context);
/** TS restore() (returns undefined in TS). */
void qa_q2_rerelease_movement_context_restore(qa_q2_rerelease_movement_context *context,
                                              qa_vec3 origin);

/** TS createRereleaseMovement result. */
typedef struct qa_q2_rerelease_movement {
  const qa_numeric_operations *numeric;
  qa_q2_rerelease_movement_context *context;
  bool flight;
  double speed_multiplier;
  qa_q2_movement_math math;
  double pm_maxspeed;
  double pm_duckspeed;
} qa_q2_rerelease_movement;

/** TS createRereleaseMovement(numericOps, context, flight, speedMultiplier). */
void qa_q2_create_rerelease_movement(qa_q2_rerelease_movement *movement,
                                     const qa_numeric_operations *numeric_ops,
                                     qa_q2_rerelease_movement_context *context, bool flight,
                                     double speed_multiplier);

/** TS Pmove(pmove, config). */
void qa_q2_rerelease_movement_pmove(qa_q2_rerelease_movement *movement, qa_q2_kex_pmove *pmove,
                                    qa_q2_pm_config config);

/** TS PM_StepSlideMove_Generic. origin/velocity point at live tuples. */
void qa_q2_rerelease_movement_step_slide_move_generic(
    qa_q2_rerelease_movement *movement, qa_q2_vec3 *origin, qa_q2_vec3 *velocity,
    double frametime, qa_q2_vec3 mins, qa_q2_vec3 maxs, qa_q2_kex_touch_list *touch,
    bool has_time, qa_q2_pm_trace_fn trace_func, void *trace_ctx);

/** TS G_FixStuckObject_Generic; returns a QA_Q2_STUCK_* value. */
double qa_q2_rerelease_movement_fix_stuck_object_generic(qa_q2_rerelease_movement *movement,
                                                         qa_q2_vec3 origin, qa_q2_vec3 own_mins,
                                                         qa_q2_vec3 own_maxs,
                                                         qa_q2_stuck_object_trace_fn trace,
                                                         void *trace_ctx);

/** TS pmoveRerelease. */
void qa_q2_pmove_rerelease(qa_q2_kex_pmove *pm, const qa_numeric_operations *numeric_ops,
                           qa_q2_pm_config config, qa_q2_rerelease_movement_context *context,
                           bool flight, double speed_multiplier);

#endif
