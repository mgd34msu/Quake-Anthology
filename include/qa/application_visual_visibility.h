#ifndef QA_APPLICATION_VISUAL_VISIBILITY_H
#define QA_APPLICATION_VISUAL_VISIBILITY_H

#include "qa/application.h"

typedef struct qa_application_visual_visibility qa_application_visual_visibility;

/* Cold map admission owns one max_align_t-aligned view workspace. Prepare
 * overwrites only that workspace; simultaneous views use separate owners. */
bool qa_application_visual_visibility_create(const qa_application *,
    qa_application_visual_visibility **, qa_error *);
void qa_application_visual_visibility_destroy(qa_application_visual_visibility *);
bool qa_application_visual_visibility_prepare(qa_application *, qa_actor_id recipient,
    qa_vec3 pvs_origin, bool no_vis, qa_application_visual_visibility *, qa_error *);
bool qa_application_visual_visibility_actor(const qa_application_visual_visibility *,
    qa_actor_id, const qa_application_visual_view *appearance, bool *, qa_error *);

#endif
