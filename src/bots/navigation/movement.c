/* Port of src/bots/navigation/movement.ts. GPL-2.0-or-later. */
#include "qa/bots/navigation/movement.h"
#include "qa/bots/navigation/helpers.h"
#include "qa/contracts/protocol.h"
#include "qa/contracts/time.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static qa_movement_result step(qa_movement_input input, qa_navigation_prediction *prediction) {
  if (!input.execution || strcmp(input.execution, "prediction"))
    qa_range_error("Navigation must use noncommitting movement prediction");
  if (!input.kind || !prediction->provider.kind || strcmp(prediction->provider.kind, input.kind))
    qa_range_error("Navigation movement input/provider disagree");
  return prediction->provider.move(prediction->provider.ctx, input, prediction->services);
}
static qa_vec3 result_origin(const qa_movement_result *r) {
  switch (r->state.kind) {
    case QA_MOVEMENT_STATE_Q2: {
      const double *e = r->state.as.q2.origin_eighths.values;
      return (qa_vec3){e[0] / 8, e[1] / 8, e[2] / 8};
    }
    case QA_MOVEMENT_STATE_Q1: return r->state.as.q1.origin;
    case QA_MOVEMENT_STATE_QW: return r->state.as.qw.origin;
    case QA_MOVEMENT_STATE_Q2_RERELEASE: return r->state.as.q2_rerelease.origin;
    case QA_MOVEMENT_STATE_Q3: return r->state.as.q3.origin;
  }
  return (qa_vec3){0, 0, 0};
}
void qa_movement_route_admission_init(qa_movement_route_admission *a, qa_navigation_prediction_driver driver,
                                      qa_navigation_profile profile, qa_navigation_prediction_limits options) {
  memset(a, 0, sizeof *a);
  a->driver = driver; a->profile = profile;
  a->max_seconds = options.has_max_seconds ? options.max_seconds : 8;
  a->max_commands = options.has_max_commands ? options.max_commands : 512;
  a->tolerance = options.has_tolerance ? options.tolerance : 8;
  if (!isfinite(a->max_seconds) || a->max_seconds <= 0 || a->max_commands <= 0
      || !isfinite(a->tolerance) || a->tolerance <= 0)
    qa_range_error("Invalid navigation prediction limits");
}
qa_traversal_admission qa_movement_route_admission_admit(qa_movement_route_admission *a, qa_traversal_request request) {
  static char reason_buf[256]; /* TS allocates per call; admission reason borrowed until next admit. */
  qa_traversal_admission adm; memset(&adm, 0, sizeof adm);
  if (a->failed) qa_range_error("Discard a failed navigation prediction before trying another route");
  if (!a->has_prediction) {
    if (!a->driver.begin(a->driver.ctx, request, a->profile, &a->prediction)) {
      snprintf(reason_buf, sizeof reason_buf, "selected movement cannot predict %s", qa_travel_mode_name(request.mode));
      adm.reason = reason_buf; return adm;
    }
    a->has_prediction = true;
  }
  qa_navigation_prediction *prediction = &a->prediction;
  const char *mk = a->profile.movement.kind, *pk = prediction->provider.kind;
  const char *mid = a->profile.movement.id, *pid = prediction->provider.id;
  if (!mk || !pk || strcmp(pk, mk) || (mid == NULL) != (pid == NULL) || (mid && pid && strcmp(mid, pid)))
    qa_range_error("Navigation predictor is not the selected movement provider");
  double seconds = 0;
  size_t traj_cap = 16, traj_n = 0;
  qa_vec3 *trajectory = malloc(traj_cap * sizeof *trajectory);
  if (!trajectory) { adm.reason = "out of memory"; a->failed = true; return adm; }
  trajectory[traj_n++] = request.from;
  for (int64_t command = 0; command < a->max_commands && seconds < a->max_seconds; command++) {
    qa_movement_input input = prediction->input(prediction->ctx, a->has_previous ? &a->previous : NULL,
                                                a->sequence++, request);
    if (!input.profile.kind || !mk || strcmp(input.profile.kind, mk)
        || (input.profile.id == NULL) != (mid == NULL)
        || (input.profile.id && mid && strcmp(input.profile.id, mid)))
      qa_range_error("Navigation prediction changed the selected movement profile");
    double elapsed = input.frame.elapsed.kind == QA_SOURCE_TIME_MILLISECONDS
                         ? input.frame.elapsed.value / 1000 : input.frame.elapsed.value;
    if (!isfinite(elapsed) || elapsed <= 0) qa_range_error("Navigation prediction must advance source time");
    qa_movement_result result = step(input, prediction);
    if (result.status && !strcmp(result.status, "actor-removed")) {
      a->failed = true; free(trajectory);
      adm.reason = "movement removed the predicted actor"; return adm;
    }
    seconds += elapsed;
    qa_vec3 origin = result_origin(&result);
    if (traj_n == traj_cap) {
      qa_vec3 *grown = realloc(trajectory, traj_cap * 2 * sizeof *grown);
      if (!grown) { free(trajectory); a->failed = true; adm.reason = "out of memory"; return adm; }
      trajectory = grown; traj_cap *= 2;
    }
    trajectory[traj_n++] = origin;
    bool grounded = result.ground.kind != QA_TRACE_HIT_NONE || result.water_level > 0 || request.mode == QA_TM_LADDER;
    a->previous = result; a->has_previous = true;
    if (qa_nav_distance(origin, request.to) <= a->tolerance && grounded) {
      adm.admitted = true; adm.seconds = seconds;
      adm.trajectory = trajectory; adm.trajectory_count = traj_n; return adm;
    }
  }
  a->failed = true; free(trajectory);
  snprintf(reason_buf, sizeof reason_buf, "selected movement did not reach the landing within %gs/%lld commands",
           a->max_seconds, (long long)a->max_commands);
  adm.reason = reason_buf; return adm;
}
static qa_traversal_admission admit_thunk(void *ctx, qa_traversal_request req) {
  return qa_movement_route_admission_admit(ctx, req);
}
qa_navigation_route_prediction qa_movement_route_admission_as_prediction(qa_movement_route_admission *a) {
  return (qa_navigation_route_prediction){a, admit_thunk};
}
qa_traversal_admission qa_create_movement_admission(qa_navigation_prediction_driver driver,
                                                    qa_navigation_prediction_limits options,
                                                    qa_traversal_request request, qa_navigation_profile profile) {
  qa_movement_route_admission a;
  qa_movement_route_admission_init(&a, driver, profile, options);
  return qa_movement_route_admission_admit(&a, request);
}
