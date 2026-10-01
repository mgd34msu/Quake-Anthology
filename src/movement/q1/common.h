#ifndef QA_MOVEMENT_Q1_COMMON_H
#define QA_MOVEMENT_Q1_COMMON_H

#include "../internal.h"

enum {
    Q1_MOVE_NONE = 0, Q1_MOVE_WALK = 3, Q1_MOVE_STEP = 4,
    Q1_MOVE_FLY = 5, Q1_MOVE_TOSS = 6, Q1_MOVE_NOCLIP = 8,
    Q1_MOVE_FLYMISSILE = 9, Q1_MOVE_BOUNCE = 10, Q1_MOVE_GIB = 11,
    Q1_FLAG_FLY = 1, Q1_FLAG_SWIM = 2, Q1_FLAG_ONGROUND = 512,
    Q1_FLAG_WATERJUMP = 2048, Q1_FLAG_JUMPRELEASED = 4096,
    Q1_CONTENTS_EMPTY = -1, Q1_CONTENTS_SOLID = -2,
    Q1_CONTENTS_WATER = -3, Q1_CONTENTS_SLIME = -4
};

typedef struct q1_move {
    qa_move_context *c;
    int32_t source_mode;
    bool projected;
    bool owned_bounds;
    bool defer_body_shape;
    bool force_box;
    bool crouch_animation;
    bool has_starting_bounds;
    qa_bounds starting_bounds, body_bounds;
    bool qw;
} q1_move;

typedef struct q1_fly_result {
    unsigned blocked;
    bool has_step_trace;
    qa_trace_result step_trace;
} q1_fly_result;

void q1_init(q1_move *, qa_move_context *, bool qw);
void q1_restore_mode(q1_move *);
void q1_resume(q1_move *);
bool q1_phase(q1_move *, qa_movement_phase);
bool q1_body_shape(q1_move *);
bool q1_touch(q1_move *, const qa_trace_result *, bool record);
bool q1_sound(q1_move *, const char *);
bool q1_action(q1_move *, qa_movement_locomotion);
bool q1_trace(q1_move *, qa_vec3 start, qa_vec3 end, qa_trace_result *);
bool q1_point_trace(q1_move *, qa_vec3 start, qa_vec3 end, qa_trace_result *);
bool q1_contents(q1_move *, qa_vec3, int32_t *);
bool q1_position_free(q1_move *, qa_vec3, bool *);
bool q1_fly(q1_move *, double dt, q1_fly_result *);
bool q1_finish(q1_move *, qa_movement_ground, int32_t water_level, int32_t water_type);
qa_trace_shape q1_shape(const q1_move *);
float q1_speed(const q1_move *, float);

static inline qa_vec3 q1_ma(qa_vec3 origin, float dt, qa_vec3 velocity) {
    return qa_vec_add(origin, qa_vec_scale(velocity, dt));
}
/* QW donor arithmetic evaluates these expressions in binary64, then stores
 * each vector component as float. The input center is not stored first. */
static inline qa_qw_origin q1_qw_add(qa_qw_origin origin, qa_vec3 delta) {
    return qa_qw_origin_from_vec3(qa_v3((float)(origin.x + delta.x),
                                     (float)(origin.y + delta.y),
                                     (float)(origin.z + delta.z)));
}
static inline qa_qw_origin q1_qw_ma(qa_qw_origin origin, double dt, qa_vec3 velocity) {
    return qa_qw_origin_from_vec3(qa_v3((float)(origin.x + dt * velocity.x),
                                     (float)(origin.y + dt * velocity.y),
                                     (float)(origin.z + dt * velocity.z)));
}
static inline qa_movement_ground q1_no_ground(void) {
    return (qa_movement_ground){ .hit = QA_TRACE_HIT_NONE };
}
static inline bool q1_stopped(const q1_move *m) { return m->c->failed || m->c->removed; }

#endif
