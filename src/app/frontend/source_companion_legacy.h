#ifndef QA_FRONTEND_SOURCE_COMPANION_LEGACY_H
#define QA_FRONTEND_SOURCE_COMPANION_LEGACY_H
#include "source_companion.h"
typedef struct frontend_source_companion_legacy {
    qa_frontend *frontend;
    qa_actor_id actor;
    qa_scene_world *world;
    frontend_source_companion_view capture;
    qa_q3_presentation *presentation;
    qa_application_q3_weapon_models models;
    bool registered;
    qa_q3_scene_options options;
    qa_scene_light *lights,*projected;
} frontend_source_companion_legacy;
bool frontend_source_companion_legacy_prepare(qa_frontend *,uint32_t physical,qa_actor_id,
    qa_scene_world *,qa_game_family,qa_scene_world_input *,frontend_source_companion_legacy *,qa_error *);
bool frontend_source_companion_legacy_submit(frontend_source_companion_legacy *,qa_scene_frame *,qa_error *);
#endif
