#ifndef QA_FRONTEND_REMOTE_SNAPSHOTS_H
#define QA_FRONTEND_REMOTE_SNAPSHOTS_H

#include "network_presentation.h"
#include "../../presentation/q3_native/entity.h"
#include "../../presentation/q3_native/remote_frame.h"

typedef struct frontend_remote_snapshots frontend_remote_snapshots;
typedef struct frontend_remote_centity {
    qa_q3_entity current, next;
    q3n_entity *presentation;
    int32_t publication_message;
    bool published, interpolate;
} frontend_remote_centity;
typedef struct frontend_remote_snapshot_settings {
    bool demo_playback, no_predict, synchronous_clients;
} frontend_remote_snapshot_settings;
typedef struct frontend_remote_snapshots_options {
    qa_frontend *frontend;
    q3n_remote_source *source;
    qa_q3_product product;
    void *context;
    /* The real source executes and adopts each receipt before this dispatch. */
    bool (*reached)(void *, const q3n_remote_command *, qa_error *);
    bool (*respawn)(void *, const q3n_remote_source_view *, qa_error *);
    bool (*reset_player)(void *, const q3n_remote_source_view *,
        frontend_remote_centity *, qa_error *);
    bool (*event)(void *, const q3n_remote_source_view *,
        frontend_remote_centity *, const qa_q3_entity *, qa_vec3 position, int32_t time, qa_error *);
    bool (*transition_player)(void *, const q3n_remote_source_view *,
        const qa_q3_player *, const qa_q3_player *, qa_error *);
    bool (*lagometer)(void *, const qa_q3_snapshot *, int32_t ping, qa_error *);
    bool (*warning)(void *, const char *, qa_error *);
} frontend_remote_snapshots_options;
typedef struct frontend_remote_snapshots_view {
    const frontend_remote_snapshots *owner;
    q3n_remote_source_view source;
    const qa_q3_snapshot *snapshot, *next_snapshot;
    int32_t time, processed_message, command_sequence;
    uint64_t revision;
    bool this_frame_teleport, next_frame_teleport;
    uint64_t callback_scope;
} frontend_remote_snapshots_view;

/* The enclosing native remote row retains the receiver, map and callbacks.
 * Construction uses the actual CG_Init tuple, before consuming its history. */
bool frontend_remote_snapshots_create(const frontend_remote_snapshots_options *,
    const q3n_remote_source_view *, frontend_remote_snapshots **, qa_error *);
bool frontend_remote_snapshots_create_video(const frontend_remote_snapshots_options *,
    const q3n_remote_source_view *,frontend_remote_snapshots **,qa_error *);
bool frontend_remote_snapshots_destroy(frontend_remote_snapshots *, qa_error *);
bool frontend_remote_snapshots_idle(const frontend_remote_snapshots *);
/* Actual contiguous native cache. The enclosing row retains this owner through
 * construction and every entered frame; borrowing storage admits no frame. */
q3n_entity *frontend_remote_snapshots_storage(frontend_remote_snapshots *);
bool frontend_remote_snapshots_process(frontend_remote_snapshots *,
    const frontend_remote_snapshot_settings *, qa_error *);
bool frontend_remote_snapshots_read(const frontend_remote_snapshots *, frontend_remote_snapshots_view *);
bool frontend_remote_snapshots_current(const frontend_remote_snapshots *, const frontend_remote_snapshots_view *);
/* The actual entered transition callback borrows the in-progress cut. This
 * scope ends when that callback returns; it never admits a completed frame. */
bool frontend_remote_snapshots_callback_read(const frontend_remote_snapshots *,
    const q3n_remote_source_view *, frontend_remote_snapshots_view *);
bool frontend_remote_snapshots_callback_current(const frontend_remote_snapshots *,
    const frontend_remote_snapshots_view *);
bool frontend_remote_snapshots_entity(const frontend_remote_snapshots *, const frontend_remote_snapshots_view *,
    uint32_t number, const frontend_remote_centity **, qa_error *);
bool frontend_remote_snapshots_callback_entity(const frontend_remote_snapshots *, const frontend_remote_snapshots_view *,
    uint32_t number, const frontend_remote_centity **, qa_error *);
/* Native PS transitions author only this CGAME-owned currentState event
 * override. Network snapshot storage and the movement predictor are absent. */
bool frontend_remote_snapshots_entity_write(frontend_remote_snapshots *, const frontend_remote_snapshots_view *,
    uint32_t number, frontend_remote_centity **, qa_error *);
bool frontend_remote_snapshots_entity_trajectory(frontend_remote_snapshots *, const frontend_remote_snapshots_view *,
    uint32_t number, int32_t current_before, int32_t next_before,
    int32_t current_after, int32_t next_after, qa_error *);
bool frontend_remote_snapshots_entity_weapon(frontend_remote_snapshots *, const frontend_remote_snapshots_view *,
    uint32_t number, int32_t before, int32_t after, qa_error *);
bool frontend_remote_snapshots_misc_time_read(const frontend_remote_snapshots *,
    const frontend_network_prediction_source *, const qa_q3_prediction_scene_entity_view *, int32_t *, qa_error *);
bool frontend_remote_snapshots_consume_teleport(frontend_remote_snapshots *, qa_error *);
/* Apply a real returned PS-transition feedback request before the next
 * prediction/view admission, after the child callback frame has ended. */
bool frontend_remote_snapshots_mark_teleport(frontend_remote_snapshots *, qa_error *);

#endif
