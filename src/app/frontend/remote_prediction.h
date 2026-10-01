#ifndef QA_FRONTEND_REMOTE_PREDICTION_H
#define QA_FRONTEND_REMOTE_PREDICTION_H

#include "remote_input.h"
#include "qa/network_q3.h"
#include "qa/network_q3_prediction_scene.h"

typedef struct frontend_remote_prediction frontend_remote_prediction;
typedef struct frontend_remote_prediction_settings {
    int32_t game_type, dm_flags, pmove_msec;
    int32_t error_decay_integer, show_miss;
    float error_decay_value;
    bool demo_playback, no_predict, synchronous_clients, predict_items, pmove_fixed;
} frontend_remote_prediction_settings;
typedef struct frontend_remote_prediction_source {
    frontend_remote_input_source input;
    qa_application_control_prediction_configuration configuration;
    const qa_collision_geometry *geometry;
    qa_q3_prediction_scene_view scene;
    frontend_remote_prediction_settings settings;
    int32_t previous_presentation_time;
    uint64_t receipt_time_ns;
    bool has_acknowledged_sequence, history_unavailable;
    uint64_t acknowledged_sequence;
    /* Genuine network-owned clear/map_restart receipt generation. It is
     * independent of launch configuration and physical connection epochs. */
    uint64_t restart_generation;
} frontend_remote_prediction_source;
typedef struct frontend_remote_prediction_options {
    qa_session *session;
    qa_application_control_prediction_configuration initial_configuration;
    void *context;
    bool (*configuration_current)(void *, const qa_application_control_prediction_configuration *);
    bool (*source_read)(void *, frontend_remote_prediction_source *, bool *present, qa_error *);
    bool (*source_current)(void *, const frontend_remote_prediction_source *);
    bool (*actor_at)(void *, uint32_t source_number, qa_actor_id *, bool *present, qa_error *);
    bool (*number_of)(void *, qa_actor_id, uint32_t *, bool *present, qa_error *);
    bool (*trace)(void *, const frontend_remote_prediction_source *,
        const qa_trace_query *, qa_trace_result *, qa_error *);
    bool (*point_contents)(void *, const frontend_remote_prediction_source *,
        const qa_point_query *, qa_point_contents *, qa_error *);
    bool (*is_bsp)(void *, const frontend_remote_prediction_source *,
        const qa_trace_result *, bool *, qa_error *);
    bool (*adjust_mover)(void *, const frontend_remote_prediction_source *, qa_vec3,
        int32_t source_number, int32_t from_time, int32_t to_time, qa_vec3 *, qa_error *);
    bool (*trigger_count)(void *, const frontend_remote_prediction_source *, size_t *, qa_error *);
    bool (*trigger_at)(void *, const frontend_remote_prediction_source *, size_t,
        qa_q3_prediction_scene_entity_view *, bool *, qa_error *);
    bool (*trigger_overlap)(void *, const frontend_remote_prediction_source *,
        const qa_q3_prediction_scene_entity_view *, qa_vec3, qa_bounds, bool *, qa_error *);
    bool (*item_position)(void *, const frontend_remote_prediction_source *,
        const qa_q3_prediction_scene_entity_view *, qa_vec3 *, qa_error *);
    /* The genuine presentation cache supplies miscTime. Raw snapshot rows do
     * not own it; source item respawn/entity effects can change it separately. */
    bool (*item_misc_time)(void *, const frontend_remote_prediction_source *,
        const qa_q3_prediction_scene_entity_view *, int32_t *, qa_error *);
    /* Active prediction clamps the actual CLIENT registry after interpolation
     * and history guards. The setter qualifies the actual publication; the
     * cached CGAME frame scalar stays unchanged until its genuine update. */
    bool (*set_pmove_msec)(void *, const frontend_remote_prediction_source *, int32_t, qa_error *);
    bool (*warning)(void *, const frontend_remote_prediction_source *, const char *, qa_error *);
} frontend_remote_prediction_options;
typedef enum frontend_remote_prediction_status {
    FRONTEND_REMOTE_PREDICTION_UNCHANGED,
    FRONTEND_REMOTE_PREDICTION_PREDICTED,
    FRONTEND_REMOTE_PREDICTION_DISABLED,
    FRONTEND_REMOTE_PREDICTION_HISTORY_EXHAUSTED
} frontend_remote_prediction_status;
typedef enum frontend_remote_prediction_angle_space {
    FRONTEND_REMOTE_PREDICTION_SOURCE_RELATIVE,
    FRONTEND_REMOTE_PREDICTION_ABSOLUTE
} frontend_remote_prediction_angle_space;
typedef struct frontend_remote_prediction_view {
    qa_movement_state movement;
    qa_q3_player player;
    qa_bounds bounds;
    qa_vec3 view_angles, command_angles, view_offset;
    qa_movement_ground ground;
    float view_height;
    int32_t water_level, water_type, command_time;
    uint64_t sequence;
    qa_vec3 prediction_error;
    int32_t prediction_error_time;
    bool hyperspace, consumed_teleport;
    frontend_remote_prediction_status status;
} frontend_remote_prediction_view;

