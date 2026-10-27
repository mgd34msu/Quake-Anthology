/*
 * Port of src/input/mouse.ts: Q3 CL_MouseMove and Q1/Q2
 * IN_MouseMove filtering/scaling. The TS optional lookSpring /
 * lookStrafe fields are plain bools here (absent reads false, and
 * the sampler only tests `=== true`). The tuning store is a
 * function pointer plus ctx. GPL-2.0-or-later.
 */
#ifndef QA_INPUT_MOUSE_H
#define QA_INPUT_MOUSE_H

#include <stdbool.h>

#include "qa/contracts/math.h"

/** TS MouseTuning. */
typedef struct qa_mouse_tuning {
  double sensitivity;
  double acceleration;
  bool filter;
  double yaw;
  double pitch;
  double side;
  double forward;
  bool free_look;
  bool look_spring;
  bool look_strafe;
  bool invert_pitch;
} qa_mouse_tuning;

extern const qa_mouse_tuning qa_default_mouse_tuning;

/** TS MouseMove. */
typedef struct qa_mouse_move {
  double yaw;
  double pitch;
  double side;
  double forward;
} qa_mouse_move;

/** TS MouseTuningStore. */
typedef struct qa_mouse_tuning_store {
  void *ctx;
  qa_mouse_tuning (*read)(void *ctx);
  void (*write)(void *ctx, qa_mouse_tuning value);
} qa_mouse_tuning_store;

typedef struct qa_mouse_input {
  qa_vec2 previous;
  bool use_store;
  qa_mouse_tuning tuning_value;
  qa_mouse_tuning_store store;
} qa_mouse_input;

/* TS constructor default is defaultMouseTuning. */
void qa_mouse_input_init(qa_mouse_input *input, qa_mouse_tuning tuning);
void qa_mouse_input_init_store(qa_mouse_input *input, qa_mouse_tuning_store store);
void qa_mouse_input_bind_settings(qa_mouse_input *input, qa_mouse_tuning_store store);
qa_mouse_tuning qa_mouse_input_tuning(const qa_mouse_input *input);
void qa_mouse_input_set_tuning(qa_mouse_input *input, qa_mouse_tuning value);
/* TS zoomSensitivity defaults to 1, binary32 to false. */
qa_mouse_move qa_mouse_input_sample(qa_mouse_input *input, qa_vec2 raw,
                                    double frame_milliseconds, bool strafe,
                                    bool mouse_look, double zoom_sensitivity,
                                    bool binary32);
void qa_mouse_input_clear(qa_mouse_input *input);

#endif
