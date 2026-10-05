#ifndef QA_FRONTEND_UNIFIED_Q3_RUNTIME_H
#define QA_FRONTEND_UNIFIED_Q3_RUNTIME_H

#include "unified_q3_snapshots.h"
#include "../../presentation/q3_native/native.h"
#include "../../presentation/q3_native/mission_hud.h"
#include "../../presentation/q3_native/loading.h"

typedef struct frontend_unified_q3_runtime frontend_unified_q3_runtime;
typedef struct frontend_unified_q3_runtime_options {
    qa_frontend *frontend;
    frontend_remote_unified *replica;
    frontend_unified_q3_client *client;
    q3n_media *media;
    q3n_clients *clients;
    bool scene_only;
    /* These options contain genuine factory-owned collision, audio, movie,
     * input and compositor services. Their contexts outlive this CG child. */
    qa_q3_presentation_options presentation;
    q3n_weapon_options weapons;
    q3n_event_options events;
    q3n_view_options view;
    q3n_player_state_options player_state;
    q3n_hud_options hud;
    q3n_server_command_options commands;
    q3n_loading_options loading;
    q3n_mission_hud_options mission;
    q3n_player_fx_compiled_backend player_fx;
    void *context;
    bool (*current)(void *, const struct frontend_unified_q3_runtime_options *);
    bool (*frame_settings)(void *, const q3n_compiled_frame *, bool loading,
        uint32_t stereo, q3n_native_frame_options *, qa_error *);
    bool (*trace_number)(void *, const q3n_compiled_frame *, const qa_trace_result *, int32_t *, qa_error *);
    bool (*command_values)(void *, int32_t weapon, float sensitivity, qa_error *);
    bool (*timescale)(void *, int32_t frame_milliseconds, qa_error *);
    bool (*preferences)(void *, qa_ui_preferences *, qa_error *);
    bool (*backend_frame)(void *, qa_q3_presentation *, qa_error *);
    bool (*backend_checkpoint)(void *, const qa_q3_presentation *, qa_buffer *, qa_error *);
    bool (*backend_restore)(void *, qa_q3_presentation *, qa_bytes, qa_error *);
    /* Retire genuine external CG command registration after Shutdown, before
     * the retained CLIENT enters fresh video initialization. */
    bool (*video_shutdown)(void *, qa_error *);
} frontend_unified_q3_runtime_options;
typedef struct frontend_unified_q3_runtime_owners {
    qa_q3_presentation *presentation;
    q3n_media *media;
    q3n_clients *clients;
    q3n_weapons *weapons;
    q3n_events *events;
    q3n_particles *particles;
    q3n_view *view;
    q3n_player_state *player_state;
    q3n_hud *hud;
    q3n_server_commands *commands;
    q3n_loading *loading;
    q3n_mission_hud *mission;
    frontend_unified_q3_snapshots *snapshots;
} frontend_unified_q3_runtime_owners;

/* Failure keeps the partially constructed owner in *out for checked cleanup. */
bool frontend_unified_q3_runtime_create(const frontend_unified_q3_runtime_options *,
    frontend_unified_q3_runtime **, qa_error *);
bool frontend_unified_q3_runtime_create_restored(const frontend_unified_q3_runtime_options *,
    frontend_unified_q3_runtime **, qa_error *);
bool frontend_unified_q3_runtime_current(const frontend_unified_q3_runtime *);
bool frontend_unified_q3_runtime_idle(const frontend_unified_q3_runtime *);
/* Borrow only during this runtime's genuine entered CG callback. The caller
 * must qualify the frame before observing its cache rows. */
const q3n_compiled_frame *frontend_unified_q3_runtime_entered(const frontend_unified_q3_runtime *);
bool frontend_unified_q3_runtime_destroy(frontend_unified_q3_runtime **, qa_error *);
bool frontend_unified_q3_runtime_owners_read(const frontend_unified_q3_runtime *,
    frontend_unified_q3_runtime_owners *, qa_error *);
bool frontend_unified_q3_runtime_initialize(frontend_unified_q3_runtime *, qa_error *);
bool frontend_unified_q3_runtime_initialize_video(frontend_unified_q3_runtime *, qa_error *);
bool frontend_unified_q3_runtime_prepare(frontend_unified_q3_runtime *, uint32_t stereo, qa_error *);
bool frontend_unified_q3_runtime_process(frontend_unified_q3_runtime *, int32_t presentation_time,
    bool *active, qa_error *);
typedef struct frontend_unified_q3_runtime_prediction_baseline {
    qa_q3_player player;
    qa_actor_id viewer;
    int32_t message,previous_command_time;
    bool this_frame_teleport;
    qa_vec3 correction;
    int32_t correction_time;
    bool hyperspace;
} frontend_unified_q3_runtime_prediction_baseline;
bool frontend_unified_q3_runtime_prediction_baseline_read(const frontend_unified_q3_runtime *,
    frontend_unified_q3_runtime_prediction_baseline *,qa_error *);
