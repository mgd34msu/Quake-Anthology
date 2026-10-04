#ifndef QA_FRONTEND_COMPONENT_SCENE_SAVE_H
#define QA_FRONTEND_COMPONENT_SCENE_SAVE_H
#include "component_scene.h"
#include "q3_inventory.h"
typedef struct frontend_component_scene_restore_set frontend_component_scene_restore_set;
typedef struct frontend_component_scene_save_refs {
    qa_application_content_graph *content;
    frontend_scene_namespace *scene;
    const qa_scene_frame_checkpoint_refs *frame;
    const qa_audio_checkpoint_refs *audio;
    frontend_q3_inventory *q3;
} frontend_component_scene_save_refs;
bool frontend_component_scenes_capture_frames(qa_frontend *,frontend_scene_namespace *,qa_error *);
bool frontend_component_scenes_bind_frames(qa_frontend *,frontend_scene_namespace *,qa_error *);
bool frontend_component_scenes_checkpoint(qa_frontend *,const frontend_component_scene_save_refs *,qa_buffer *,qa_error *);
/* Decode every row before claiming graph views. The app and remote scene
 * constructors consume these genuine prefixes before shared dictionaries. */
bool frontend_component_scenes_prepare_restored(qa_frontend *,qa_application_content_graph *,qa_bytes,
    frontend_component_scene_restore_set **,qa_error *);
bool frontend_component_scene_restore_set_order(frontend_component_scene_restore_set *,qa_error *);
bool frontend_component_scenes_restore_continuation(frontend_component_scene_restore_set *,
    const frontend_component_scene_save_refs *,qa_error *);
bool frontend_component_scene_save_current(const qa_frontend *,const frontend_component_scene_restore_set *,
    const void *actual_owner,uint64_t frontend_identity,qa_error *);
void frontend_component_scene_restore_set_destroy(frontend_component_scene_restore_set *);
#endif
