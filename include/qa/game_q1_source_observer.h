#ifndef QA_GAME_Q1_SOURCE_OBSERVER_H
#define QA_GAME_Q1_SOURCE_OBSERVER_H
#include "qa/game_q1.h"

/* Projects the actual observer body through source velocity, door-group and
 * teleporter geometry. The caller publishes the returned body and any real
 * selected movement discontinuity; this query invokes no source callback. */
bool qa_q1_source_observer_body(qa_q1_game *, qa_actor_id, const qa_q1_input *,
    qa_body_state *projected, qa_body_state *passage, bool *passage_written,
    bool *teleported, qa_vec3 *view_angles, double *until, qa_error *);
#endif
