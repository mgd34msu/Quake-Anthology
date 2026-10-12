#ifndef QA_Q1_WIRE_INTERNAL_H
#define QA_Q1_WIRE_INTERNAL_H

#include "internal.h"
#include "qa/game_q1_wire.h"
#include "qa/id_index.h"

typedef struct q1_wire_table {
    qa_string_id *rows;
    size_t count, capacity;
    qa_arena ordinal_storage;
    qa_id_index ordinals;
} q1_wire_table;
typedef struct q1_wire_client {
    qa_actor_id actor;
    qa_string_id name;
    float frags;
    uint8_t colors;
    bool present;
    qa_vec3 eye;
} q1_wire_client;
typedef struct q1_wire_edict {
    float freetime;
    bool free;
    qa_actor_id released;
} q1_wire_edict;
typedef struct q1_qw_fraglog {
    uint8_t buffers[2][1450];
    uint32_t sizes[2], sequence;
    double time;
    bool overflowed[2];
} q1_qw_fraglog;
typedef struct q1_wire_state {
    q1_wire_table models, sounds;
    q1_wire_client *board;
    uint64_t generation, revision;
    qa_string_id map_path;
    qa_lightstyles lightstyles;
    double qw_client_stats[32][16];
    q1_qw_fraglog qw_fraglog;
    q1_wire_edict *edicts;
    qa_arena edict_storage;
    uint32_t edict_capacity;
    uint32_t next_dynamic, authored_entities, authored_cursor, inline_models, edict_limit;
    bool loading, id1;
} q1_wire_state;

void q1_wire_destroy(qa_q1_game *);
void q1_wire_map_reset(qa_q1_game *);
void q1_wire_actor_released(qa_q1_game *, qa_actor_record);
bool q1_wire_allocate_slot(qa_q1_game *, bool *, uint32_t *, qa_error *);
bool q1_wire_spawn_slot_valid(const qa_q1_game *, uint32_t);
bool q1_wire_spawn_declarations(qa_q1_game *, const qa_q1_spawn *, qa_error *);
void q1_wire_player_items(qa_q1_program,qa_q1_weapon,const double powers[QA_Q1_POWER_COUNT],
    double,const qa_armor *,uint32_t,bool,uint32_t *items,uint32_t *items2);
void q1_wire_changed(q1_wire_state *);
bool q1_wire_edict_extent(q1_wire_state *, uint32_t, qa_error *);

#endif
