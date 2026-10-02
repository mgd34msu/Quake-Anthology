#ifndef QA_FRONTEND_NETWORK_PREDICTION_H
#define QA_FRONTEND_NETWORK_PREDICTION_H

#include "qa/frontend.h"
#include "qa/application_q3_client.h"
#include "qa/network_q3_prediction_scene.h"

typedef struct frontend_network_prediction_source {
    qa_net_client_id connection;
    uint64_t epoch, restart_generation;
    int32_t previous_presentation_time;
    qa_application_q3_client_context receiver;
    qa_q3_prediction_scene_view scene;
    const qa_collision_geometry *geometry;
    const qa_resource *map;
    qa_actor_id viewer;
} frontend_network_prediction_source;
struct frontend_remote_snapshots_view;
struct frontend_remote_prediction;
struct frontend_remote_prediction_source;

bool frontend_network_prediction_source_read(const qa_frontend *,
    frontend_network_prediction_source *, bool *present, qa_error *);
bool frontend_network_prediction_source_current(const qa_frontend *,
    const frontend_network_prediction_source *);
bool frontend_network_prediction_entity_current(const qa_frontend *,
    const frontend_network_prediction_source *, const qa_q3_prediction_scene_entity_view *);
/* Consumes only a returned, current retail CG receipt before replay. The
 * output is the actual refreshed scene identity, never a copied retail PS. */
bool frontend_network_prediction_teleport_feedback(qa_frontend *,
    const frontend_network_prediction_source *, const struct frontend_remote_snapshots_view *,
    frontend_network_prediction_source *, qa_error *);
/* Retail consumes its flag first. The completed predictor receipt then
 * authorizes this same raw scene transition, preserving its seed and clock. */
bool frontend_network_prediction_teleport_consume(qa_frontend *,
    const frontend_network_prediction_source *, const struct frontend_remote_snapshots_view *,
    const struct frontend_remote_prediction *, const struct frontend_remote_prediction_source *,
    frontend_network_prediction_source *, qa_error *);
bool frontend_network_prediction_trace(qa_frontend *, const frontend_network_prediction_source *,
    const qa_trace_query *, qa_trace_result *, qa_error *);
/* The entered frame retains these paired outputs as one genuine trace receipt. */
bool frontend_network_prediction_trace_with_number(qa_frontend *, const frontend_network_prediction_source *,
    const qa_trace_query *, qa_trace_result *, int32_t *source_number, qa_error *);
bool frontend_network_prediction_point_contents(qa_frontend *, const frontend_network_prediction_source *,
    const qa_point_query *, qa_point_contents *, qa_error *);
bool frontend_network_prediction_is_bsp(qa_frontend *, const frontend_network_prediction_source *,
    const qa_trace_result *, bool *, qa_error *);
bool frontend_network_prediction_adjust_mover(qa_frontend *, const frontend_network_prediction_source *,
    qa_vec3 origin, int32_t mover, int32_t from_time, int32_t to_time, qa_vec3 *, qa_error *);
bool frontend_network_prediction_trigger_count(qa_frontend *, const frontend_network_prediction_source *,
    size_t *, qa_error *);
bool frontend_network_prediction_trigger_at(qa_frontend *, const frontend_network_prediction_source *,
    size_t, qa_q3_prediction_scene_entity_view *, bool *present, qa_error *);
bool frontend_network_prediction_trigger_overlap(qa_frontend *, const frontend_network_prediction_source *,
    const qa_q3_prediction_scene_entity_view *, qa_vec3 origin, qa_bounds, bool *, qa_error *);
bool frontend_network_prediction_item_position(qa_frontend *, const frontend_network_prediction_source *,
    const qa_q3_prediction_scene_entity_view *, qa_vec3 *, qa_error *);
bool frontend_network_client_frame(qa_frontend *, qa_error *);
bool frontend_network_client_pose_publish(qa_frontend *, qa_error *);

#endif
