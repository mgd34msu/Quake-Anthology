/* Port of src/input/gamepad.ts. GPL-2.0-or-later. */
#include "qa/input/gamepad.h"

#include <math.h>
#include <string.h>

#include "qa/foundation/support.h"

const qa_gamepad_tuning qa_default_gamepad_tuning = {
    {QA_STICK_CURVE_RADIAL, 0.175, 0.02, 2},
    {QA_STICK_CURVE_RADIAL, 0.175, 0.02, 2},
    false, 240, 130, false, 1, 1, 0.2,
    {false, 1, 1, false}};

void qa_validate_gamepad_tuning(const qa_gamepad_tuning *tuning) {
  const qa_stick_curve curves[2] = {tuning->move, tuning->look};
  for (int i = 0; i < 2; i++) {
    const qa_stick_curve *curve = &curves[i];
    if (!isfinite(curve->deadzone) || curve->deadzone < 0 || curve->deadzone >= 1 ||
        !isfinite(curve->exponent) || curve->exponent <= 0 ||
        (curve->kind == QA_STICK_CURVE_RADIAL &&
         (!isfinite(curve->outer_threshold) || curve->outer_threshold < 0 ||
          curve->deadzone + curve->outer_threshold >= 1))) {
      qa_range_error("Invalid gamepad deadzone or response curve");
    }
  }
  const double sensitivities[6] = {tuning->yaw_degrees_per_second,
                                   tuning->pitch_degrees_per_second,
                                   tuning->forward_sensitivity,
                                   tuning->side_sensitivity,
                                   tuning->gyro.yaw_sensitivity,
                                   tuning->gyro.pitch_sensitivity};
  for (int i = 0; i < 6; i++) {
    if (!isfinite(sensitivities[i])) qa_range_error("Invalid gamepad sensitivity");
  }
  if (!isfinite(tuning->trigger_threshold) || tuning->trigger_threshold < 0 ||
      tuning->trigger_threshold > 1) {
    qa_range_error("Invalid trigger threshold");
  }
}

qa_vec2 qa_apply_stick_curve(qa_vec2 axis, qa_stick_curve curve) {
  if (curve.kind == QA_STICK_CURVE_AXIAL) {
    qa_vec2 out;
    double values[2] = {axis.x, axis.y};
    double applied[2];
    for (int i = 0; i < 2; i++) {
      double value = values[i];
      double magnitude = fmax(0, fmin(1, (fabs(value) - curve.deadzone) /
                                             (1 - curve.deadzone)));
      applied[i] = (value > 0 ? 1 : value < 0 ? -1 : 0) * pow(magnitude, curve.exponent);
    }
    out.x = applied[0];
    out.y = applied[1];
    return out;
  }
  double magnitude = hypot(axis.x, axis.y);
  if (magnitude <= curve.deadzone) {
    qa_vec2 zero = {0, 0};
    return zero;
  }
  double scale = pow(fmin(1, (magnitude - curve.deadzone) /
                                 (1 - curve.deadzone - curve.outer_threshold)),
                     curve.exponent) /
                 magnitude;
  qa_vec2 out = {axis.x * scale, axis.y * scale};
  return out;
}

static const char *const axis_names[6] = {"left-x", "left-y", "right-x",
                                          "right-y", "left-trigger", "right-trigger"};

qa_controller_axis qa_controller_axis_name(double axis) {
  if (axis != trunc(axis) || axis < 0 || axis > 5) return NULL;
  return axis_names[(int)axis];
}

double qa_normalized_controller_axis(qa_controller_axis axis, double raw) {
  size_t length = strlen(axis);
  bool trigger = length >= 7 && strcmp(axis + length - 7, "trigger") == 0;
  return fmax(trigger ? 0 : -1, fmin(1, raw / 32767));
}

static int axis_index(qa_controller_axis axis) {
  if (axis == NULL) return -1;
  for (int i = 0; i < 6; i++) {
    if (strcmp(axis, axis_names[i]) == 0) return i;
  }
  return -1;
}

static double clamped(double value) { return fmax(-1, fmin(1, value)); }

static double axis_value(const double *axes, const bool *set, int index) {
  return set[index] ? axes[index] : 0;
}

