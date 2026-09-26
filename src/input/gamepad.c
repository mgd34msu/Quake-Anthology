#include "qa/input.h"
#include <string.h>

static float clamp(float value, float low, float high) { return fmaxf(low, fminf(high, value)); }
static bool curve_valid(const qa_stick_curve *curve) {
    return (curve->kind == QA_STICK_RADIAL || curve->kind == QA_STICK_AXIAL) &&
           isfinite(curve->deadzone) && curve->deadzone >= 0 && curve->deadzone < 1 &&
           isfinite(curve->exponent) && curve->exponent > 0 &&
           (curve->kind == QA_STICK_AXIAL ||
            (isfinite(curve->outer_threshold) && curve->outer_threshold >= 0 &&
             curve->deadzone + curve->outer_threshold < 1));
}
qa_gamepad_tuning qa_gamepad_defaults(void) {
    qa_stick_curve curve = {
        .kind = QA_STICK_RADIAL, .deadzone = 0.175f, .outer_threshold = 0.02f, .exponent = 2};
    return (qa_gamepad_tuning){.move = curve,
                               .look = curve,
                               .yaw_speed = 240,
                               .pitch_speed = 130,
                               .forward_sensitivity = 1,
                               .side_sensitivity = 1,
                               .trigger_threshold = 0.2f,
                               .gyro_yaw_sensitivity = 1,
                               .gyro_pitch_sensitivity = 1,
                               .gyro_yaw_axis = QA_GYRO_YAW_Y};
}
bool qa_gamepad_tuning_valid(const qa_gamepad_tuning *t) {
    return t && curve_valid(&t->move) && curve_valid(&t->look) && isfinite(t->yaw_speed) &&
           isfinite(t->pitch_speed) && isfinite(t->forward_sensitivity) &&
           isfinite(t->side_sensitivity) && isfinite(t->gyro_yaw_sensitivity) &&
           isfinite(t->gyro_pitch_sensitivity) && isfinite(t->trigger_threshold) &&
           t->trigger_threshold >= 0 && t->trigger_threshold <= 1 &&
           (t->gyro_yaw_axis == QA_GYRO_YAW_Y || t->gyro_yaw_axis == QA_GYRO_YAW_Z);
}
qa_input_pair qa_stick_apply(qa_input_pair value, const qa_stick_curve *curve) {
    if (curve->kind == QA_STICK_AXIAL) {
        float x = powf(clamp((fabsf(value.x) - curve->deadzone) / (1 - curve->deadzone), 0, 1),
                       curve->exponent);
        float y = powf(clamp((fabsf(value.y) - curve->deadzone) / (1 - curve->deadzone), 0, 1),
                       curve->exponent);
        return (qa_input_pair){copysignf(x, value.x), copysignf(y, value.y)};
    }
    float magnitude = hypotf(value.x, value.y);
    if (magnitude <= curve->deadzone)
        return (qa_input_pair){0};
    float scale = powf(fminf(1, (magnitude - curve->deadzone) /
                                    (1 - curve->deadzone - curve->outer_threshold)),
                       curve->exponent) /
                  magnitude;
    return (qa_input_pair){value.x * scale, value.y * scale};
}
float qa_controller_axis_normalize(qa_controller_axis axis, int16_t raw) {
    return clamp((float)raw / 32767, axis >= QA_AXIS_LEFT_TRIGGER ? 0 : -1, 1);
}
bool qa_gamepad_axis(qa_gamepad_input *input, qa_controller_axis axis, float value, bool aiming,
                     qa_error *error) {
    if (!input || axis < QA_AXIS_LEFT_X || axis >= QA_AXIS_COUNT || !isfinite(value)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid controller axis");
        return false;
    }
    input->preview_axes[axis] = clamp(value, -1, 1);
    if (aiming)
        input->axes[axis] = input->preview_axes[axis];
    return true;
}
static void sticks(const float *axes, const qa_gamepad_tuning *t, qa_input_pair *move,
                   qa_input_pair *look) {
    qa_input_pair left = {axes[QA_AXIS_LEFT_X], axes[QA_AXIS_LEFT_Y]};
    qa_input_pair right = {axes[QA_AXIS_RIGHT_X], axes[QA_AXIS_RIGHT_Y]};
    *move = t->swap_sticks ? right : left;
    *look = t->swap_sticks ? left : right;
}
void qa_gamepad_preview_read(const qa_gamepad_input *input, const qa_gamepad_tuning *t,
                             qa_gamepad_preview *out) {
    sticks(input->preview_axes, t, &out->move_raw, &out->look_raw);
    out->move_curved = qa_stick_apply(out->move_raw, &t->move);
    out->look_curved = qa_stick_apply(out->look_raw, &t->look);
}
void qa_gamepad_calibration_begin(qa_gamepad_input *input) {
    input->calibrating = true;
    input->has_sample = false;
    input->capture = (qa_gyro_capture){0};
}
void qa_gamepad_calibration_cancel(qa_gamepad_input *input) {
    input->calibrating = false;
    input->has_sample = false;
    input->capture = (qa_gyro_capture){0};
}
void qa_gamepad_calibration_reset(qa_gamepad_input *input) {
    qa_gamepad_calibration_cancel(input);
    input->has_bias = false;
    input->gyro_bias = qa_v3(0, 0, 0);
}
qa_gyro_status qa_gamepad_calibration_status(const qa_gamepad_input *input) {
    if (input->calibrating)
        return (qa_gyro_status){
            .state = QA_GYRO_CALIBRATING,
            .progress =
                (float)fmin(1, fmin((input->capture.last_ms - input->capture.start_ms) / 2000,
                                    (double)input->capture.samples / 64)),
            .samples = input->capture.samples};
    return (qa_gyro_status){.state = input->has_bias ? QA_GYRO_READY : QA_GYRO_IDLE,
                            .bias = input->gyro_bias};
}
bool qa_gamepad_gyro(qa_gamepad_input *input, qa_vec3 value, double time, bool aiming,
                     qa_error *error) {
    if (!input || !qa_vec_finite(value) || !isfinite(time) || time < 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid gyro sample");
        return false;
    }
    input->has_sample = aiming;
    input->gyro_sample = value;
    if (!input->calibrating)
        return true;
    input->has_sample = false;
    qa_gyro_capture *capture = &input->capture;
    if (qa_vec_length(value) > 0.15f) {
        *capture = (qa_gyro_capture){0};
        return true;
    }
    if (capture->samples && time == capture->last_ms)
        return true;
    if (!capture->samples || time < capture->last_ms || time - capture->last_ms > 250) {
        *capture =
            (qa_gyro_capture){.start_ms = time, .last_ms = time, .samples = 1, .mean = value};
        return true;
    }
    ++capture->samples;
    qa_vec3 before = qa_vec_sub(value, capture->mean);
    capture->mean = qa_vec_add(capture->mean, qa_vec_scale(before, 1.0f / (float)capture->samples));
    qa_vec3 after = qa_vec_sub(value, capture->mean);
    capture->deviation = qa_vec_add(
        capture->deviation, qa_v3(before.x * after.x, before.y * after.y, before.z * after.z));
    float maximum = fmaxf(capture->deviation.x, fmaxf(capture->deviation.y, capture->deviation.z));
    if (maximum / (float)(capture->samples - 1) > 0.0001f) {
        *capture = (qa_gyro_capture){0};
        return true;
    }
    capture->last_ms = time;
    if (time - capture->start_ms >= 2000 && capture->samples >= 64) {
        input->has_bias = true;
        input->gyro_bias = capture->mean;
        qa_gamepad_calibration_cancel(input);
    }
    return true;
}
bool qa_gamepad_sample_read(const qa_gamepad_input *input, const qa_gamepad_tuning *t, double frame,
                            qa_gamepad_sample *out, qa_error *error) {
    if (!input || !out || !qa_gamepad_tuning_valid(t) || !isfinite(frame) || frame < 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid controller sample");
        return false;
    }
    qa_input_pair move, look;
    sticks(input->axes, t, &move, &look);
    move = qa_stick_apply(move, &t->move);
    look = qa_stick_apply(look, &t->look);
    float seconds = (float)(frame / 1000);
    float gyro_scale = t->gyro_enabled && !input->calibrating && input->has_sample
                           ? 57.29577951308232f * seconds
                           : 0;
    qa_vec3 gyro = input->has_sample ? input->gyro_sample : qa_v3(0, 0, 0);
    if (input->has_sample && input->has_bias)
        gyro = qa_vec_sub(gyro, input->gyro_bias);
    float yaw = t->gyro_yaw_axis == QA_GYRO_YAW_Y ? gyro.y : gyro.z;
    qa_gamepad_sample result = {
        .move = {move.x * t->side_sensitivity, -move.y * t->forward_sensitivity},
        .look_degrees = {
            look.x * t->yaw_speed * seconds - yaw * t->gyro_yaw_sensitivity * gyro_scale,
            (look.y * t->pitch_speed * seconds - gyro.x * t->gyro_pitch_sensitivity * gyro_scale) *
                (t->invert_pitch ? -1.0f : 1.0f)}};
    if (!isfinite(result.move.x) || !isfinite(result.move.y) || !isfinite(result.look_degrees.x) ||
        !isfinite(result.look_degrees.y)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Controller movement overflow");
        return false;
    }
    *out = result;
    return true;
}
void qa_gamepad_clear(qa_gamepad_input *input) {
    memset(input->axes, 0, sizeof(input->axes));
    memset(input->preview_axes, 0, sizeof(input->preview_axes));
    qa_gamepad_calibration_cancel(input);
}
