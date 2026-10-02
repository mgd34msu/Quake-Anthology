#ifndef QA_FRONTEND_REMOTE_UNIFIED_Q1_H
#define QA_FRONTEND_REMOTE_UNIFIED_Q1_H
#include "remote_unified_media.h"
#include "qa/source_save.h"
#include "qa/audio_save.h"
#include "qa/hud.h"
#include "remote_unified_events.h"

typedef struct frontend_unified_q1 frontend_unified_q1;
typedef struct frontend_unified_q1_options {
    uint64_t audio_owner;
    void *context;
    bool (*audio_actor)(void *,qa_actor_id,uint64_t *,qa_error *);
    bool (*action_validate)(void *,const qa_unified_document *,qa_json_id,qa_error *);
    bool (*action)(void *,const qa_unified_document *,qa_json_id,qa_error *);
} frontend_unified_q1_options;
typedef struct frontend_unified_q1_refs {
    const qa_audio_checkpoint_refs *audio;
    qa_hud_checkpoint_refs hud;
    void *context;
    bool (*identity_encode)(void *,bool static_audio,uint64_t,uint64_t *,qa_error *);
    bool (*identity_decode)(void *,bool static_audio,uint64_t,uint64_t *,qa_error *);
    bool (*asset_encode)(void *,const qa_audio_asset *,uint64_t *,qa_error *);
    bool (*asset_decode)(void *,uint64_t,const qa_audio_asset **,qa_error *);
} frontend_unified_q1_refs;
bool frontend_unified_q1_create(qa_frontend *,frontend_remote_unified *,frontend_unified_media *,const frontend_unified_q1_options *,frontend_unified_q1 **,qa_error *);
bool frontend_unified_q1_events(frontend_unified_q1 *,frontend_unified_events *,qa_error *);
bool frontend_unified_q1_owner_validate(frontend_unified_q1 *,const qa_unified_document *,qa_json_id,qa_error *);
bool frontend_unified_q1_owner_retire(frontend_unified_q1 *,const qa_unified_document *,qa_json_id,qa_error *);
bool frontend_unified_q1_validate(frontend_unified_q1 *,bool,const qa_unified_document *,qa_json_id,qa_error *);
bool frontend_unified_q1_presentation(frontend_unified_q1 *,const qa_unified_document *,qa_json_id,bool *,qa_error *);
bool frontend_unified_q1_sound_presentation(frontend_unified_q1 *,const qa_unified_document *,qa_json_id,bool simulation_owned,qa_error *);
bool frontend_unified_q1_frame_prepare(frontend_unified_q1 *,const qa_unified_document *,qa_error *);
bool frontend_unified_q1_frame_ready(frontend_unified_q1 *,const qa_unified_document *,qa_error *);
void frontend_unified_q1_frame_commit(frontend_unified_q1 *);
void frontend_unified_q1_frame_abort(frontend_unified_q1 *);
bool frontend_unified_q1_world(frontend_unified_q1 *,const qa_scene_view *,const qa_scene_world_input *,qa_scene_frame *,qa_error *);
bool frontend_unified_q1_world_input(frontend_unified_q1 *,qa_scene_world_input *,qa_error *);
bool frontend_unified_q1_audio_detach(frontend_unified_q1 *,qa_error *);
bool frontend_unified_q1_hud(frontend_unified_q1 *,qa_ui *,qa_scene_rect,qa_scene_frame *,qa_error *);
bool frontend_unified_q1_model(frontend_unified_q1 *,qa_actor_id,const char *,const char *,qa_scene_model_input *,qa_error *);
bool frontend_unified_q1_current(const frontend_unified_q1 *);
bool frontend_unified_q1_idle(const frontend_unified_q1 *);
bool frontend_unified_q1_destroy(frontend_unified_q1 **,qa_error *);
size_t frontend_unified_q1_group_count(const frontend_unified_q1 *);
bool frontend_unified_q1_music_at(const frontend_unified_q1 *,size_t,uint64_t *,qa_audio_music **);
const qa_scene_image *frontend_unified_q1_particle_image(const frontend_unified_q1 *,size_t);
size_t frontend_unified_q1_light_count(const frontend_unified_q1 *,size_t);
bool frontend_unified_q1_light_at(const frontend_unified_q1 *,size_t,size_t,uint64_t *);
size_t frontend_unified_q1_static_count(const frontend_unified_q1 *,size_t);
bool frontend_unified_q1_static_at(const frontend_unified_q1 *,size_t,size_t,uint64_t *,const qa_audio_asset **,qa_audio_mixer **);
bool frontend_unified_q1_checkpoint(frontend_unified_q1 *,const frontend_unified_q1_refs *,qa_buffer *,qa_error *);
bool frontend_unified_q1_restore(qa_frontend *,frontend_remote_unified *,frontend_unified_media *,const frontend_unified_q1_options *,const frontend_unified_q1_refs *,qa_bytes,frontend_unified_q1 **,qa_error *);
bool frontend_unified_q1_restore_finish(frontend_unified_q1 *,qa_error *);
#endif