bool frontend_remote_prediction_create(const frontend_remote_prediction_options *,
    frontend_remote_prediction **, qa_error *);
void frontend_remote_prediction_destroy(frontend_remote_prediction *);
/* Only actual network clear-active/map_restart producers reset this owner. */
void frontend_remote_prediction_clear(frontend_remote_prediction *);
/* The real initial zero usercmd is appended before any physical receipt.
 * Admission associates the retained cold constructor continuation with it. */
bool frontend_remote_prediction_admit_initial(frontend_remote_prediction *,
    const qa_movement_command *actual_zero_source, qa_error *);
/* These are paired products of one physical receipt. The selected command
 * keeps its own axes; the independent raw Q3 command supplies source timing. */
bool frontend_remote_prediction_submit(frontend_remote_prediction *,
    const qa_movement_command *selected, frontend_remote_prediction_angle_space,
    const qa_movement_command *source, qa_error *);
/* Receives an actual decoded snapshot and replays private state with pure
 * collision queries. No source program, body write or GAME frame is invoked. */
bool frontend_remote_prediction_replay(frontend_remote_prediction *,
    frontend_remote_prediction_view *, bool *present, qa_error *);
bool frontend_remote_prediction_read(const frontend_remote_prediction *, const frontend_remote_prediction_source *,
    frontend_remote_prediction_view *);
/* Feedback from the real CGAME presentation owner requires the same completed
 * prediction receipt. It never changes decoded snapshot PS or source clocks. */
bool frontend_remote_prediction_error_clear(frontend_remote_prediction *,
    const frontend_remote_prediction_source *, qa_error *);
/* Packet conversion supplies the working CG PS cursor before/after its actual
 * BG_PlayerStateToEntityState call. Only that one conversion may be published. */
bool frontend_remote_prediction_entity_event_publish(frontend_remote_prediction *,
    const frontend_remote_prediction_source *, int32_t before, int32_t after, qa_error *);
/* Presentation reads private item effects against the exact retained row.
 * Authoritative source ES and collision rows remain unchanged. */
bool frontend_remote_prediction_item_read(const frontend_remote_prediction *,
    const frontend_remote_prediction_source *, const qa_q3_prediction_scene_entity_view *,
    int32_t *flags, int32_t *misc_time, qa_error *);
bool frontend_remote_prediction_checkpoint(const frontend_remote_prediction *, qa_buffer *, qa_error *);
bool frontend_remote_prediction_restore(frontend_remote_prediction *, qa_bytes, qa_error *);
bool frontend_remote_prediction_restore_new(const frontend_remote_prediction_options *,
    qa_bytes, frontend_remote_prediction **, qa_error *);

#endif
