/*
 * Port of src/movement/q2/swept.ts: converts source tuple storage
 * and preserves native trace identity; the shared swept-body solver
 * owns iteration. GPL-2.0-or-later.
 * C accommodations: origin/velocity are pointers to the caller's
 * live tuple arrays (the TS readonly references the solver writes
 * through); optional members use has_* flags / NULL callbacks; the
 * native trace handed to touch/solid/prepare_trace is borrowed and
 * valid until the sweep's next trace call, as in the TS.
 */
#ifndef QA_MOVEMENT_Q2_SWEPT_H
#define QA_MOVEMENT_Q2_SWEPT_H

#include <stdbool.h>

#include "qa/contracts/numeric.h"
#include "qa/movement/q2/types.h"
#include "qa/movement/swept_body.h"

/** TS Q2BodySweep. prepare_trace/solid may be NULL. */
typedef struct qa_q2_body_sweep {
  void *ctx;
  qa_q2_vec3 *origin;
  qa_q2_vec3 *velocity;
  double elapsed;
  const qa_numeric_operations *numeric;
  qa_q2_trace (*trace)(void *ctx, qa_q2_vec3 start, qa_q2_vec3 end);
  void (*clip)(void *ctx, qa_q2_vec3 velocity, qa_q2_vec3 normal, qa_q2_vec3 out);
  void (*touch)(void *ctx, qa_q2_trace *trace);
  void (*prepare_trace)(void *ctx, qa_q2_trace *trace);
  void (*solid)(void *ctx, qa_q2_trace *trace);
  bool has_duplicate_plane;
  double duplicate_plane_threshold;
  void (*duplicate_plane_recover)(void *ctx, qa_q2_vec3 normal);
} qa_q2_body_sweep;

/** TS sweepQ2Body. */
qa_sweep_stop qa_sweep_q2_body(const qa_q2_body_sweep *options);

#endif
