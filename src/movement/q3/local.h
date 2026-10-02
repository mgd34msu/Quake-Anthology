#ifndef QA_MOVEMENT_Q3_LOCAL_H
#define QA_MOVEMENT_Q3_LOCAL_H

#include "../internal.h"

enum {
    Q3_NORMAL, Q3_NOCLIP, Q3_SPECTATOR, Q3_DEAD, Q3_FREEZE,
    Q3_INTERMISSION, Q3_SPINTERMISSION
};
enum {
    Q3_DUCKED = 1, Q3_JUMP_HELD = 2, Q3_BACKWARDS_JUMP = 8,
    Q3_BACKWARDS_RUN = 16, Q3_TIME_LAND = 32, Q3_TIME_KNOCKBACK = 64,
    Q3_TIME_WATERJUMP = 256, Q3_RESPAWNED = 512, Q3_GRAPPLE_PULL = 2048,
    Q3_INVULEXPAND = 16384,
    Q3_ALL_TIMES = Q3_TIME_LAND | Q3_TIME_KNOCKBACK | Q3_TIME_WATERJUMP,
    Q3_ATTACK = 1, Q3_TALK = 2, Q3_USE_HOLDABLE = 4, Q3_WALKING = 16,
    Q3_SURF_NODAMAGE = 1, Q3_SURF_SLICK = 2,
    Q3_SURF_METALSTEPS = 0x1000, Q3_SURF_NOSTEPS = 0x2000,
    Q3_MASK_WATER = 56, Q3_CONTENTS_BODY = 0x2000000
};
enum {
    Q3_EV_FOOTSTEP = 1, Q3_EV_FOOTSTEP_METAL, Q3_EV_FOOTSPLASH,
    Q3_EV_FOOTWADE, Q3_EV_SWIM, Q3_EV_STEP_4, Q3_EV_STEP_8,
    Q3_EV_STEP_12, Q3_EV_STEP_16, Q3_EV_FALL_SHORT, Q3_EV_FALL_MEDIUM,
    Q3_EV_FALL_FAR, Q3_EV_JUMP_PAD, Q3_EV_JUMP, Q3_EV_WATER_TOUCH,
    Q3_EV_WATER_LEAVE, Q3_EV_WATER_UNDER, Q3_EV_WATER_CLEAR
};
enum {
    Q3_LEGS_WALKCR = 13, Q3_LEGS_WALK, Q3_LEGS_RUN, Q3_LEGS_BACK,
    Q3_LEGS_SWIM, Q3_LEGS_JUMP, Q3_LEGS_LAND, Q3_LEGS_JUMPB,
    Q3_LEGS_LANDB, Q3_LEGS_IDLE, Q3_LEGS_IDLECR,
    Q3_LEGS_BACKCR = 32, Q3_LEGS_BACKWALK = 33
};

typedef struct qa_q3_step {
    qa_move_context *context;
    qa_vec3 previous_origin, previous_velocity, forward, right, ground_normal;
    uint32_t mask, milliseconds;
    int32_t ground_surface_flags, water_level, water_type;
    float dt, impact_speed, horizontal_speed;
    bool ground_plane, walking;
} qa_q3_step;

static inline qa_q3_movement_state *q3_state(qa_q3_step *step) {
    return &step->context->state->data.q3;
}
static inline bool q3_active(const qa_q3_step *step) {
    return !step->context->failed && !step->context->removed;
}
static inline float q3_gravity(const qa_q3_step *step) {
    return truncf((float)step->context->state->data.q3.gravity *
                  step->context->input->environment.gravity_multiplier);
}
static inline float q3_speed(const qa_q3_step *step) {
    return truncf((float)step->context->state->data.q3.speed *
                  step->context->input->environment.speed_multiplier);
}
static inline int32_t q3_type(const qa_q3_step *step) {
    const qa_movement_environment *environment = &step->context->input->environment;
    return environment->has_mode && environment->health > 0
        ? qa_move_mode_type(QA_MOVEMENT_Q3, environment->mode)
        : step->context->state->data.q3.movement_type;
}

qa_vec3 q3_clip(qa_vec3, qa_vec3);
bool q3_move_trace(qa_q3_step *, qa_vec3, qa_vec3, qa_trace_result *);
bool q3_contact(qa_q3_step *, const qa_trace_result *);
bool q3_slide(qa_q3_step *, bool gravity);
void q3_step_slide(qa_q3_step *, bool gravity);

#endif
