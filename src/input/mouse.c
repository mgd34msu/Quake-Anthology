#include "qa/input.h"

qa_mouse_tuning qa_mouse_defaults(void) {
    return (qa_mouse_tuning){.sensitivity = 3,
                             .yaw = 0.022f,
                             .pitch = 0.022f,
                             .side = 0.8f,
                             .forward = 1,
                             .free_look = true};
}
bool qa_mouse_tuning_valid(const qa_mouse_tuning *t) {
    return t && isfinite(t->sensitivity) && isfinite(t->acceleration) && isfinite(t->yaw) &&
           isfinite(t->pitch) && isfinite(t->side) && isfinite(t->forward);
}
bool qa_mouse_sample(qa_mouse_input *input, const qa_mouse_tuning *t, qa_vec2 raw,
                     double frame, bool strafe, bool look, float zoom, qa_mouse_move *out,
                     qa_error *error) {
    if (!input || !out || !qa_mouse_tuning_valid(t) || !isfinite(raw.x) || !isfinite(raw.y) ||
        !isfinite(frame) || frame <= 0 || !isfinite(zoom)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid mouse sample");
        return false;
    }
    float x = t->filter ? (raw.x + input->previous.x) * 0.5f : raw.x;
    float y = t->filter ? (raw.y + input->previous.y) * 0.5f : raw.y;
    float rate = sqrtf(x * x + y * y) / (float)frame;
    float gain = (t->sensitivity + rate * t->acceleration) * zoom;
    x *= gain;
    y *= gain;
    bool horizontal_strafe = strafe || (t->look_strafe && look);
    bool pitch = !strafe && (look || t->free_look);
    qa_mouse_move result = {.yaw = horizontal_strafe ? 0 : -t->yaw * x,
                            .side = horizontal_strafe ? t->side * x : 0,
                            .pitch = pitch ? t->pitch * y * (t->invert_pitch ? -1.0f : 1.0f) : 0,
                            .forward = pitch ? 0 : -t->forward * y};
    if (!isfinite(result.yaw) || !isfinite(result.side) || !isfinite(result.pitch) ||
        !isfinite(result.forward)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Mouse movement overflow");
        return false;
    }
    input->previous = raw;
    *out = result;
    return true;
}

float qa_pitch_drift_sample(qa_pitch_drift *drift, float pitch, float seconds,
                            const qa_pitch_drift_input *in) {
    if (in->manual)
        *drift = (qa_pitch_drift){0};
    else if (in->start && (!drift->drifting || drift->velocity == 0)) {
        drift->drifting = true;
        drift->velocity = in->speed;
        drift->moving_seconds = 0;
    }
    if (in->disabled || !in->grounded) {
        drift->moving_seconds = drift->velocity = 0;
        return pitch;
    }
    if (!drift->drifting) {
        drift->moving_seconds =
            in->manual || fabsf(in->forward) < in->threshold ? 0 : drift->moving_seconds + seconds;
        if (drift->moving_seconds > in->delay) {
            drift->drifting = true;
            drift->velocity = in->speed;
            drift->moving_seconds = 0;
        }
        return pitch;
    }
    float delta = in->ideal_pitch - pitch;
    if (delta == 0) {
        drift->velocity = 0;
        return pitch;
    }
    float move = fminf(fabsf(delta), seconds * drift->velocity);
    drift->velocity += seconds * in->speed;
    if (move == fabsf(delta))
        drift->velocity = 0;
    return pitch + (delta < 0 ? -move : move);
}
