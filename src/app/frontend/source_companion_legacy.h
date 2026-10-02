#ifndef QA_FRONTEND_SOURCE_COMPANION_LEGACY_H
#define QA_FRONTEND_SOURCE_COMPANION_LEGACY_H
#include "source_companion.h"
typedef struct frontend_source_companion_legacy frontend_source_companion_legacy;
bool frontend_source_companion_legacy_prepare(qa_frontend *,uint32_t physical,qa_actor_id,
    qa_scene_world *,qa_scene_family,qa_scene_world_input *,frontend_source_companion_legacy **,qa_error *);
bool frontend_source_companion_legacy_submit(frontend_source_companion_legacy *,qa_scene_frame *,qa_error *);
void frontend_source_companion_legacy_dispose(frontend_source_companion_legacy **);
#endif
