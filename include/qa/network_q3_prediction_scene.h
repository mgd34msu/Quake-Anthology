#ifndef QA_NETWORK_Q3_PREDICTION_SCENE_H
#define QA_NETWORK_Q3_PREDICTION_SCENE_H

#include "qa/network_q3.h"
#include "qa/collision.h"

typedef struct qa_q3_prediction_scene qa_q3_prediction_scene;
typedef struct qa_q3_prediction_scene_view {
    const qa_q3_snapshot *snapshot, *next_snapshot, *prediction_snapshot;
    int32_t time, physics_time, processed_snapshot;
    bool this_frame_teleport, next_frame_teleport;
    uint64_t revision;
} qa_q3_prediction_scene_view;
typedef struct qa_q3_prediction_scene_collision {
    qa_collision_geometry *geometry;
    void *context;
    bool (*actor_at)(void *, uint32_t number, qa_actor_id *, bool *, qa_error *);
    bool (*number_of)(void *, qa_actor_id, uint32_t *, bool *, qa_error *);
    qa_trace_scratch *scratch;
} qa_q3_prediction_scene_collision;
typedef struct qa_q3_prediction_scene_entity_view {
    const qa_q3_entity *entity;
    uint32_t source_number;
    int32_t publication_message;
    bool published, current_valid;
} qa_q3_prediction_scene_entity_view;

bool qa_q3_prediction_scene_create(qa_q3_product, qa_q3_prediction_scene **, qa_error *);
void qa_q3_prediction_scene_destroy(qa_q3_prediction_scene *);
/* Clear-active creates a cold decoded scene. A fast map_restart preserves
 * current/next entity rows and marks the genuine teleport transition. */
void qa_q3_prediction_scene_clear(qa_q3_prediction_scene *);
bool qa_q3_prediction_scene_restart(qa_q3_prediction_scene *, qa_error *);
/* Borrow only genuinely decoded native history. Copies use the original
 * CL_GetSnapshot 256-entry extent and preserve its append order. This owner
 * calls no source program or server-command consumer. */
bool qa_q3_prediction_scene_process(qa_q3_prediction_scene *, int32_t latest,
    qa_q3_snapshot_lookup, void *, int32_t presentation_time, qa_error *);
bool qa_q3_prediction_scene_read(const qa_q3_prediction_scene *, qa_q3_prediction_scene_view *);
bool qa_q3_prediction_scene_current(const qa_q3_prediction_scene *, const qa_q3_prediction_scene_view *);
/* Prediction queries the retained prior packet poses. Publish the next poses
 * only at the actual packet-presentation boundary, after prediction. */
bool qa_q3_prediction_scene_publish_poses(qa_q3_prediction_scene *, bool smooth_clients, qa_error *);
bool qa_q3_prediction_scene_consume_teleport(qa_q3_prediction_scene *, qa_error *);
/* Before prediction, apply the actual returned CG player-transition feedback.
 * This selects the retained current snapshot without changing its raw PS. */
bool qa_q3_prediction_scene_mark_teleport(qa_q3_prediction_scene *,
    const qa_q3_prediction_scene_view *, qa_error *);
/* Ordered membership borrows the actual current centity, including retained
 * cold rows selected by the original BuildSolidList ordering. Publication is
 * the decoded message which assigned current state, never a frame counter. */
bool qa_q3_prediction_scene_solid_at(const qa_q3_prediction_scene *, const qa_q3_prediction_scene_view *,
    size_t ordinal, qa_q3_prediction_scene_entity_view *, bool *present, qa_error *);
bool qa_q3_prediction_scene_trigger_count(const qa_q3_prediction_scene *, const qa_q3_prediction_scene_view *,
    size_t *, qa_error *);
bool qa_q3_prediction_scene_trigger_at(const qa_q3_prediction_scene *, const qa_q3_prediction_scene_view *,
    size_t ordinal, qa_q3_prediction_scene_entity_view *, bool *present, qa_error *);
bool qa_q3_prediction_scene_entity_current(const qa_q3_prediction_scene *, const qa_q3_prediction_scene_view *,
    const qa_q3_prediction_scene_entity_view *);
bool qa_q3_prediction_scene_adjust_mover(const qa_q3_prediction_scene *, const qa_q3_prediction_scene_view *,
    qa_vec3 origin, int32_t mover, int32_t from_time, int32_t to_time, qa_vec3 *, qa_error *);
bool qa_q3_prediction_scene_trigger_overlap(const qa_q3_prediction_scene *, const qa_q3_prediction_scene_view *,
    qa_collision_geometry *, qa_trace_scratch *, const qa_q3_prediction_scene_entity_view *, qa_vec3 origin, qa_bounds,
    bool *, qa_error *);
bool qa_q3_prediction_scene_item_position(const qa_q3_prediction_scene *, const qa_q3_prediction_scene_view *,
    const qa_q3_prediction_scene_entity_view *, qa_vec3 *, qa_error *);
bool qa_q3_prediction_scene_trace(const qa_q3_prediction_scene *, const qa_q3_prediction_scene_view *,
    const qa_q3_prediction_scene_collision *, const qa_trace_query *, qa_trace_result *, qa_error *);
/* Returns the literal winning currentState.number from this same query,
 * including cold row zero, WORLD and NONE, before actor adaptation. */
bool qa_q3_prediction_scene_trace_with_number(const qa_q3_prediction_scene *, const qa_q3_prediction_scene_view *,
    const qa_q3_prediction_scene_collision *, const qa_trace_query *, qa_trace_result *, int32_t *, qa_error *);
bool qa_q3_prediction_scene_point_contents(const qa_q3_prediction_scene *, const qa_q3_prediction_scene_view *,
    const qa_q3_prediction_scene_collision *, const qa_point_query *, qa_point_contents *, qa_error *);
bool qa_q3_prediction_scene_is_bsp(const qa_q3_prediction_scene *, const qa_q3_prediction_scene_view *,
    const qa_q3_prediction_scene_collision *, const qa_trace_result *, bool *, qa_error *);
bool qa_q3_prediction_scene_checkpoint(const qa_q3_prediction_scene *, qa_buffer *, qa_error *);
bool qa_q3_prediction_scene_restore(qa_bytes, qa_q3_product, qa_q3_prediction_scene **, qa_error *);

#endif