bool frontend_unified_q3_runtime_prediction(frontend_unified_q3_runtime *, const qa_q3_player *,
    const frontend_unified_q3_prediction_receipt *, qa_vec3 correction, int32_t correction_time,
    bool hyperspace, qa_error *);
typedef struct frontend_unified_q3_runtime_camera {
    qa_q3_refdef refdef;
    qa_scene_rect viewport;
    float near_clip,far_clip;
} frontend_unified_q3_runtime_camera;
/* Calculate genuine CG camera state once, before the common view is entered.
 * The parent applies it only under its actual primary Source binding. */
bool frontend_unified_q3_runtime_camera_prepare(frontend_unified_q3_runtime *,
    frontend_unified_q3_runtime_camera *,bool *active,qa_error *);
bool frontend_unified_q3_runtime_scene_camera(frontend_unified_q3_runtime *,const qa_scene_view *,qa_error *);
bool frontend_unified_q3_runtime_draw(frontend_unified_q3_runtime *, bool *rendered, qa_error *);
/* Gather once after the actual CHARACTER sampler. The retained bank receipt
 * then submits only CG geometry into the caller's unfinished world view. */
bool frontend_unified_q3_runtime_scene_prepare(frontend_unified_q3_runtime *,
    struct qa_q3_source_scene_bank *,bool *active,qa_error *);
bool frontend_unified_q3_runtime_scene_lights(const frontend_unified_q3_runtime *,
    const qa_scene_light **,size_t *,qa_error *);
typedef struct frontend_unified_q3_runtime_scene_owner {
    qa_actor_owner provider;
    const char *instance;
    uint64_t publication,map_revision;
    qa_actor_id actor;
    uint32_t number;
    int32_t message;
} frontend_unified_q3_runtime_scene_owner;
/* The gathered scene is admitted in the shared bank and this actor belongs
 * to its processed snapshot's physical bindings. PVS-hidden actors and hidden
 * packet outputs retain Source ownership through that exact observation. */
bool frontend_unified_q3_runtime_scene_actor(const frontend_unified_q3_runtime *,qa_actor_id,
    frontend_unified_q3_runtime_scene_owner *,bool *owned,qa_error *);
/* Successful native view-weapon handling includes its genuine hidden outcomes.
 * Independent registered equipment replacement retains its own ownership. */
bool frontend_unified_q3_runtime_scene_view_weapon(const frontend_unified_q3_runtime *,qa_actor_id,
    frontend_unified_q3_runtime_scene_owner *,bool *owned,qa_error *);
bool frontend_unified_q3_runtime_scene_submit(frontend_unified_q3_runtime *,
    const qa_scene_world_input *,qa_scene_frame *,qa_error *);
bool frontend_unified_q3_runtime_hud(frontend_unified_q3_runtime *,bool *rendered,qa_error *);
bool frontend_unified_q3_runtime_frame_end(frontend_unified_q3_runtime *, bool completed, qa_error *);
bool frontend_unified_q3_runtime_command_call(frontend_unified_q3_runtime *,const q3n_compiled_frame *,
    void *,bool (*)(void *,const q3n_frame *,const frontend_unified_q3_runtime_owners *,qa_error *),qa_error *);
bool frontend_unified_q3_runtime_load_deferred(frontend_unified_q3_runtime *,const q3n_frame *,qa_error *);
bool frontend_unified_q3_runtime_key_event(frontend_unified_q3_runtime *,int32_t key,bool down,qa_error *);
bool frontend_unified_q3_runtime_mouse_event(frontend_unified_q3_runtime *,int32_t dx,int32_t dy,qa_error *);
bool frontend_unified_q3_runtime_event_handling(frontend_unified_q3_runtime *,int32_t type,qa_error *);
bool frontend_unified_q3_runtime_rebind_prepare(frontend_unified_q3_runtime *,
    const frontend_unified_q3_client_frame *, qa_error *);
bool frontend_unified_q3_runtime_rebind_ready(const frontend_unified_q3_runtime *,
    const frontend_unified_q3_client_frame *);
const frontend_unified_q3_client_frame *frontend_unified_q3_runtime_rebind_frame(const frontend_unified_q3_runtime *);
bool frontend_unified_q3_runtime_checkpoint_current(const frontend_unified_q3_runtime *);
bool frontend_unified_q3_runtime_rebind_checkpoint_ready(const frontend_unified_q3_runtime *,
    const frontend_unified_q3_client_frame *);
bool frontend_unified_q3_runtime_rebind_restore(frontend_unified_q3_runtime *,
    const frontend_unified_q3_client_frame *,qa_error *);
void frontend_unified_q3_runtime_rebind_commit(frontend_unified_q3_runtime *, const frontend_unified_q3_client_frame *);
void frontend_unified_q3_runtime_rebind_abort(frontend_unified_q3_runtime *, const frontend_unified_q3_client_frame *);

#endif
