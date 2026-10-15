#ifndef QA_AAS_REACHABILITY_INTERNAL_H
#define QA_AAS_REACHABILITY_INTERNAL_H
#include "internal.h"

enum {
    AAS_REACH_GROUNDED = 1, AAS_REACH_LADDER_AREA = 2, AAS_REACH_SWIMMING = 4,
    AAS_REACH_FACE_SOLID = 1, AAS_REACH_FACE_LADDER = 2, AAS_REACH_FACE_GROUND = 4,
    AAS_REACH_TELEPORT = 64, AAS_REACH_JUMPPAD = 128,
    AAS_REACH_WEAPON_JUMP_AREA = 8192,
    AAS_REACH_WALK = 2, AAS_REACH_JUMP = 4, AAS_REACH_BARRIER = 5,
    AAS_REACH_LADDER = 6, AAS_REACH_LEDGE = 7,
    AAS_REACH_SWIM = 8, AAS_REACH_WATERJUMP = 9,
    AAS_REACH_TELEPORT_TYPE = 10, AAS_REACH_ELEVATOR = 11,
    AAS_REACH_ROCKET = 12, AAS_REACH_GRAPPLE = 14,
    AAS_REACH_JUMPPAD_TYPE = 18, AAS_REACH_BOBBING = 19
};
typedef struct aas_reach_settings {
    float gravity, max_step, max_water_jump, max_barrier, start_crouch,
        water_jump, barrier_jump, start_ledge, max_fall_height,
        fall_delta5, fall_delta10, fall_damage5, fall_damage10,
        max_velocity, max_walk_velocity, jump_velocity, max_jump_fall_height, start_jump,
        teleport, jump_pad, air_jump_pad, start_elevator, func_bob, start_grapple, rocket_jump;
    bool grapple;
} aas_reach_settings;
typedef struct aas_reach_closest_edges {
    qa_vec3 start[2], end[2];
    float distance;
    bool found;
} aas_reach_closest_edges;
typedef struct aas_reach_link {
    qa_aas_reach reach;
    struct aas_reach_link *next;
} aas_reach_link;
typedef struct aas_reach_build {
    qa_navigation *navigation;
    const qa_aas_view *source;
    const qa_bsp_view *geometry;
    qa_entities entities;
    uint32_t *entity_areas;
    size_t entity_area_count;
    qa_arena storage;
    qa_aas_query *query;
    qa_nav_workspace *workspace;
    qa_nav_prediction_result prediction;
    qa_movement_environment environment;
    qa_aas_setting *settings;
    aas_reach_link **heads;
    size_t link_count;
    aas_reach_settings movement;
} aas_reach_build;

static inline bool aas_reach_grounded(const aas_reach_build *w, uint32_t area) {
    return (w->settings[area].flags & AAS_REACH_GROUNDED) != 0;
}
static inline bool aas_reach_swimming(const aas_reach_build *w, uint32_t area) {
    return (w->settings[area].flags & AAS_REACH_SWIMMING) != 0;
}
static inline bool aas_reach_crouched(const aas_reach_build *w, uint32_t area) {
    return (w->settings[area].presence & 2) == 0;
}
bool aas_reach_exists(const aas_reach_build *, uint32_t from, uint32_t to);
bool aas_reach_add(aas_reach_build *, uint32_t from, qa_aas_reach, qa_error *);
bool aas_reach_time(double, uint16_t *, qa_error *);
bool aas_reach_integer(double, int32_t *, qa_error *);
bool aas_reach_geometry(aas_reach_build *, uint32_t from, uint32_t to, bool *found, qa_error *);
bool aas_reach_walk_ledge(aas_reach_build *, uint32_t area, qa_error *);
float aas_reach_fall_delta(const aas_reach_build *, float);
qa_vec3 aas_reach_face_center(const qa_aas_view *, const qa_aas_face *);
void aas_reach_closest_edge_points(qa_vec3, qa_vec3, qa_vec3, qa_vec3,
    const qa_aas_plane *, const qa_aas_plane *, aas_reach_closest_edges *);
float aas_reach_max_jump_distance(const aas_reach_build *);
bool aas_reach_horizontal_jump_speed(const aas_reach_build *, float z_velocity,
    qa_vec3 start, qa_vec3 end, float *speed);
bool aas_reach_predict(aas_reach_build *, qa_nav_prediction_query *, qa_error *);
bool aas_reach_world_trace(aas_reach_build *, qa_vec3 start, qa_vec3 end,
    const qa_bounds *, qa_trace_result *, qa_error *);
static inline uint32_t aas_reach_prediction_frames(const aas_reach_build *work) {
    return work->prediction.frames - (work->prediction.stop_event && work->prediction.frames ? 1u : 0u);
}
bool aas_reach_special(aas_reach_build *, qa_error *);
bool aas_reach_special_prepare(aas_reach_build *, qa_error *);
bool aas_reach_grapple(aas_reach_build *, uint32_t from, uint32_t to, qa_error *);
bool aas_reach_weapon_jump(aas_reach_build *, uint32_t from, uint32_t to, qa_error *);

#endif
