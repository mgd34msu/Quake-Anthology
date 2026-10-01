#ifndef QA_Q3_NATIVE_REMOTE_FRAME_H
#define QA_Q3_NATIVE_REMOTE_FRAME_H

#include "qa/application_native_q3_remote_client.h"

typedef struct q3n_remote_source q3n_remote_source;
typedef struct q3n_frame q3n_frame;
typedef struct q3n_remote_frame q3n_remote_frame;
typedef struct q3n_entity q3n_entity;

/* A Network observation. The gamestate is the real reached transport owner,
 * not an authoritative GAME observation or a second configstring array.
 * executed_command is Network's literal engine acknowledgment; it need not
 * equal CGAME's private reached cursor after an absent demo result. */
typedef struct q3n_remote_publication {
    qa_net_client_id connection;
    uint64_t epoch, restart_generation;
    const qa_q3_gamestate *gamestate;
    qa_actor_id viewer;
    int32_t initial_message, initial_command, latest_message, latest_time;
    int32_t presentation_time, server_message, received_command, executed_command;
    bool initializing, has_snapshot, demo_playback;
} q3n_remote_publication;
typedef struct q3n_remote_command {
    q3n_remote_publication publication;
    int32_t sequence;
    const qa_q3_tokens *tokens;
    bool present;
} q3n_remote_command;
typedef struct q3n_remote_source_options {
    qa_native_q3_remote_client_service *client;
    void *context;
    bool (*publication_read)(void *, q3n_remote_publication *, qa_error *);
    bool (*publication_current)(void *, const q3n_remote_publication *);
    /* Read executes the actual Network command exactly once. It retains the
     * Network token receipt through the real command callback. */
    bool (*command_read)(void *, int32_t, q3n_remote_command *, qa_error *);
    bool (*command_current)(void *, const q3n_remote_command *);
    bool (*idle)(void *);
} q3n_remote_source_options;
typedef struct q3n_remote_source_view {
    const q3n_remote_source *owner;
    qa_native_q3_remote_client_basis basis;
    q3n_remote_publication publication;
    int32_t reached_command, game_type, max_clients, dm_flags, match_start_time;
} q3n_remote_source_view;

bool q3n_remote_source_create(const q3n_remote_source_options *, q3n_remote_source **, qa_error *);
bool q3n_remote_source_destroy(q3n_remote_source *, qa_error *);
qa_native_q3_remote_client_service *q3n_remote_source_client(const q3n_remote_source *);
bool q3n_remote_source_idle(const q3n_remote_source *);
bool q3n_remote_source_read(const q3n_remote_source *, q3n_remote_source_view *, qa_error *);
bool q3n_remote_source_current(const q3n_remote_source_view *);
bool q3n_remote_source_configstring(const q3n_remote_source *, uint32_t,
    const char **text, uint64_t *revision, qa_error *);
/* The snapshot owner may already have executed Network's command. Adopt that
 * genuine receipt before CGAME dispatch, including absent bcs fragments and
 * cycled demo commands. The real command_current callback proves the result;
 * adopting it never rewrites Network's literal acknowledgment. */
bool q3n_remote_source_reached(q3n_remote_source *, const q3n_remote_command *, qa_error *);
bool q3n_remote_source_command(q3n_remote_source *, int32_t, q3n_remote_command *, qa_error *);
bool q3n_remote_command_current(const q3n_remote_source *, const q3n_remote_command *);
bool q3n_remote_source_checkpoint(const q3n_remote_source *, qa_buffer *, qa_error *);
bool q3n_remote_source_restore(const q3n_remote_source_options *, qa_bytes, q3n_remote_source **, qa_error *);

typedef enum q3n_remote_frame_stage {
    Q3N_REMOTE_INITIALIZATION,
    Q3N_REMOTE_SNAPSHOT_CALLBACK,
    Q3N_REMOTE_PREDICTION_CALLBACK,
    Q3N_REMOTE_COMPLETED_FRAME
} q3n_remote_frame_stage;
/* These are borrowed real cache rows. An unpublished constructor row is
 * available to entityAt; published and render-valid have separate meanings. */
