#ifndef QA_AAS_REACHABILITY_INTERNAL_H
#define QA_AAS_REACHABILITY_INTERNAL_H
#include "internal.h"

enum {
    AAS_REACH_GROUNDED = 1, AAS_REACH_SWIMMING = 4,
    AAS_REACH_FACE_SOLID = 1, AAS_REACH_FACE_GROUND = 4,
    AAS_REACH_TELEPORT = 64, AAS_REACH_JUMPPAD = 128,
    AAS_REACH_WALK = 2, AAS_REACH_BARRIER = 5, AAS_REACH_LEDGE = 7,
    AAS_REACH_SWIM = 8, AAS_REACH_WATERJUMP = 9
};
typedef struct aas_reach_settings {
    float gravity, max_step, max_water_jump, max_barrier, start_crouch,
        water_jump, barrier_jump, start_ledge, max_fall_height,
        fall_delta5, fall_delta10, fall_damage5, fall_damage10;
} aas_reach_settings;
typedef struct aas_reach_link {
    qa_aas_reach reach;
    struct aas_reach_link *next;
} aas_reach_link;
typedef struct aas_reach_build {
    qa_navigation *navigation;
    const qa_aas_view *source;
    qa_arena storage;
    qa_aas_query *query;
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
bool aas_reach_geometry(aas_reach_build *, uint32_t from, uint32_t to, bool *found, qa_error *);
bool aas_reach_walk_ledge(aas_reach_build *, uint32_t area, qa_error *);
float aas_reach_fall_delta(const aas_reach_build *, float);

#endif
