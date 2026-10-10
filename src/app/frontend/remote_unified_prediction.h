#ifndef QA_FRONTEND_REMOTE_UNIFIED_PREDICTION_H
#define QA_FRONTEND_REMOTE_UNIFIED_PREDICTION_H

#include "remote_unified.h"
#include "qa/network_q3.h"

typedef struct frontend_remote_unified_prediction frontend_remote_unified_prediction;
qa_trace_scratch *frontend_remote_unified_prediction_scratch(const frontend_remote_unified_prediction *);
typedef enum frontend_unified_prediction_status {
    FRONTEND_UNIFIED_PREDICTION_UNCHANGED, FRONTEND_UNIFIED_PREDICTION_ACTIVE,
    FRONTEND_UNIFIED_PREDICTION_DISABLED, FRONTEND_UNIFIED_PREDICTION_EXHAUSTED
} frontend_unified_prediction_status;
typedef struct frontend_unified_prediction_view {
    qa_actor_id actor;
    qa_movement_state state;
    qa_vec3 origin_shift, view_angles, view_offset;
    qa_bounds bounds;
    qa_movement_ground ground;
    float view_height;
    double command_time_ms;
    int64_t sequence;
    uint64_t authoritative_frame;
    frontend_unified_prediction_status status;
} frontend_unified_prediction_view;
typedef struct frontend_unified_q3_prediction_source {
    void *context;
    bool (*current)(void *,qa_error *);
    bool (*number)(void *,qa_actor_id,uint32_t *,bool *found,qa_error *);
} frontend_unified_q3_prediction_source;
bool frontend_remote_unified_prediction_merged_q3(frontend_remote_unified_prediction *,
    const qa_q3_player *,qa_actor_id,const frontend_unified_q3_prediction_source *,
    qa_q3_player *,frontend_unified_prediction_view *,qa_error *);

/* Borrows the replica's actual recipe geometry and private identity registry.
 * Close this child before retiring either owner. It never owns a GAME source. */
bool frontend_remote_unified_prediction_create(frontend_remote_unified *,
    frontend_remote_unified_prediction **, qa_error *);
/* Reads the actual whole typed FRAME; no child prediction document is built. */
bool frontend_remote_unified_prediction_receive(frontend_remote_unified_prediction *,
    const qa_unified_document *, qa_error *);
/* Raw input stays absolute and unmodified. Time is the input producer's actual
 * accumulated source clock, independent of NQ acknowledged_server_seconds. */
bool frontend_remote_unified_prediction_input(frontend_remote_unified_prediction *,
    const qa_usercmd *, double command_time_ms, qa_error *);
bool frontend_remote_unified_prediction_read(frontend_remote_unified_prediction *,
    frontend_unified_prediction_view *, qa_error *);
/* Matches the real prior PPS command clock at an actually consumed Q3 replay
 * boundary. Disabled/exhausted/no-command paths return false. */
bool frontend_remote_unified_prediction_read_command_boundary(frontend_remote_unified_prediction *,
    int32_t prior_command_time,frontend_unified_prediction_view *,bool *matched,qa_error *);
/* Authoritative input baseline, without replaying unacknowledged commands. */
bool frontend_remote_unified_prediction_snapshot(const frontend_remote_unified_prediction *,
    frontend_unified_prediction_view *, qa_error *);
const qa_unified_document *frontend_remote_unified_prediction_document(
    const frontend_remote_unified_prediction *);
bool frontend_remote_unified_prediction_time(const frontend_remote_unified_prediction *,
    double *command_time_ms, qa_error *);
/* Queries the received collision-only world; neither call replays input. */
bool frontend_remote_unified_prediction_trace(frontend_remote_unified_prediction *,
    const qa_trace_query *, qa_trace_result *, qa_error *);
bool frontend_remote_unified_prediction_body_read(const frontend_remote_unified_prediction *,
    qa_actor_id, qa_body_state *, qa_error *);
bool frontend_remote_unified_prediction_point_contents(frontend_remote_unified_prediction *,
    const qa_point_query *, qa_point_contents *, qa_error *);
bool frontend_remote_unified_prediction_player_origin(frontend_remote_unified_prediction *,qa_vec3 *,qa_error *);
bool frontend_remote_unified_prediction_idle(const frontend_remote_unified_prediction *);
bool frontend_remote_unified_prediction_destroy(frontend_remote_unified_prediction **, qa_error *);

#endif
