#ifndef QA_FRONTEND_REMOTE_UNIFIED_Q1_H
#define QA_FRONTEND_REMOTE_UNIFIED_Q1_H
#include "remote_unified_media.h"
#include "remote_unified_render.h"
#include "qa/hud.h"
#include "remote_unified_events.h"
#include "qa/unified_frame_events.h"
#include "qa/unified_frame_metadata.h"

typedef struct frontend_unified_q1 frontend_unified_q1;
const qa_unified_q1_world_state *frontend_unified_q1_world_read(const frontend_remote_unified *);
typedef struct frontend_unified_q1_options {
    uint64_t audio_owner;
    void *context;
    bool (*audio_actor)(void *,qa_actor_id,uint64_t *,qa_error *);
} frontend_unified_q1_options;
bool frontend_unified_q1_create(qa_frontend *,frontend_remote_unified *,frontend_unified_media *,const frontend_unified_q1_options *,frontend_unified_q1 **,qa_error *);
bool frontend_unified_q1_events(frontend_unified_q1 *,frontend_unified_events *,qa_error *);
bool frontend_unified_q1_owner_validate(frontend_unified_q1 *,const qa_unified_presentation_event *,qa_error *);
bool frontend_unified_q1_owner_retire(frontend_unified_q1 *,const qa_unified_presentation_event *,qa_error *);
bool frontend_unified_q1_presentation_validate(frontend_unified_q1 *,const qa_unified_presentation_event *,qa_error *);
bool frontend_unified_q1_simulation_validate(frontend_unified_q1 *,const qa_unified_simulation_event *,qa_error *);
bool frontend_unified_q1_presentation(frontend_unified_q1 *,const qa_unified_presentation_event *,bool *,qa_error *);
bool frontend_unified_q1_sound_presentation(frontend_unified_q1 *,const qa_unified_presentation_event *,bool simulation_owned,qa_error *);
bool frontend_unified_q1_frame_prepare(frontend_unified_q1 *,const qa_unified_document *,qa_error *);
bool frontend_unified_q1_frame_ready(frontend_unified_q1 *,const qa_unified_document *,qa_error *);
void frontend_unified_q1_frame_commit(frontend_unified_q1 *);
void frontend_unified_q1_frame_abort(frontend_unified_q1 *);
bool frontend_unified_q1_world_models(frontend_unified_q1 *,const qa_scene_world_input *,qa_scene_frame *,qa_error *);
bool frontend_unified_q1_world_particles(frontend_unified_q1 *,const qa_scene_world_input *,qa_scene_frame *,qa_error *);
bool frontend_unified_q1_world_dlights(frontend_unified_q1 *,const qa_scene_world_input *,qa_scene_frame *,qa_scene_vec4 *,qa_error *);
bool frontend_unified_q1_world_blend(frontend_unified_q1 *,const qa_scene_world_input *,qa_scene_vec4,qa_scene_frame *,qa_error *);
bool frontend_unified_q1_entity_effects(frontend_unified_q1 *,const frontend_unified_render_entity_effects *,double,qa_error *);
bool frontend_unified_q1_world_input(frontend_unified_q1 *,qa_scene_world_input *,qa_error *);
bool frontend_unified_q1_audio_detach(frontend_unified_q1 *,qa_error *);
void frontend_unified_q1_hud_status(const frontend_unified_q1 *,qa_hud_q1_status *);
bool frontend_unified_q1_hud(frontend_unified_q1 *,qa_ui *,qa_scene_rect,qa_scene_frame *,qa_error *);
bool frontend_unified_q1_model(frontend_unified_q1 *,qa_actor_id,const char *,const char *,qa_scene_model_input *,qa_error *);
bool frontend_unified_q1_current(const frontend_unified_q1 *);
bool frontend_unified_q1_idle(const frontend_unified_q1 *);
bool frontend_unified_q1_destroy(frontend_unified_q1 **,qa_error *);
bool frontend_unified_q1_checkpoint_ready(const frontend_unified_q1 *);
#endif
