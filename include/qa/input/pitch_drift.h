/*
 * Port of src/input/pitch-drift.ts: Q1 V_StartPitchDrift /
 * V_DriftPitch. The source supplies grounded state and ideal
 * pitch. GPL-2.0-or-later.
 */
#ifndef QA_INPUT_PITCH_DRIFT_H
#define QA_INPUT_PITCH_DRIFT_H

#include <stdbool.h>

/** TS PitchDriftState. */
typedef struct qa_pitch_drift_state {
  bool grounded;
  double ideal_pitch;
  bool disabled;
} qa_pitch_drift_state;

typedef struct qa_pitch_drift {
  bool stopped;
  double velocity;
  double moving_seconds;
} qa_pitch_drift;

void qa_pitch_drift_init(qa_pitch_drift *drift);
void qa_pitch_drift_clear(qa_pitch_drift *drift);
/* TS speed/delay default to 500 and .15. */
double qa_pitch_drift_sample(qa_pitch_drift *drift, double pitch,
                             double elapsed_milliseconds,
                             qa_pitch_drift_state state, bool manual,
                             bool start, double forward,
                             double forward_threshold, double speed,
                             double delay);

#endif
