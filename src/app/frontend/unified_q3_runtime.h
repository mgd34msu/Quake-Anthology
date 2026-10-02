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
    void *context;
    bool (*current)(void *, const frontend_unified_q3_runtime_options *);
    bool (*frame_settings)(void *, const q3n_compiled_frame *, bool loading,
        uint32_t stereo, q3n_native_frame_options *, qa_error *);
    bool (*trace_number)(void *, const q3n_compiled_frame *, const qa_trace_result *, int32_t *, qa_error *);
    bool (*command_values)(void *, int32_t weapon, float sensitivity, qa_error *);
    /* Actual predictor continuation receives the cursor changed by BG's
     * private PPS projection. No raw Source or transport PS is written. */
    bool (*prediction_cursor)(void *, const q3n_compiled_frame *, int32_t before,
        int32_t after, qa_error *);
    bool (*timescale)(void *, int32_t frame_milliseconds, qa_error *);
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
bool frontend_unified_q3_runtime_destroy(frontend_unified_q3_runtime **, qa_error *);
bool frontend_unified_q3_runtime_owners_read(const frontend_unified_q3_runtime *,
    frontend_unified_q3_runtime_owners *, qa_error *);
bool frontend_unified_q3_runtime_initialize(frontend_unified_q3_runtime *, qa_error *);
bool frontend_unified_q3_runtime_prepare(frontend_unified_q3_runtime *, uint32_t stereo, qa_error *);
bool frontend_unified_q3_runtime_process(frontend_unified_q3_runtime *, int32_t presentation_time,
    bool *active, qa_error *);
bool frontend_unified_q3_runtime_prediction(frontend_unified_q3_runtime *, const qa_q3_player *,
    uint64_t command_receipt, qa_vec3 correction, int32_t correction_time, bool hyperspace, qa_error *);
bool frontend_unified_q3_runtime_draw(frontend_unified_q3_runtime *, bool *rendered, qa_error *);
bool frontend_unified_q3_runtime_frame_end(frontend_unified_q3_runtime *, bool completed, qa_error *);
bool frontend_unified_q3_runtime_rebind_prepare(frontend_unified_q3_runtime *,
    const frontend_unified_q3_client_frame *, qa_error *);
bool frontend_unified_q3_runtime_rebind_ready(const frontend_unified_q3_runtime *,
    const frontend_unified_q3_client_frame *);
void frontend_unified_q3_runtime_rebind_commit(frontend_unified_q3_runtime *, const frontend_unified_q3_client_frame *);
void frontend_unified_q3_runtime_rebind_abort(frontend_unified_q3_runtime *, const frontend_unified_q3_client_frame *);
/* Asset dictionaries and actual client/cache parents import first; this
 * continuation does not replay CG_Init, reached commands or movie opens. */
bool frontend_unified_q3_runtime_checkpoint(const frontend_unified_q3_runtime *, qa_buffer *, qa_error *);
bool frontend_unified_q3_runtime_restore(frontend_unified_q3_runtime *, qa_bytes, qa_error *);

#endif
