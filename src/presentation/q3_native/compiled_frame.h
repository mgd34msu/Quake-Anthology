#ifndef QA_Q3_NATIVE_COMPILED_FRAME_H
#define QA_Q3_NATIVE_COMPILED_FRAME_H

#include "compiled_source.h"
#include "qa/network_q3.h"

typedef struct q3n_entity q3n_entity;
typedef struct q3n_compiled_frame q3n_compiled_frame;
typedef enum q3n_compiled_stage {
    Q3N_COMPILED_INITIALIZATION, Q3N_COMPILED_SNAPSHOT_CALLBACK,
    Q3N_COMPILED_PLAYER_TRANSITION, Q3N_COMPILED_COMPLETED_FRAME,
    Q3N_COMPILED_CONSOLE, Q3N_COMPILED_AWAITING_SNAPSHOT,
    Q3N_COMPILED_LOADING_INFORMATION
} q3n_compiled_stage;
typedef struct q3n_compiled_entity {
    const q3n_compiled_frame *frame;
    const qa_q3_entity *current, *next;
    q3n_entity *presentation;
    qa_actor_id actor;
    uint32_t number;
    int32_t snapshot_number;
    bool published, current_valid, interpolate, predicted;
} q3n_compiled_entity;
/* Lexical receipts name the actual receiver/cache owner and its current
 * callback scope. A returned Draw frame borrows its completed cache revision. */
struct q3n_compiled_frame {
    q3n_compiled_source_view source;
    const void *owner;
    uint64_t revision, scope;
    q3n_compiled_stage stage;
    int32_t time, processed_snapshot, reached_command;
    const qa_q3_snapshot *snapshot, *next_snapshot;
    q3n_entity *entities;
    qa_q3_player *predicted_player;
    qa_q3_entity *predicted_state, *predicted_next_state;
    q3n_entity *predicted_entity;
    const qa_q3_player *transition_player, *previous_player;
    qa_vec3 prediction_error;
    int32_t prediction_error_time;
    bool hyperspace, this_frame_teleport, next_frame_teleport;
    void *context;
    bool (*current)(void *, const q3n_compiled_frame *);
    bool (*entity)(void *, const q3n_compiled_frame *, uint32_t, q3n_compiled_entity *, qa_error *);
    bool (*entity_event)(void *, const q3n_compiled_frame *, uint32_t, int32_t, int32_t, qa_error *);
    bool (*entity_trajectory)(void *, const q3n_compiled_entity *, int32_t, int32_t, int32_t, int32_t, qa_error *);
    bool (*entity_weapon)(void *, const q3n_compiled_entity *, int32_t, int32_t, qa_error *);
    bool (*prediction_error_clear)(void *, const q3n_compiled_frame *, qa_error *);
    bool (*trace_number)(void *, const q3n_compiled_frame *, const qa_trace_result *, int32_t *, qa_error *);
};
bool q3n_compiled_frame_read(const q3n_compiled_frame *, q3n_compiled_frame *, qa_error *);
bool q3n_compiled_frame_current(const q3n_compiled_frame *);
bool q3n_compiled_frame_entity(const q3n_compiled_frame *, uint32_t, q3n_compiled_entity *, qa_error *);
bool q3n_compiled_frame_predicted(const q3n_compiled_frame *, q3n_compiled_entity *, qa_error *);
bool q3n_compiled_entity_current(const q3n_compiled_entity *);
bool q3n_compiled_frame_entity_event(const q3n_compiled_frame *, uint32_t, int32_t, int32_t, qa_error *);
bool q3n_compiled_frame_entity_trajectory(const q3n_compiled_entity *, int32_t, int32_t, int32_t, int32_t, qa_error *);
bool q3n_compiled_frame_entity_weapon(const q3n_compiled_entity *, int32_t, int32_t, qa_error *);
bool q3n_compiled_frame_prediction_error_clear(const q3n_compiled_frame *, qa_error *);
bool q3n_compiled_frame_trace_number(const q3n_compiled_frame *, const qa_trace_result *, int32_t *, qa_error *);

#endif
