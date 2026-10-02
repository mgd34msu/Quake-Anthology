#ifndef QA_FRONTEND_UNIFIED_INPUT_COMMAND_H
#define QA_FRONTEND_UNIFIED_INPUT_COMMAND_H
#include "qa/input.h"
#include "qa/network_unified.h"

/* The physical sampler owns its native float samples. Command accumulation
 * and the unified payload retain binary64 until the selected provider projects
 * the received command. These angles are absolute in the unified dialect. */
typedef struct frontend_unified_command_builder {
    qa_movement_kind kind;
    qa_unified_vec3 angles;
    double mouse_x, mouse_y, drift_velocity, drift_seconds;
    bool drifting, previous_mouse_look;
} frontend_unified_command_builder;
typedef struct frontend_unified_command_frame {
    qa_movement_kind kind;
    double acknowledged_seconds, server_time_ms, server_frame;
    double weapon, sensitivity, light_level;
    bool attack_allowed, has_pitch_drift, grounded, drift_disabled;
    double ideal_pitch;
} frontend_unified_command_frame;
bool frontend_unified_command_build(frontend_unified_command_builder *,
    const qa_input_command_tuning *, const qa_seat_input_sample *,
    const frontend_unified_command_frame *, double elapsed_ms,
    qa_unified_movement *, qa_error *);
#endif
