#ifndef QA_Q1_WIRE_INTERNAL_H
#define QA_Q1_WIRE_INTERNAL_H

#include "internal.h"
#include "qa/game_q1_wire.h"

typedef struct q1_wire_table {
    qa_string_id *rows;
    size_t count, capacity;
} q1_wire_table;
typedef struct q1_wire_damage {
    qa_actor_id recipient;
    qa_q1_wire_feedback value;
    uint64_t revision;
} q1_wire_damage;
typedef struct q1_wire_client {
    qa_actor_id actor;
    qa_string_id name;
    float frags;
    uint8_t colors;
    bool present;
} q1_wire_client;
typedef struct q1_wire_state {
    q1_wire_table models, sounds;
    q1_wire_damage *damage;
    q1_wire_client *board;
    size_t damage_count, damage_capacity;
    uint64_t generation, revision;
    qa_string_id map_path;
    qa_string_id lightstyles[64];
    uint32_t next_dynamic, authored_entities, inline_models;
    bool loading, id1;
} q1_wire_state;

void q1_wire_destroy(qa_q1_game *);
void q1_wire_map_reset(qa_q1_game *);
void q1_wire_actor_released(qa_q1_game *, qa_actor_id);
bool q1_wire_allocate_slot(qa_q1_game *, bool *, uint32_t *, qa_error *);
bool q1_wire_spawn_declarations(qa_q1_game *, const qa_q1_spawn *, qa_error *);
void q1_wire_changed(q1_wire_state *);

#endif
