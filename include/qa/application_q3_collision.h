#ifndef QA_APPLICATION_Q3_COLLISION_H
#define QA_APPLICATION_Q3_COLLISION_H
#include "qa/application.h"
#include "qa/q3_host_collision.h"

/* Pure physical CG inventory. A genuine undeclared scene clears outputs.
 * The returned lower hold pins the exact host until checked release. */
bool qa_application_q3_collision_scene_hold(qa_application *, qa_actor_owner receiver,
    uint32_t authored_seat, uint64_t service_owner, qa_q3_host_collision_scene **,
    bool *present, qa_error *);
#endif
