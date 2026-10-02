#ifndef QA_APPLICATION_Q3_COMPONENT_BODY_H
#define QA_APPLICATION_Q3_COMPONENT_BODY_H
#include "qa/q3_presentation.h"

#include "qa/application_q3_body_source.h"

typedef struct application_q3_component_body application_q3_component_body;
typedef struct qa_application_q3_component_body_lease qa_application_q3_component_body_lease;
typedef struct qa_application_q3_component_part {
    qa_application_q3_body_part part;
    uint32_t helper;
    bool base;
    const qa_q3_ref_entity *passes;
    size_t count;
} qa_application_q3_component_part;
typedef struct qa_application_q3_component_actor {
    qa_actor_id actor;
    const qa_application_q3_component_part *parts;
    size_t count;
} qa_application_q3_component_actor;
typedef struct qa_application_q3_component_bodies {
    const application_q3_component_body *producer;
    qa_actor_owner owner;
    qa_q3_presentation_assets *assets;
    uint64_t sequence, generation;
    int32_t time_ms;
    size_t count;
} qa_application_q3_component_bodies;

/* Borrow the completed original component helper output in its own numeric
 * registry. These observations never invoke guest code or register assets.
 * A new component frame invalidates the previous borrowed arrays. */
bool qa_application_q3_component_bodies_read(const application_q3_component_body *,
    qa_application_q3_component_bodies *);
bool qa_application_q3_component_bodies_current(const qa_application_q3_component_bodies *);
bool qa_application_q3_component_body_at(const qa_application_q3_component_bodies *,
    size_t, qa_application_q3_component_actor *);
/* The actual primary Draw retains these owners until all source submissions
 * return. A lease blocks the component's next advance, capture and teardown;
 * return consumes it even if its reached source has become stale. */
bool qa_application_q3_component_bodies_borrow(application_q3_component_body *,
    qa_application_q3_component_body_lease **, qa_application_q3_component_bodies *, qa_error *);
void qa_application_q3_component_bodies_return(qa_application_q3_component_body_lease **);

#endif
