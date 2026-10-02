#ifndef QA_FRONTEND_Q1_SKY_PRIVATE_H
#define QA_FRONTEND_Q1_SKY_PRIVATE_H
#include "q1_sky.h"
#include "internal.h"

typedef struct frontend_q1_sky_selection {
    struct frontend_q1_sky_selection *next;
    qa_actor_owner provider;
    qa_actor_id recipient;
    qa_scene_resources *bank;
    char *name;
    const qa_scene_image *images[6];
    uint64_t sequence, map_revision;
    uint8_t found;
} frontend_q1_sky_selection;
struct frontend_q1_sky {
    qa_frontend *frontend;
    qa_application *application;
    qa_resource *map;
    qa_scene_resources *map_bank;
    uint64_t map_revision, next_sequence, fog_modification;
    float fog;
    bool q1_map, map_fog, busy;
    frontend_q1_sky_selection *baseline, *retained;
    frontend_q1_sky_policy *pending;
};
bool frontend_q1_sky_current(const frontend_q1_sky *);
bool frontend_q1_sky_selection_current(const frontend_q1_sky *, const frontend_q1_sky_selection *);
void frontend_q1_sky_selection_free(frontend_q1_sky_selection *);
bool frontend_q1_sky_selection_load(frontend_q1_sky_selection *, qa_scene_resources *, qa_error *);
const frontend_q1_sky_selection *frontend_q1_sky_selected(const frontend_q1_sky *, qa_actor_id);
#endif