void qa_gamepad_input_init(qa_gamepad_input *input, const qa_gamepad_tuning *tuning) {
  memset(input, 0, sizeof *input);
  input->tuning = tuning != NULL ? *tuning : qa_default_gamepad_tuning;
  qa_validate_gamepad_tuning(&input->tuning);
}

void qa_gamepad_input_preview_axis(qa_gamepad_input *input, qa_controller_axis axis,
                                   double value) {
  int index = axis_index(axis);
  if (index < 0) return;
  input->preview_axes[index] = clamped(value);
  input->preview_set[index] = true;
}

qa_gamepad_preview qa_gamepad_input_preview(const qa_gamepad_input *input) {
  qa_vec2 left = {axis_value(input->preview_axes, input->preview_set, 0),
                  axis_value(input->preview_axes, input->preview_set, 1)};
  qa_vec2 right = {axis_value(input->preview_axes, input->preview_set, 2),
                   axis_value(input->preview_axes, input->preview_set, 3)};
  qa_vec2 move = input->tuning.swap_sticks ? right : left;
  qa_vec2 look = input->tuning.swap_sticks ? left : right;
  qa_gamepad_preview preview;
  preview.move_raw = move;
  preview.move_curved = qa_apply_stick_curve(move, input->tuning.move);
  preview.look_raw = look;
  preview.look_curved = qa_apply_stick_curve(look, input->tuning.look);
  return preview;
}

void qa_gamepad_input_axis(qa_gamepad_input *input, qa_controller_axis axis,
                           double value) {
  qa_gamepad_input_preview_axis(input, axis, value);
  int index = axis_index(axis);
  if (index < 0) return;
  input->axes[index] = clamped(value);
  input->axis_set[index] = true;
}

enum { GYRO_CALIBRATION_DURATION = 2000, GYRO_CALIBRATION_SAMPLES = 64 };

qa_gyro_calibration_state qa_gamepad_input_gyro_calibration(
    const qa_gamepad_input *input) {
  qa_gyro_calibration_state state;
  memset(&state, 0, sizeof state);
  if (input->calibrating) {
    state.kind = QA_GYRO_CALIBRATION_CALIBRATING;
    state.samples = input->has_capture ? input->capture_samples : 0;
    state.progress = !input->has_capture
                         ? 0
                         : fmin(1, fmin((input->capture_last - input->capture_start) /
                                            GYRO_CALIBRATION_DURATION,
                                        input->capture_samples /
                                            GYRO_CALIBRATION_SAMPLES));
    return state;
  }
  if (!input->has_gyro_bias) {
    state.kind = QA_GYRO_CALIBRATION_IDLE;
    return state;
  }
  state.kind = QA_GYRO_CALIBRATION_READY;
  state.bias = input->gyro_bias;
  return state;
}

void qa_gamepad_input_begin_gyro_calibration(qa_gamepad_input *input) {
  input->calibrating = true;
  input->has_capture = false;
  input->has_gyro_sample = false;
}

void qa_gamepad_input_cancel_gyro_calibration(qa_gamepad_input *input) {
  input->calibrating = false;
  input->has_capture = false;
  input->has_gyro_sample = false;
}

void qa_gamepad_input_reset_gyro_calibration(qa_gamepad_input *input) {
  qa_gamepad_input_cancel_gyro_calibration(input);
  input->has_gyro_bias = false;
}

