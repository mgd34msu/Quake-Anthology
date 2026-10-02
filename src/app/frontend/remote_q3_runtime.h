#ifndef QA_FRONTEND_REMOTE_Q3_RUNTIME_H
#define QA_FRONTEND_REMOTE_Q3_RUNTIME_H

#include "remote_q3_frame.h"
#include "../../presentation/q3_native/native.h"
#include "../../presentation/q3_native/mission_hud.h"

typedef struct frontend_remote_q3_runtime frontend_remote_q3_runtime;
typedef struct frontend_remote_q3_commands frontend_remote_q3_commands;
typedef struct frontend_remote_q3_runtime_owners {
    qa_q3_presentation *presentation;
    q3n_media *media;
    q3n_clients *clients;
    q3n_events *events;
    q3n_weapons *weapons;
    q3n_view *view;
    q3n_player_state *player_state;
    q3n_hud *hud;
    q3n_server_commands *commands;
    q3n_particles *particles;
    q3n_mission_hud *mission;
} frontend_remote_q3_runtime_owners;

/* The row retains this structural child before any source callback. Failed
 * construction leaves the admitted child attached for checked retirement. */
bool frontend_remote_q3_runtime_create(frontend_remote_q3 *, frontend_remote_q3_runtime **, qa_error *);
bool frontend_remote_q3_runtime_create_restored(frontend_remote_q3 *, frontend_remote_q3_runtime **, qa_error *);
bool frontend_remote_q3_runtime_restore_candidate_ready(const frontend_remote_q3_runtime *, qa_error *);
bool frontend_remote_q3_runtime_prepare_import(frontend_remote_q3_runtime *, qa_error *);
bool frontend_remote_q3_runtime_checkpoint(const frontend_remote_q3_runtime *, qa_buffer *, qa_error *);
bool frontend_remote_q3_runtime_restore(frontend_remote_q3_runtime *, qa_bytes, qa_error *);
/* Late bind the saved soundtrack origin after actual player/engine import.
 * Sources finish verifies that every saved explicit origin found its caller. */
bool frontend_remote_q3_runtime_music_restore_bind(frontend_remote_q3_runtime *, qa_error *);
frontend_remote_q3 *frontend_remote_q3_runtime_parent(const frontend_remote_q3_runtime *);
bool frontend_remote_q3_runtime_idle(const frontend_remote_q3_runtime *);
/* Actual frontend capture holds the registry while returned children serialize.
 * This grants no ordinary callback, draw, or retirement admission. */
bool frontend_remote_q3_runtime_capture_current(const frontend_remote_q3_runtime *);
/* Returned CG_Init completion belongs to both retained runtime and frame. */
bool frontend_remote_q3_runtime_initialized_current(const frontend_remote_q3_runtime *);
/* A prepared draw retains its actual pre-draw oldTime through later rendering
 * callbacks. Outside that bracket this reads the returned oldTime continuation. */
bool frontend_remote_q3_runtime_previous_time_read(const frontend_remote_q3_runtime *,
    const q3n_remote_source_view *, int32_t *, qa_error *);
/* The decoded frame cache and QRRT continuation precede predictor restoration
 * and ordinary frame binding in the actual staged graph import. */
bool frontend_remote_q3_runtime_previous_time_import_read(const frontend_remote_q3_runtime *,
    const frontend_remote_q3_frame_import_view *, int32_t *, qa_error *);
bool frontend_remote_q3_runtime_retired(const frontend_remote_q3_runtime *);
bool frontend_remote_q3_runtime_destroy(frontend_remote_q3_runtime **, qa_error *);
bool frontend_remote_q3_runtime_callbacks_read(frontend_remote_q3_runtime *,
    frontend_remote_q3_frame_callbacks *, qa_error *);
/* Borrow constructor callbacks before the restored asset dictionary enters
 * capture. Importing private continuation neither invokes nor replaces them. */
bool frontend_remote_q3_runtime_callbacks_read_restored(frontend_remote_q3_runtime *,
    frontend_remote_q3_frame_callbacks *, qa_error *);
bool frontend_remote_q3_runtime_bind_frames(frontend_remote_q3_runtime *, frontend_remote_q3_frame *, qa_error *);
bool frontend_remote_q3_runtime_unbind_frames(frontend_remote_q3_runtime *, frontend_remote_q3_frame *, qa_error *);
frontend_remote_q3_frame *frontend_remote_q3_runtime_frames(const frontend_remote_q3_runtime *);
bool frontend_remote_q3_runtime_owners_read(const frontend_remote_q3_runtime *,
    frontend_remote_q3_runtime_owners *, qa_error *);
/* Command projection borrows an entered lexical frame. It neither replays
 * prediction nor changes the last camera and frame-time continuations. */
bool frontend_remote_q3_runtime_command_frame(frontend_remote_q3_runtime *,
    const q3n_remote_frame *, q3n_frame *, qa_error *);
bool frontend_remote_q3_runtime_command_call(frontend_remote_q3_runtime *, const q3n_remote_frame *,
    void *, bool (*execute)(void *, const q3n_frame *, qa_error *), qa_error *);
bool frontend_remote_q3_runtime_key_event(frontend_remote_q3_runtime *, const q3n_remote_frame *,
    int32_t key, bool down, qa_error *);
bool frontend_remote_q3_runtime_mouse_event(frontend_remote_q3_runtime *, const q3n_remote_frame *,
    int32_t dx, int32_t dy, qa_error *);
bool frontend_remote_q3_runtime_event_handling(frontend_remote_q3_runtime *, const q3n_remote_frame *,
    int32_t type, qa_error *);
frontend_remote_q3_commands *frontend_remote_q3_runtime_console(const frontend_remote_q3_runtime *);
bool frontend_remote_q3_runtime_load_deferred(frontend_remote_q3_runtime *, const q3n_frame *, qa_error *);
bool frontend_remote_q3_runtime_teleport_take(frontend_remote_q3_runtime *, bool *, qa_error *);
bool frontend_remote_q3_runtime_listener(const frontend_remote_q3_runtime *, qa_audio_listener *, bool *, qa_error *);
/* One physical draw brackets snapshot callbacks and predictor replay. Authored
 * information precedes scene clearing; the outer owner always ends the bracket. */
bool frontend_remote_q3_runtime_prepare(frontend_remote_q3_runtime *, uint32_t stereo, qa_error *);
bool frontend_remote_q3_runtime_information_read(const frontend_remote_q3_runtime *,
    const q3n_remote_source_view *, const char **text, qa_error *);
bool frontend_remote_q3_runtime_information_current(const frontend_remote_q3_runtime *,
    const q3n_remote_source_view *, const char *text);
bool frontend_remote_q3_runtime_before_prediction(frontend_remote_q3_runtime *, bool *active, qa_error *);
bool frontend_remote_q3_runtime_frame_end(frontend_remote_q3_runtime *, bool completed, qa_error *);
bool frontend_remote_q3_runtime_initialize(void *, const q3n_remote_frame *, qa_error *);
bool frontend_remote_q3_runtime_draw(void *, const q3n_remote_frame *, qa_error *);
bool frontend_remote_q3_runtime_waiting_draw(void *, const q3n_remote_frame *, qa_error *);
bool frontend_remote_q3_runtime_information_draw(void *, const q3n_remote_frame *, qa_error *);

#endif
