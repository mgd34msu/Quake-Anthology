#ifndef QA_FRONTEND_UNIFIED_Q3_RUNTIME_FACTORY_H
#define QA_FRONTEND_UNIFIED_Q3_RUNTIME_FACTORY_H
#include "unified_q3_runtime_services.h"
#include "unified_q3_commands.h"
#include "remote_unified_input.h"
#include "qa/audio_save.h"
#include "qa/q3_cinematic_handles.h"

typedef struct frontend_unified_q3_runtime_factory frontend_unified_q3_runtime_factory;
typedef struct frontend_unified_q3_runtime_factory_options {
    qa_frontend *frontend;
    frontend_remote_unified *replica;
    frontend_unified_media *media;
    frontend_unified_q3_client *client;
    /* Genuine constructor receipt. After construction the factory owns its
     * activation names and retains no FRAME row spans; current() qualifies
     * the actual CLIENT/source owner, not this initial observation. */
    frontend_unified_q3_source_view source;
    bool (*input_read)(void *,frontend_remote_unified *,frontend_unified_input **,qa_error *);
    frontend_remote_unified_prediction *prediction;
    frontend_unified_events *events;
    uint64_t receiver,audio_owner;
    /* Qualified by current() against the actual WORLD/ENTITIES binding. */
    bool primary_view;
    void *audio_context;
    bool (*audio_actor)(void *,qa_actor_id,uint64_t *,qa_error *);
    void *context;
    bool (*current)(void *,const struct frontend_unified_q3_runtime_factory_options *,bool checkpoint);
    /* Pure custody of this still-owned row/client/media while checked cleanup
     * runs after Source departure. This grants no live publication authority. */
    bool (*retirement_current)(void *,const struct frontend_unified_q3_runtime_factory_options *);
    bool (*send_client)(void *,frontend_unified_q3_client *,const qa_command_context *,const char *,qa_error *);
    /* The actual composition renderer qualifies independent CHARACTER and
     * equipment output. These callbacks never infer ownership from a bank. */
    q3n_player_fx_compiled_backend composition;
    bool (*view_replacement)(void *,const q3n_frame *,const qa_q3_player *,bool *,qa_error *);
    bool (*camera_override)(void *,const q3n_frame *,qa_application_camera_view *,bool *,qa_error *);
    bool (*status_replacement)(void *,const q3n_compiled_frame *,bool *,qa_error *);
} frontend_unified_q3_runtime_factory_options;

bool frontend_unified_q3_runtime_factory_create(const frontend_unified_q3_runtime_factory_options *,
    frontend_unified_q3_runtime_factory **,qa_error *);
bool frontend_unified_q3_runtime_factory_current(const frontend_unified_q3_runtime_factory *);
bool frontend_unified_q3_runtime_factory_idle(const frontend_unified_q3_runtime_factory *);
bool frontend_unified_q3_runtime_factory_destroy(frontend_unified_q3_runtime_factory **,qa_error *);
/* A failed fresh Init closes its actual CG children before resetting only the
 * retained CLIENT constructor cache. Transport history remains owned. */
bool frontend_unified_q3_runtime_factory_constructor_abort(frontend_unified_q3_runtime_factory **,qa_error *);
frontend_unified_q3_runtime *frontend_unified_q3_runtime_factory_runtime(const frontend_unified_q3_runtime_factory *);
bool frontend_unified_q3_runtime_factory_cinematic_read(const frontend_unified_q3_runtime_factory *,
    qa_q3_cinematic_source **,qa_error *);
bool frontend_unified_q3_runtime_factory_initialize(frontend_unified_q3_runtime_factory *,qa_error *);
bool frontend_unified_q3_runtime_factory_rebind_prepare(frontend_unified_q3_runtime_factory *,
    const frontend_unified_q3_client_frame *,qa_error *);
bool frontend_unified_q3_runtime_factory_rebind_ready(const frontend_unified_q3_runtime_factory *,
    const frontend_unified_q3_client_frame *);
bool frontend_unified_q3_runtime_factory_rebind_checkpoint_ready(const frontend_unified_q3_runtime_factory *,
    const frontend_unified_q3_client_frame *);
void frontend_unified_q3_runtime_factory_rebind_commit(frontend_unified_q3_runtime_factory *,
    const frontend_unified_q3_client_frame *);
void frontend_unified_q3_runtime_factory_rebind_abort(frontend_unified_q3_runtime_factory *,
    const frontend_unified_q3_client_frame *);
bool frontend_unified_q3_runtime_factory_prepare(frontend_unified_q3_runtime_factory *,uint32_t stereo,qa_error *);
bool frontend_unified_q3_runtime_factory_process(frontend_unified_q3_runtime_factory *,int32_t time,bool *active,qa_error *);
bool frontend_unified_q3_runtime_factory_prediction_baseline(const frontend_unified_q3_runtime_factory *,
    frontend_unified_q3_runtime_prediction_baseline *,qa_error *);
bool frontend_unified_q3_runtime_factory_prediction(frontend_unified_q3_runtime_factory *,const qa_q3_player *,
    const frontend_unified_q3_prediction_receipt *,qa_vec3 correction,int32_t correction_time,bool hyperspace,qa_error *);
bool frontend_unified_q3_runtime_factory_camera_prepare(frontend_unified_q3_runtime_factory *,
    frontend_unified_q3_runtime_camera *,bool *active,qa_error *);
bool frontend_unified_q3_runtime_factory_scene_camera(frontend_unified_q3_runtime_factory *,const qa_scene_view *,qa_error *);
bool frontend_unified_q3_runtime_factory_scene_prepare(frontend_unified_q3_runtime_factory *,
    struct qa_q3_source_scene_bank *,bool *active,qa_error *);
bool frontend_unified_q3_runtime_factory_scene_lights(const frontend_unified_q3_runtime_factory *,
    const qa_scene_light **,size_t *,qa_error *);
bool frontend_unified_q3_runtime_factory_scene_actor(const frontend_unified_q3_runtime_factory *,qa_actor_id,
    frontend_unified_q3_runtime_scene_owner *,bool *owned,qa_error *);
bool frontend_unified_q3_runtime_factory_scene_view_weapon(const frontend_unified_q3_runtime_factory *,qa_actor_id,
    frontend_unified_q3_runtime_scene_owner *,bool *owned,qa_error *);
bool frontend_unified_q3_runtime_factory_scene_submit(frontend_unified_q3_runtime_factory *,
    const qa_scene_world_input *,qa_scene_frame *,qa_error *);
bool frontend_unified_q3_runtime_factory_hud(frontend_unified_q3_runtime_factory *,bool *rendered,qa_error *);
bool frontend_unified_q3_runtime_factory_frame_end(frontend_unified_q3_runtime_factory *,bool completed,qa_error *);
bool frontend_unified_q3_runtime_factory_listener(const frontend_unified_q3_runtime_factory *,qa_audio_listener *,bool *present,qa_error *);

#endif