void qa_gamepad_input_gyro(qa_gamepad_input *input, qa_vec3 value,
                           double time_milliseconds, bool aiming) {
  if (!isfinite(value.x) || !isfinite(value.y) || !isfinite(value.z) ||
      !isfinite(time_milliseconds) || time_milliseconds < 0) {
    qa_range_error("Invalid gyro sample or timestamp");
  }
  input->has_gyro_sample = aiming;
  if (aiming) input->gyro_sample = value;
  if (!input->calibrating) return;
  input->has_gyro_sample = false;
  if (hypot(hypot(value.x, value.y), value.z) > 0.15) {
    input->has_capture = false;
    return;
  }
  bool has_previous = input->has_capture;
  if (has_previous && time_milliseconds == input->capture_last) return;
  if (has_previous && (time_milliseconds < input->capture_last ||
                       time_milliseconds - input->capture_last > 250)) {
    has_previous = false;
  }
  if (!has_previous) {
    input->has_capture = true;
    input->capture_start = time_milliseconds;
    input->capture_last = time_milliseconds;
    input->capture_samples = 1;
    input->capture_mean = value;
    input->capture_deviation.x = 0;
    input->capture_deviation.y = 0;
    input->capture_deviation.z = 0;
    return;
  }
  double samples = input->capture_samples + 1;
  qa_vec3 mean = {input->capture_mean.x + (value.x - input->capture_mean.x) / samples,
                  input->capture_mean.y + (value.y - input->capture_mean.y) / samples,
                  input->capture_mean.z + (value.z - input->capture_mean.z) / samples};
  qa_vec3 deviation = {
      input->capture_deviation.x + (value.x - input->capture_mean.x) * (value.x - mean.x),
      input->capture_deviation.y + (value.y - input->capture_mean.y) * (value.y - mean.y),
      input->capture_deviation.z + (value.z - input->capture_mean.z) * (value.z - mean.z)};
  double limit = 0.01 * 0.01;
  if (fmax(deviation.x, fmax(deviation.y, deviation.z)) / (samples - 1) > limit) {
    input->has_capture = false;
    return;
  }
  double start = input->capture_start;
  input->capture_last = time_milliseconds;
  input->capture_samples = samples;
  input->capture_mean = mean;
  input->capture_deviation = deviation;
  if (time_milliseconds - start >= GYRO_CALIBRATION_DURATION &&
      samples >= GYRO_CALIBRATION_SAMPLES) {
    input->gyro_bias = mean;
    input->has_gyro_bias = true;
    qa_gamepad_input_cancel_gyro_calibration(input);
  }
}

qa_gamepad_sample qa_gamepad_input_sample(qa_gamepad_input *input,
                                          double frame_milliseconds) {
  qa_vec2 left = {axis_value(input->axes, input->axis_set, 0),
                  axis_value(input->axes, input->axis_set, 1)};
  qa_vec2 right = {axis_value(input->axes, input->axis_set, 2),
                   axis_value(input->axes, input->axis_set, 3)};
  qa_vec2 move = qa_apply_stick_curve(input->tuning.swap_sticks ? right : left,
                                      input->tuning.move);
  qa_vec2 look = qa_apply_stick_curve(input->tuning.swap_sticks ? left : right,
                                      input->tuning.look);
  double seconds = frame_milliseconds / 1000;
  double gyro_scale = input->tuning.gyro.enabled && !input->calibrating &&
                              input->has_gyro_sample
                          ? 180 / 3.141592653589793 * seconds
                          : 0;
  double bias_yaw = 0, bias_pitch = 0;
  if (input->has_gyro_bias) {
    bias_yaw = input->tuning.gyro.yaw_axis_z ? input->gyro_bias.z : input->gyro_bias.y;
    bias_pitch = input->gyro_bias.x;
  }
  double gyro_yaw = !input->has_gyro_sample
                        ? 0
                        : (input->tuning.gyro.yaw_axis_z ? input->gyro_sample.z
                                                         : input->gyro_sample.y) -
                              bias_yaw;
  double gyro_pitch = !input->has_gyro_sample ? 0 : input->gyro_sample.x - bias_pitch;
  qa_gamepad_sample sample;
  sample.move.x = move.x * input->tuning.side_sensitivity;
  sample.move.y = -move.y * input->tuning.forward_sensitivity;
  sample.look_degrees.x = look.x * input->tuning.yaw_degrees_per_second * seconds -
                          gyro_yaw * input->tuning.gyro.yaw_sensitivity * gyro_scale;
  sample.look_degrees.y =
      (look.y * input->tuning.pitch_degrees_per_second * seconds -
       gyro_pitch * input->tuning.gyro.pitch_sensitivity * gyro_scale) *
      (input->tuning.invert_pitch ? -1 : 1);
  return sample;
}

void qa_gamepad_input_clear(qa_gamepad_input *input) {
  memset(input->preview_axes, 0, sizeof input->preview_axes);
  memset(input->preview_set, 0, sizeof input->preview_set);
  memset(input->axes, 0, sizeof input->axes);
  memset(input->axis_set, 0, sizeof input->axis_set);
  qa_gamepad_input_cancel_gyro_calibration(input);
}
