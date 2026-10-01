#ifndef QA_APPLICATION_Q3_SCENE_WORLD_H
#define QA_APPLICATION_Q3_SCENE_WORLD_H

#include "qa/application.h"

typedef struct qa_application_q3_scene_role {
    qa_actor_owner receiver;
    qa_qvm_role role;
    uint32_t seat;
    uint64_t service_owner;
    const void *frontend_lifetime;
} qa_application_q3_scene_role;

/* The tuple names an actual installed CGAME/UI lease, including its authored
 * seat. These operations admit idle construction/restoration without requiring
 * source Init, an actor receipt, a clock read or a source callback. */
bool qa_application_q3_scene_world_read(qa_application *,
    const qa_application_q3_scene_role *, const qa_scene_world **, qa_error *);
bool qa_application_q3_scene_world_rebind_ready(qa_application *,
    const qa_application_q3_scene_role *, const qa_scene_world *current,
    const qa_scene_world *destination, qa_error *);
/* Preflight every affected lease before binding any; preserve those leases and
 * world owners until this nofail exchange. This function moves only a borrow. */
void qa_application_q3_scene_world_rebind(qa_application *,
    const qa_application_q3_scene_role *, qa_scene_world *destination);

#endif
