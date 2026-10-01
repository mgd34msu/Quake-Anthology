#include "trajectory.h"

#include <string.h>

static float add(float a, float b) { volatile float value = a + b; return value; }
static float mul(float a, float b) { volatile float value = a * b; return value; }
static float divide(float a, float b) { volatile float value = a / b; return value; }
static int32_t word(uint32_t value) { int32_t result; memcpy(&result, &value, sizeof(result)); return result; }
static int32_t elapsed(int32_t a, int32_t b) { return word((uint32_t)a - (uint32_t)b); }
static int32_t end_time(const qa_q3_trajectory *t) { return word((uint32_t)t->time + (uint32_t)t->duration); }
static float seconds(int32_t milliseconds) { return mul((float)milliseconds, 0.001f); }
static float periodic(const qa_q3_trajectory *t, int32_t time) {
    return mul(mul(divide((float)elapsed(time, t->time), (float)t->duration), 3.14159265358979323846f), 2);
}
static qa_vec3 scaled(const float value[3], float scale) {
    return qa_v3(mul(value[0], scale), mul(value[1], scale), mul(value[2], scale));
}
static qa_vec3 displaced(const qa_q3_trajectory *t, float scale) {
    qa_vec3 delta = scaled(t->delta, scale);
    return qa_v3(add(t->base[0], delta.x), add(t->base[1], delta.y), add(t->base[2], delta.z));
}
static bool unknown(const qa_q3_trajectory *t, bool delta, qa_error *error) {
    qa_error_set(error, QA_ERROR_FORMAT, 0, "BG_EvaluateTrajectory%s: unknown trType: %d", delta ? "Delta" : "", t->time);
    return false;
}
bool q3n_trajectory(const qa_q3_trajectory *t, int32_t time, qa_vec3 *out, qa_error *error) {
    qa_vec3 result;
    switch (t->type) {
    case 0: case 1: result = qa_v3(t->base[0], t->base[1], t->base[2]); break;
    case 2: result = displaced(t, seconds(elapsed(time, t->time))); break;
    case 3: {
        int32_t end = end_time(t), sample = time > end ? end : time;
        result = displaced(t, fmaxf(0, seconds(elapsed(sample, t->time)))); break;
    }
    case 4: result = displaced(t, (float)sin((double)periodic(t, time))); break;
    case 5: {
        float dt = seconds(elapsed(time, t->time)); result = displaced(t, dt);
        result.z = add(result.z, -mul(mul(400, dt), dt)); break;
    }
    default: return unknown(t, false, error);
    }
    *out = result; return true;
}
bool q3n_trajectory_delta(const qa_q3_trajectory *t, int32_t time, qa_vec3 *out, qa_error *error) {
    qa_vec3 result;
    switch (t->type) {
    case 0: case 1: result = qa_v3(0, 0, 0); break;
    case 2: result = qa_v3(t->delta[0], t->delta[1], t->delta[2]); break;
    case 3: result = time > end_time(t) ? qa_v3(0, 0, 0) : qa_v3(t->delta[0], t->delta[1], t->delta[2]); break;
    case 4: result = scaled(t->delta, mul((float)cos((double)periodic(t, time)), 0.5f)); break;
    case 5: result = qa_v3(t->delta[0], t->delta[1],
        add(t->delta[2], -mul(800, seconds(elapsed(time, t->time))))); break;
    default: return unknown(t, true, error);
    }
    *out = result; return true;
}
