#ifndef QA_FRONTEND_UNIFIED_Q3_SNAPSHOTS_H
#define QA_FRONTEND_UNIFIED_Q3_SNAPSHOTS_H

#include "unified_q3_client.h"
#include "../../presentation/q3_native/compiled_frame.h"
#include "../../presentation/q3_native/entity.h"

typedef struct frontend_unified_q3_snapshots frontend_unified_q3_snapshots;
typedef struct frontend_unified_q3_snapshots_options {
    frontend_unified_q3_client *client;
    void *context;
    bool (*reached)(void *, const q3n_compiled_frame *, const frontend_unified_q3_command *, qa_error *);
    bool (*respawn)(void *, const q3n_compiled_frame *, qa_error *);
    bool (*reset_player)(void *, const q3n_compiled_frame *, const q3n_compiled_entity *, qa_error *);
    bool (*event)(void *, const q3n_compiled_frame *, const q3n_compiled_entity *,
        const qa_q3_entity *, qa_vec3, qa_error *);
    bool (*transition_player)(void *, const q3n_compiled_frame *, const qa_q3_player *, const qa_q3_player *, qa_error *);
    bool (*lagometer)(void *, const qa_q3_snapshot *, qa_error *);
    bool (*warning)(void *, const char *, qa_error *);
    /* Actual retained collision owner supplies the full actor/physical number
     * witness. The cache never guesses an actor number from a registry slot. */
    bool (*trace_number)(void *, const q3n_compiled_frame *, const qa_trace_result *, int32_t *, qa_error *);
} frontend_unified_q3_snapshots_options;
bool frontend_unified_q3_snapshots_create(const frontend_unified_q3_snapshots_options *,
    frontend_unified_q3_snapshots **, qa_error *);
bool frontend_unified_q3_snapshots_destroy(frontend_unified_q3_snapshots **, qa_error *);
bool frontend_unified_q3_snapshots_idle(const frontend_unified_q3_snapshots *);
/* Runs real child Init in its lexical scope. No snapshot or predicted state is
 * admitted until that constructor returns and marks the CLIENT initialized. */
bool frontend_unified_q3_snapshots_initialize(frontend_unified_q3_snapshots *,
    bool (*)(void *, const q3n_compiled_frame *, qa_error *), void *, qa_error *);
bool frontend_unified_q3_snapshots_process(frontend_unified_q3_snapshots *, int32_t presentation_time,
    bool no_predict, bool synchronous_clients, qa_error *);
bool frontend_unified_q3_snapshots_read(const frontend_unified_q3_snapshots *, q3n_compiled_frame *, qa_error *);
q3n_entity *frontend_unified_q3_snapshots_storage(frontend_unified_q3_snapshots *);
/* Publishes the actual predictor's merged Q3 PS copy. A foreign movement owner
 * must supply its authentic Q3 adapter; this API performs no family cast. */
bool frontend_unified_q3_snapshots_prediction(frontend_unified_q3_snapshots *, const qa_q3_player *,
    uint64_t command_receipt, qa_vec3 correction, int32_t correction_time, bool hyperspace, qa_error *);
bool frontend_unified_q3_snapshots_checkpoint(const frontend_unified_q3_snapshots *, qa_buffer *, qa_error *);
bool frontend_unified_q3_snapshots_restore(const frontend_unified_q3_snapshots_options *, qa_bytes,
    frontend_unified_q3_snapshots **, qa_error *);

#endif
