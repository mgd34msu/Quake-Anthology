#ifndef QA_APPLICATION_VISUAL_VISIBILITY_H
#define QA_APPLICATION_VISUAL_VISIBILITY_H

#include "qa/application.h"

typedef struct qa_application_visual_visibility qa_application_visual_visibility;

/* One view borrows the completed Source. Caller-owned, max_align_t-aligned
 * storage expires with that view; it retains no cross-frame PVS cache. */
size_t qa_application_visual_visibility_bytes(const qa_application *);
bool qa_application_visual_visibility_prepare(qa_application *, qa_actor_id recipient,
    qa_vec3 pvs_origin, bool no_vis, void *storage, size_t capacity,
    qa_application_visual_visibility **, qa_error *);
bool qa_application_visual_visibility_actor(const qa_application_visual_visibility *,
    qa_actor_id, const qa_application_visual_view *appearance, bool *, qa_error *);

#endif
