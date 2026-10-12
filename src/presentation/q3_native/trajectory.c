#include "trajectory.h"

static qa_trajectory import_trajectory(const qa_q3_trajectory *source) {
    return (qa_trajectory){.type = (qa_trajectory_type)source->type,
        .time_ms = source->time, .duration_ms = source->duration,
        .base = {source->base[0], source->base[1], source->base[2]},
        .delta = {source->delta[0], source->delta[1], source->delta[2]}};
}
bool q3n_trajectory(const qa_q3_trajectory *t, int32_t time, qa_vec3 *out, qa_error *error) {
    qa_trajectory trajectory = import_trajectory(t);
    return qa_trajectory_position(&trajectory, time, 800, out, error);
}
bool q3n_trajectory_delta(const qa_q3_trajectory *t, int32_t time, qa_vec3 *out, qa_error *error) {
    qa_trajectory trajectory = import_trajectory(t);
    return qa_trajectory_velocity(&trajectory, time, 800, out, error);
}