typedef struct q3n_remote_entity {
    const q3n_remote_frame *frame;
    const qa_q3_entity *current, *next;
    q3n_entity *presentation;
    uint32_t number;
    int32_t publication_message;
    bool published, current_valid, interpolate, predicted;
} q3n_remote_entity;
typedef struct q3n_remote_snapshot_receipt {
    const void *owner;
    uint64_t revision;
    q3n_remote_frame_stage stage;
    uint64_t callback_scope;
    const qa_q3_snapshot *snapshot, *next_snapshot;
    q3n_entity *entities; /* actual contiguous 1024-row presentation owner */
    int32_t time, processed_message, command_sequence;
    bool this_frame_teleport, next_frame_teleport;
} q3n_remote_snapshot_receipt;
typedef struct q3n_remote_prediction_receipt {
    const void *owner;
    const qa_q3_player *player;
    uint64_t receipt_time_ns, command_sequence;
    int32_t seed_message, presentation_time, physics_time;
    qa_vec3 prediction_error;
    int32_t prediction_error_time;
    bool hyperspace;
} q3n_remote_prediction_receipt;
typedef struct q3n_remote_frame_options {
    q3n_remote_source_view source;
    q3n_remote_snapshot_receipt snapshots;
    q3n_remote_prediction_receipt prediction;
    /* The actual separate predictedPlayerEntity continuation is retained by
     * CGAME. Packet presentation alone projects PS into this private ES. */
    qa_q3_entity *predicted_state;
    qa_q3_entity *predicted_next_state;
    q3n_entity *predicted_entity;
    /* CGAME's private prediction copy retains BG event conversion across
     * stereo/draw calls. It is distinct from prediction.player and raw PS. */
    qa_q3_player *predicted_player;
    const qa_q3_player *transition_player, *previous_player;
    uint64_t initialization_scope; /* actual entered CG_Init constructor */
    uint64_t transition_scope; /* actual entered predictor-completion callback */
    void *context;
    bool (*current)(void *, const q3n_remote_frame *);
    bool (*entity)(void *, const q3n_remote_frame *, uint32_t, q3n_remote_entity *, qa_error *);
    bool (*entity_event)(void *, const q3n_remote_frame *, uint32_t,
        int32_t event, int32_t parameter, qa_error *);
    bool (*entity_trajectory)(void *, const q3n_remote_entity *,
        int32_t current_before, int32_t next_before, int32_t current_after, int32_t next_after, qa_error *);
    bool (*entity_weapon)(void *, const q3n_remote_entity *, int32_t before, int32_t after, qa_error *);
    bool (*prediction_error_clear)(void *, const q3n_remote_frame *, qa_error *);
    /* The collision producer proves its own raw entity-number witness. No
     * decoded ES number is converted into an invented canonical actor. */
    bool (*trace_number)(void *, const q3n_remote_frame *, const qa_trace_result *, int32_t *, qa_error *);
} q3n_remote_frame_options;
struct q3n_remote_frame {
    qa_native_q3_remote_client_service *client;
    q3n_remote_source_view source;
    q3n_remote_snapshot_receipt snapshots;
    q3n_remote_prediction_receipt prediction;
    qa_q3_entity *predicted_state;
    qa_q3_entity *predicted_next_state;
    q3n_entity *predicted_entity;
    qa_q3_player *predicted_player;
    const qa_q3_player *transition_player, *previous_player;
    uint64_t initialization_scope;
    uint64_t transition_scope;
    void *context;
    bool (*current)(void *, const q3n_remote_frame *);
    bool (*entity)(void *, const q3n_remote_frame *, uint32_t, q3n_remote_entity *, qa_error *);
    bool (*entity_event)(void *, const q3n_remote_frame *, uint32_t,
        int32_t event, int32_t parameter, qa_error *);
    bool (*entity_trajectory)(void *, const q3n_remote_entity *,
        int32_t current_before, int32_t next_before, int32_t current_after, int32_t next_after, qa_error *);
    bool (*entity_weapon)(void *, const q3n_remote_entity *, int32_t before, int32_t after, qa_error *);
    bool (*prediction_error_clear)(void *, const q3n_remote_frame *, qa_error *);
    bool (*trace_number)(void *, const q3n_remote_frame *, const qa_trace_result *, int32_t *, qa_error *);
};
bool q3n_remote_frame_read(const q3n_remote_frame_options *, q3n_remote_frame *, qa_error *);
bool q3n_remote_frame_current(const q3n_remote_frame *);
bool q3n_remote_frame_entity(const q3n_remote_frame *, uint32_t, q3n_remote_entity *, qa_error *);
bool q3n_remote_frame_predicted(const q3n_remote_frame *, q3n_remote_entity *, qa_error *);
bool q3n_remote_entity_current(const q3n_remote_entity *);
bool q3n_remote_frame_entity_event(const q3n_remote_frame *, uint32_t,
    int32_t event, int32_t parameter, qa_error *);
/* Expected values qualify mutation of the actual CGAME cache owner. These
 * stores never author Network snapshots, predictor PS or GAME state. */
bool q3n_remote_frame_entity_trajectory(const q3n_remote_entity *,
    int32_t current_before, int32_t next_before, int32_t current_after, int32_t next_after, qa_error *);
bool q3n_remote_frame_entity_weapon(const q3n_remote_entity *, int32_t before, int32_t after, qa_error *);
bool q3n_remote_frame_prediction_error_clear(const q3n_remote_frame *, qa_error *);
bool q3n_remote_frame_trace_number(const q3n_remote_frame *, const qa_trace_result *, int32_t *, qa_error *);

/* Common source observations. Remote entity presence means published, even
 * when that genuine followed-player row is not render-valid. */
bool q3n_frame_current(const q3n_frame *);
qa_q3_product q3n_frame_product(const q3n_frame *);
int32_t q3n_frame_game_type(const q3n_frame *);
int32_t q3n_frame_max_clients(const q3n_frame *);
int32_t q3n_frame_match_start_time(const q3n_frame *);
uint32_t q3n_frame_entity_capacity(const q3n_frame *);
const qa_q3_player *q3n_frame_snapshot_player(const q3n_frame *);
const qa_q3_player *q3n_frame_predicted_player(const q3n_frame *);
bool q3n_frame_configstring(const q3n_frame *, uint32_t, const char **, uint64_t *, qa_error *);
bool q3n_frame_entity(const q3n_frame *, uint32_t, qa_q3_entity *, q3n_entity **,
    bool *present, qa_error *);

#endif
