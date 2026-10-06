#ifndef APPLICATION_VISUAL_VISIBILITY_H
#define APPLICATION_VISUAL_VISIBILITY_H

#include "qa/application_visual_visibility.h"
#include "qa/native_host_q2_wire.h"

struct application_native_q2;

typedef struct application_q2_visibility_recipient {
    qa_actor_id actor;
    uint32_t slot;
    qa_vec3 origin;
    qa_collision_leaf leaf;
    int32_t clusters[64];
    size_t cluster_count;
    int32_t *pending;
    size_t pending_capacity;
} application_q2_visibility_recipient;

typedef struct application_q2_visibility_entity {
    qa_actor_id actor;
    qa_bounds bounds;
    uint32_t flags;
    const qa_native_host_q2_entity *original;
} application_q2_visibility_entity;

size_t application_q2_visibility_pending_capacity(const qa_application *);
bool application_q2_visibility_recipient_prepare(qa_application *, qa_actor_id,
    uint32_t slot, qa_vec3 origin, application_q2_visibility_recipient *, qa_error *);
bool application_q2_visibility_test(qa_application *, struct application_native_q2 *,
    bool rerelease, bool builtin, const application_q2_visibility_recipient *,
    const application_q2_visibility_entity *, const qa_q2_entity *, bool no_vis,
    bool *, qa_error *);

#endif
