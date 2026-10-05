#ifndef QA_FRONTEND_UNIFIED_Q3_SNAPSHOTS_H
#define QA_FRONTEND_UNIFIED_Q3_SNAPSHOTS_H

#include "unified_q3_client.h"
#include "../../presentation/q3_native/compiled_frame.h"
#include "../../presentation/q3_native/entity.h"

typedef struct frontend_unified_q3_snapshots frontend_unified_q3_snapshots;
typedef enum frontend_unified_q3_prediction_outcome {
    FRONTEND_UNIFIED_Q3_INTERPOLATED, FRONTEND_UNIFIED_Q3_EXHAUSTED,
    FRONTEND_UNIFIED_Q3_UNMOVED, FRONTEND_UNIFIED_Q3_MOVED
} frontend_unified_q3_prediction_outcome;
typedef struct frontend_unified_q3_prediction_receipt {
    uint64_t baseline_revision;
    int64_t command_receipt; /* -1 is the real unacknowledged constructor baseline. */
    frontend_unified_q3_prediction_outcome outcome;
    bool teleport_consumed;
} frontend_unified_q3_prediction_receipt;
typedef struct frontend_unified_q3_snapshots_options {
    frontend_unified_q3_client *client;
    void *context;
    bool (*reached)(void *, const q3n_compiled_frame *, const frontend_unified_q3_command *, qa_error *);
    bool (*respawn)(void *, const q3n_compiled_frame *, qa_error *);
    bool (*reset_player)(void *, const q3n_compiled_frame *, const q3n_compiled_entity *, qa_error *);
    bool (*event)(void *, const q3n_compiled_frame *, const q3n_compiled_entity *,
        const qa_q3_entity *, qa_vec3, qa_error *);
    bool (*transition_player)(void *, const q3n_compiled_frame *, const qa_q3_player *, const qa_q3_player *, qa_error *);
    bool (*transition_teleport)(void *, const q3n_compiled_frame *, bool *, qa_error *);
    bool (*prediction_finished)(void *, const q3n_compiled_frame *, qa_error *);
    bool (*lagometer)(void *, const qa_q3_snapshot *, qa_error *);
    bool (*warning)(void *, const char *, qa_error *);
    /* Actual retained collision owner supplies the full actor/physical number
     * witness. The cache never guesses an actor number from a registry slot. */
    bool (*trace_number)(void *, const q3n_compiled_frame *, const qa_trace_result *, int32_t *, qa_error *);
    /* Pure actual runtime reason: loading text or the real awaiting-snapshot
     * branch. A caller cannot relabel a completed frame into those stages. */
    bool (*draw_reason)(void *, q3n_compiled_stage);
} frontend_unified_q3_snapshots_options;
bool frontend_unified_q3_snapshots_create(const frontend_unified_q3_snapshots_options *,
    frontend_unified_q3_snapshots **, qa_error *);
bool frontend_unified_q3_snapshots_create_video(const frontend_unified_q3_snapshots_options *,
    const frontend_unified_q3_client_video *, frontend_unified_q3_snapshots **, qa_error *);
bool frontend_unified_q3_snapshots_destroy(frontend_unified_q3_snapshots **, qa_error *);
bool frontend_unified_q3_snapshots_idle(const frontend_unified_q3_snapshots *);
/* Runs real child Init in its lexical scope. CG's private PlayerStateRecord
 * already exists; snapshots are admitted after that constructor returns. */
bool frontend_unified_q3_snapshots_initialize(frontend_unified_q3_snapshots *,
    bool (*)(void *, const q3n_compiled_frame *, qa_error *), void *, qa_error *);
bool frontend_unified_q3_snapshots_process(frontend_unified_q3_snapshots *, int32_t presentation_time,
    bool no_predict, bool synchronous_clients, qa_error *);
bool frontend_unified_q3_snapshots_read(const frontend_unified_q3_snapshots *, q3n_compiled_frame *, qa_error *);
bool frontend_unified_q3_snapshots_has_snapshot(const frontend_unified_q3_snapshots *, bool *, qa_error *);
bool frontend_unified_q3_snapshots_previous_command_time(const frontend_unified_q3_snapshots *, int32_t *, qa_error *);
bool frontend_unified_q3_snapshots_scene_player(frontend_unified_q3_snapshots *, qa_error *);
bool frontend_unified_q3_snapshots_entered_draw(frontend_unified_q3_snapshots *,q3n_compiled_stage,int32_t presentation_time,
    bool (*)(void *,const q3n_compiled_frame *,qa_error *),void *,qa_error *);
bool frontend_unified_q3_snapshots_console(frontend_unified_q3_snapshots *,
    bool (*)(void *,const q3n_compiled_frame *,qa_error *),void *,qa_error *);
q3n_entity *frontend_unified_q3_snapshots_storage(frontend_unified_q3_snapshots *);
/* Publishes the actual predictor's merged Q3 PS copy. A foreign movement owner
 * must supply its authentic Q3 adapter; this API performs no family cast. */
bool frontend_unified_q3_snapshots_prediction(frontend_unified_q3_snapshots *, const qa_q3_player *,
    const frontend_unified_q3_prediction_receipt *, qa_vec3 correction, int32_t correction_time,
    bool hyperspace, qa_error *);


#endif
