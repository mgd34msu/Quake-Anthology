#ifndef QA_FRONTEND_REMOTE_Q3_FRAME_H
#define QA_FRONTEND_REMOTE_Q3_FRAME_H

#include "remote_q3_services.h"
#include "remote_snapshots.h"
#include "remote_prediction.h"

typedef struct frontend_remote_q3_frame frontend_remote_q3_frame;
typedef struct frontend_remote_q3_frame_callbacks {
    void *context;
    bool (*reached)(void *, const q3n_remote_frame *, const q3n_remote_command *, qa_error *);
    bool (*respawn)(void *, const q3n_remote_frame *, qa_error *);
    bool (*reset_player)(void *, const q3n_remote_frame *, frontend_remote_centity *, qa_error *);
    bool (*event)(void *, const q3n_remote_frame *, frontend_remote_centity *,
        const qa_q3_entity *, qa_vec3, int32_t, qa_error *);
    bool (*transition_player)(void *, const q3n_remote_frame *, const qa_q3_player *,
        const qa_q3_player *, qa_error *);
    bool (*prediction_completed)(void *, const q3n_remote_frame *, frontend_remote_prediction_status, qa_error *);
    bool (*teleport_take)(void *, bool *pending, qa_error *);
    bool (*lagometer)(void *, const qa_q3_snapshot *, int32_t, qa_error *);
    bool (*warning)(void *, const char *, qa_error *);
    bool (*trace_number)(void *, const q3n_remote_frame *, const qa_trace_result *, int32_t *, qa_error *);
} frontend_remote_q3_frame_callbacks;
/* The actual decoded QRFG cache precedes predictor reconstruction. This
 * receipt borrows that blocked import owner; it admits no entered frame. */
typedef struct frontend_remote_q3_frame_import_view {
    const frontend_remote_q3_frame *owner;
    frontend_remote_snapshots_view snapshots;
    const qa_q3_player *player;
    bool has_prediction, init_finished;
} frontend_remote_q3_frame_import_view;

/* The CGAME owner retains its constructor-zero PPS and both predicted ES rows.
 * Callback frames expire before the callback returns; completed frames expire
 * before the entered draw returns. Neither scope publishes a GAME actor. */
bool frontend_remote_q3_frame_create(frontend_remote_q3 *,
    const frontend_remote_q3_frame_callbacks *, frontend_remote_q3_frame **, qa_error *);
bool frontend_remote_q3_frame_create_video(frontend_remote_q3 *,
    const frontend_remote_q3_frame_callbacks *,frontend_remote_q3_frame **,qa_error *);
bool frontend_remote_q3_frame_idle(const frontend_remote_q3_frame *);
/* Completed Init belongs to this actual retained frame owner, independently
 * of the physical service marker and constructor allocation. */
bool frontend_remote_q3_frame_initialized_current(const frontend_remote_q3_frame *);
frontend_remote_q3 *frontend_remote_q3_frame_parent(const frontend_remote_q3_frame *);
void *frontend_remote_q3_frame_callbacks_context(const frontend_remote_q3_frame *);
bool frontend_remote_q3_frame_destroy(frontend_remote_q3_frame **, qa_error *);
frontend_remote_snapshots *frontend_remote_q3_frame_snapshots(const frontend_remote_q3_frame *);
bool frontend_remote_q3_frame_initialize(frontend_remote_q3_frame *, void *,
    bool (*initialize)(void *, const q3n_remote_frame *, qa_error *), qa_error *);
bool frontend_remote_q3_frame_process(frontend_remote_q3_frame *,
    const frontend_remote_snapshot_settings *, qa_error *);
/* Replays the real predictor on every invocation, including stereo draws.
 * The source must qualify the actual completed predictor readback. */
bool frontend_remote_q3_frame_predict(frontend_remote_q3_frame *, frontend_remote_prediction *,
    bool *present, qa_error *);
bool frontend_remote_q3_frame_draw(frontend_remote_q3_frame *, void *,
    bool (*draw)(void *, const q3n_remote_frame *, qa_error *), qa_error *);
/* Commands borrow the actual cold or completed cache without replay. A
 * command failure closes only its lexical scope, preserving the CGAME owner. */
bool frontend_remote_q3_frame_command(frontend_remote_q3_frame *, void *,
    bool (*execute)(void *, const q3n_remote_frame *, qa_error *), qa_error *);
/* Loading uses genuine snapshot absence or SNAPFLAG_NOT_ACTIVE. */
bool frontend_remote_q3_frame_waiting(frontend_remote_q3_frame *, void *,
    bool (*draw)(void *, const q3n_remote_frame *, qa_error *), qa_error *);
/* Reads the genuine prepared loading reason before snapshots. Empty text
 * returns present=false; nonempty text paints through its own lexical scope. */
bool frontend_remote_q3_frame_information(frontend_remote_q3_frame *, void *,
    bool (*draw)(void *, const q3n_remote_frame *, qa_error *), bool *present, qa_error *);
/* Called immediately around the real packet BG conversion in the entered draw. */
bool frontend_remote_q3_frame_event_publish(frontend_remote_q3_frame *,
    const q3n_remote_frame *, int32_t before, qa_error *);
bool frontend_remote_q3_frame_prediction_status(const frontend_remote_q3_frame *,
    const q3n_remote_frame *, frontend_remote_prediction_status *, qa_error *);
bool frontend_remote_q3_frame_prediction_source(const frontend_remote_q3_frame *,
    const q3n_remote_frame *, frontend_remote_prediction_source *, qa_error *);
bool frontend_remote_q3_frame_network_source(const frontend_remote_q3_frame *,
    const q3n_remote_frame *, frontend_network_prediction_source *, qa_error *);
bool frontend_remote_q3_frame_checkpoint(const frontend_remote_q3_frame *, qa_buffer *, qa_error *);
bool frontend_remote_q3_frame_prepare_restored(frontend_remote_q3 *,
    const frontend_remote_q3_frame_callbacks *, qa_bytes, frontend_remote_q3_frame **, qa_error *);
bool frontend_remote_q3_frame_import_read(const frontend_remote_q3_frame *,
    frontend_remote_q3_frame_import_view *, qa_error *);
bool frontend_remote_q3_frame_import_current(const frontend_remote_q3_frame_import_view *);
bool frontend_remote_q3_frame_finish_restore(frontend_remote_q3_frame *, frontend_remote_prediction *,
    const frontend_remote_prediction_source *, qa_error *);
bool frontend_remote_q3_frame_restore(frontend_remote_q3 *,
    const frontend_remote_q3_frame_callbacks *, frontend_remote_prediction *,
    const frontend_remote_prediction_source *, qa_bytes, frontend_remote_q3_frame **, qa_error *);

#endif
