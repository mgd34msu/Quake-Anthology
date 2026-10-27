/*
 * Port of src/input/gamepad.ts: Q1 Ironwail-style radial curve and
 * Q2 gamepad_map axial curve, per-seat stick state, and the shared
 * still-controller gyro calibration policy. The TS gyro yawAxis
 * "y" | "z" is yaw_axis_z here. GPL-2.0-or-later.
 */
#ifndef QA_INPUT_GAMEPAD_H
#define QA_INPUT_GAMEPAD_H

#include <stdbool.h>

#include "qa/contracts/math.h"
#include "qa/contracts/ui.h"

/** TS StickCurve union on kind. */
typedef enum qa_stick_curve_kind {
  QA_STICK_CURVE_RADIAL, QA_STICK_CURVE_AXIAL
} qa_stick_curve_kind;
typedef struct qa_stick_curve {
  qa_stick_curve_kind kind;
  double deadzone;
  double outer_threshold; /* radial only */
  double exponent;
} qa_stick_curve;

/** TS GamepadTuning. */
typedef struct qa_gamepad_tuning {
  qa_stick_curve move;
  qa_stick_curve look;
  bool swap_sticks;
  double yaw_degrees_per_second;
  double pitch_degrees_per_second;
  bool invert_pitch;
  double forward_sensitivity;
  double side_sensitivity;
  double trigger_threshold;
  struct {
    bool enabled;
    double yaw_sensitivity;
    double pitch_sensitivity;
    bool yaw_axis_z; /* TS yawAxis "z"; false is "y" */
  } gyro;
} qa_gamepad_tuning;

extern const qa_gamepad_tuning qa_default_gamepad_tuning;

/* Aborts with the TS RangeError messages on invalid tuning. */
void qa_validate_gamepad_tuning(const qa_gamepad_tuning *tuning);
qa_vec2 qa_apply_stick_curve(qa_vec2 axis, qa_stick_curve curve);
/* TS null becomes a NULL result. */
qa_controller_axis qa_controller_axis_name(double axis);
double qa_normalized_controller_axis(qa_controller_axis axis, double raw);

/** TS GamepadSample. */
typedef struct qa_gamepad_sample {
  qa_vec2 move;
  qa_vec2 look_degrees;
} qa_gamepad_sample;

/** TS GamepadInput.preview result. */
typedef struct qa_gamepad_preview {
  qa_vec2 move_raw;
  qa_vec2 move_curved;
  qa_vec2 look_raw;
  qa_vec2 look_curved;
} qa_gamepad_preview;

/** TS GyroCalibrationState union on kind. */
typedef enum qa_gyro_calibration_kind {
  QA_GYRO_CALIBRATION_IDLE, QA_GYRO_CALIBRATION_CALIBRATING, QA_GYRO_CALIBRATION_READY
} qa_gyro_calibration_kind;
typedef struct qa_gyro_calibration_state {
  qa_gyro_calibration_kind kind;
  double progress; /* calibrating */
  double samples;  /* calibrating */
  qa_vec3 bias;    /* ready */
} qa_gyro_calibration_state;

typedef struct qa_gamepad_input {
  qa_gamepad_tuning tuning;
  double axes[6];
  bool axis_set[6];
  double preview_axes[6];
  bool preview_set[6];
  bool has_gyro_sample;
  qa_vec3 gyro_sample;
  bool has_gyro_bias;
  qa_vec3 gyro_bias;
  bool calibrating;
  bool has_capture;
  double capture_start;
  double capture_last;
  double capture_samples;
  qa_vec3 capture_mean;
  qa_vec3 capture_deviation;
} qa_gamepad_input;

/* tuning NULL selects the TS default. Validates (aborts) as the
 * TS constructor does. */
void qa_gamepad_input_init(qa_gamepad_input *input, const qa_gamepad_tuning *tuning);
void qa_gamepad_input_preview_axis(qa_gamepad_input *input, qa_controller_axis axis,
                                   double value);
qa_gamepad_preview qa_gamepad_input_preview(const qa_gamepad_input *input);
void qa_gamepad_input_axis(qa_gamepad_input *input, qa_controller_axis axis,
                           double value);
qa_gyro_calibration_state qa_gamepad_input_gyro_calibration(
    const qa_gamepad_input *input);
void qa_gamepad_input_begin_gyro_calibration(qa_gamepad_input *input);
void qa_gamepad_input_cancel_gyro_calibration(qa_gamepad_input *input);
void qa_gamepad_input_reset_gyro_calibration(qa_gamepad_input *input);
/* TS aiming defaults to true. */
void qa_gamepad_input_gyro(qa_gamepad_input *input, qa_vec3 value,
                           double time_milliseconds, bool aiming);
qa_gamepad_sample qa_gamepad_input_sample(qa_gamepad_input *input,
                                          double frame_milliseconds);
void qa_gamepad_input_clear(qa_gamepad_input *input);

#endif
