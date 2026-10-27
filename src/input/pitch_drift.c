/* Port of src/input/pitch-drift.ts. GPL-2.0-or-later. */
#include "qa/input/pitch_drift.h"

#include <math.h>

void qa_pitch_drift_init(qa_pitch_drift *drift) {
  drift->stopped = true;
  drift->velocity = 0;
  drift->moving_seconds = 0;
}

void qa_pitch_drift_clear(qa_pitch_drift *drift) { qa_pitch_drift_init(drift); }

double qa_pitch_drift_sample(qa_pitch_drift *drift, double pitch,
                             double elapsed_milliseconds,
                             qa_pitch_drift_state state, bool manual,
                             bool start, double forward,
                             double forward_threshold, double speed,
                             double delay) {
  if (manual) qa_pitch_drift_clear(drift);
  else if (start && (drift->stopped || drift->velocity == 0)) {
    drift->stopped = false;
    drift->velocity = speed;
    drift->moving_seconds = 0;
  }
  if (state.disabled || !state.grounded) {
    drift->moving_seconds = 0;
    drift->velocity = 0;
    return pitch;
  }
  double seconds = elapsed_milliseconds / 1000;
  if (drift->stopped) {
    drift->moving_seconds = manual || fabs(forward) < forward_threshold
                                ? 0
                                : drift->moving_seconds + seconds;
    if (drift->moving_seconds > delay) {
      drift->stopped = false;
      drift->velocity = speed;
      drift->moving_seconds = 0;
    }
    return pitch;
  }
  double delta = state.ideal_pitch - pitch;
  if (delta == 0) {
    drift->velocity = 0;
    return pitch;
  }
  double move = fmin(fabs(delta), seconds * drift->velocity);
  drift->velocity += seconds * speed;
  if (move == fabs(delta)) drift->velocity = 0;
  return pitch + (delta > 0 ? 1 : delta < 0 ? -1 : 0) * move;
}
