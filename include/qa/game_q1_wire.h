#ifndef QA_GAME_Q1_WIRE_H
#define QA_GAME_Q1_WIRE_H

#include "qa/game_q1.h"

/* A receipt holds the actual native source owner through every dependent read.
 * Model and sound zero are empty; all other rows retain declaration order. */
typedef struct qa_q1_wire_receipt {
    qa_q1_game_operation operation;
    uint64_t generation;
    qa_actor_owner owner;
    uint32_t client_slots, entity_slots;
    const qa_string_id *models, *sounds;
    size_t model_count, sound_count;
    qa_string_id map_path;
    double seconds;
    int32_t deathmatch;
    qa_q1_program program;
    qa_q1_edition edition;
    bool standard_quake;
} qa_q1_wire_receipt;

typedef struct qa_q1_wire_feedback {
    float armor, blood;
    qa_actor_id inflictor;
    double origin[3];
} qa_q1_wire_feedback;
typedef struct qa_q1_wire_world {
    qa_string_id map, level, lightstyles[64];
    uint32_t total_secrets, found_secrets, total_monsters, killed_monsters, server_flags;
} qa_q1_wire_world;
typedef struct qa_q1_wire_player {
    qa_string_id weapon_model;
    int32_t weapon_frame;
    uint32_t weapon, weapons, powers, ammo_items, extra_items;
    double ammo, shells, nails, rockets, cells;
} qa_q1_wire_player;
typedef struct qa_q1_wire_board_change {
    qa_actor_id actor;
    qa_string_id name;
    float frags;
    uint32_t slot;
    uint8_t colors;
    bool present, name_changed, frags_changed, colors_changed;
} qa_q1_wire_board_change;

bool qa_q1_wire_begin_world(qa_q1_game *, const char *map_path, uint32_t inline_models,
                            uint32_t authored_entities, qa_error *);
bool qa_q1_wire_declare_model(qa_q1_game *, const char *, qa_error *);
bool qa_q1_wire_declare_sound(qa_q1_game *, const char *, qa_error *);
bool qa_q1_wire_freeze(qa_q1_game *, qa_error *);
bool qa_q1_wire_authored_slot(const qa_q1_game *, size_t ordinal, uint32_t *);
bool qa_q1_wire_emission_index(const qa_q1_game *, bool models, qa_string_id, uint32_t *);
bool qa_q1_wire_emission_slot(const qa_q1_game *, qa_actor_id, uint32_t *);
bool qa_q1_wire_enabled(const qa_q1_game *);
/* Pure state of the actual native NetQuake registration owner. */
bool qa_q1_wire_registration_state(const qa_q1_game *, uint64_t *generation, bool *loading);
bool qa_q1_wire_lightstyle(qa_q1_game *, int32_t, qa_string_id, qa_error *);
/* Observe the actual source pattern table independently of transport admission. */
bool qa_q1_source_lightstyle_read(const qa_q1_game *, uint32_t, qa_string_id *, qa_error *);
bool qa_q1_wire_world_read(const qa_q1_wire_receipt *, qa_q1_wire_world *);
bool qa_q1_wire_player_read(const qa_q1_wire_receipt *, qa_actor_id, qa_q1_wire_player *, qa_error *);
bool qa_q1_wire_board_observe(const qa_q1_wire_receipt *, uint32_t client_slot,
                              qa_q1_wire_board_change *, qa_error *);
bool qa_q1_wire_board_commit(const qa_q1_wire_receipt *, const qa_q1_wire_board_change *);
bool qa_q1_wire_read_begin(qa_q1_game *, qa_q1_wire_receipt *, qa_error *);
bool qa_q1_wire_receipt_current(const qa_q1_wire_receipt *);
void qa_q1_wire_read_end(qa_q1_wire_receipt *);
bool qa_q1_wire_index(const qa_q1_wire_receipt *, bool models, qa_string_id, uint32_t *);
bool qa_q1_wire_actor_slot(const qa_q1_wire_receipt *, qa_actor_id, uint32_t *);
bool qa_q1_wire_actor_at(const qa_q1_wire_receipt *, uint32_t, qa_actor_id *);
bool qa_q1_wire_feedback_add(qa_q1_game *, qa_actor_id recipient, qa_actor_id inflictor,
                             float armor, float blood, const double origin[3], qa_error *);
bool qa_q1_wire_feedback_consume(const qa_q1_wire_receipt *, qa_actor_id,
                                 qa_q1_wire_feedback *);

#endif
