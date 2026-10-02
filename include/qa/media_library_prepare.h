#ifndef QA_MEDIA_LIBRARY_PREPARE_H
#define QA_MEDIA_LIBRARY_PREPARE_H
#include "qa/cinematic.h"

typedef struct qa_media_library_stage qa_media_library_stage;
bool qa_media_library_idle(const qa_media_library *);
/* The source cache stays unchanged. New resources use the actual provider
 * view and decode into a private cache bound to its prepared image bank. */
bool qa_media_library_stage_prepare(qa_media_library *, qa_scene_resource_policy *,
    qa_media_library_stage **, qa_error *);
qa_media_library *qa_media_library_stage_destination(const qa_media_library_stage *);
qa_media_library *qa_media_library_stage_source(const qa_media_library_stage *);
bool qa_media_library_stage_ready(qa_media_library_stage *, qa_error *);
bool qa_media_library_stage_ready_is(const qa_media_library_stage *);
void qa_media_library_stage_publish(qa_media_library_stage *);
bool qa_media_library_stage_finish(qa_media_library_stage **, qa_error *);
bool qa_media_library_stage_abort(qa_media_library_stage **, qa_error *);
#endif
