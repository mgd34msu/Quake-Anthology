#ifndef QA_Q2_ORIGINAL_SAVE_INTERNAL_H
#define QA_Q2_ORIGINAL_SAVE_INTERNAL_H

#include "../entities/internal.h"
#include "../monsters/internal.h"
#include "qa/binary.h"
#include "qa/json.h"
#include "qa/json_writer.h"
#include "qa/q2_save.h"
#include "qa/network_q2.h"

typedef enum q2_original_record_kind {
    Q2_ORIGINAL_PERSISTENT,
    Q2_ORIGINAL_VIEW,
    Q2_ORIGINAL_CLIENT,
    Q2_ORIGINAL_WEAPON,
    Q2_ORIGINAL_POWERS,
    Q2_ORIGINAL_ENTITY,
    Q2_ORIGINAL_MOVER,
    Q2_ORIGINAL_GAME,
    Q2_ORIGINAL_LEVEL
} q2_original_record_kind;

typedef enum q2_original_field_kind {
    Q2_ORIGINAL_I32, Q2_ORIGINAL_U32, Q2_ORIGINAL_U64, Q2_ORIGINAL_I16, Q2_ORIGINAL_U8,
    Q2_ORIGINAL_F32, Q2_ORIGINAL_BOOL,
    Q2_ORIGINAL_VECTOR, Q2_ORIGINAL_TIME, Q2_ORIGINAL_FRAME_TIME,
    Q2_ORIGINAL_FRAME_INDEX, Q2_ORIGINAL_SECONDS_TIME, Q2_ORIGINAL_WEAPON_PHASE,
    Q2_ORIGINAL_TEXT
} q2_original_field_kind;

typedef struct q2_original_field {
    const char *name;
    size_t offset, count;
    q2_original_field_kind kind;
    uint16_t classic_offsets[3];
} q2_original_field;

typedef struct q2_original_layout {
    size_t size;
    const q2_original_field *fields;
    size_t count;
} q2_original_layout;

/* The caller stages the real typed state on reads. Classic offsets describe
 * the original MSVC x86 ABI; they are independent of our native C layout. */
typedef struct q2_original_record_io {
    qa_q2_edition edition;
    qa_q2_product product;
    qa_json_writer *writer;
    const qa_json_document *document;
    qa_json_id object;
    qa_bytes input, strings;
    struct q2_save_io *string_tail;
    qa_buffer output;
    qa_error *error;
    bool reading, references_only;
} q2_original_record_io;

/* Temporary Source values at the original file boundary, never a GAME owner. */
typedef struct q2_original_game_state {
    char help[2][512], spawnpoint[512];
    uint32_t help_changes[2], clients, entities, level_flags, unit_flags, items;
    bool autosave;
} q2_original_game_state;

typedef struct q2_original_client_state {
    qa_q2_player_state player;
    qa_q2_player_carry persistent;
    qa_q2_weapon_state weapon;
    qa_q2_powerups powers;
    qa_q2_player_view view;
    qa_movement_result movement;
    qa_vec3 command_angles;
    int32_t silencer;
    qa_actor_id sphere;
} q2_original_client_state;

typedef struct q2_original_level_state {
    uint32_t frame;
    uint64_t time_ns, intermission_ns;
    char name[64], map[64], next_map[64];
    qa_vec3 intermission_origin, intermission_angles;
    bool exit_intermission, intermission_clear;
    int32_t health_image, total_secrets, found_secrets, total_goals, found_goals;
    uint32_t total_monsters, killed_monsters, body_queue, power_cubes;
    uint64_t disguise_ns, restart_ns, autosave_ns;
} q2_original_level_state;

typedef struct q2_original_edict_row {
    uint32_t number;
    qa_bytes fields, strings;
    qa_json_id object;
} q2_original_edict_row;

typedef struct q2_original_level_file {
    qa_json_document *document;
    q2_original_level_state state;
    qa_bytes level_fields, level_strings;
    qa_json_id level_object;
    q2_original_edict_row *rows;
    size_t count;
} q2_original_level_file;

bool q2_original_source_text(q2_original_record_io *, qa_q2_game *, const char *,
    qa_string_id *, size_t);
bool q2_original_scalar(q2_original_record_io *, const char *, q2_original_field_kind,
    uint16_t, uint16_t, uint16_t, void *);
bool q2_original_item(qa_q2_game *, q2_original_record_io *, const char *,
    uint16_t, uint16_t, uint16_t, qa_item_id *);
bool q2_original_config_layout(qa_q2_edition, const qa_q2_save_level *, qa_q2_config_layout *, qa_error *);
bool q2_original_resource(qa_q2_game *, q2_original_record_io *, const qa_q2_save_level *,
    const char *, uint16_t, uint16_t, uint16_t, uint32_t, qa_string_id *);
bool q2_original_object_begin(q2_original_record_io *, const char *, q2_original_record_io *, bool *);
bool q2_original_object_end(q2_original_record_io *);
bool q2_original_game_open(qa_q2_game *, qa_bytes, qa_json_document **,
    q2_original_record_io *, q2_original_game_state *, qa_error *);
bool q2_original_client_capture(qa_q2_game *, q2_actor *, q2_original_client_state *, qa_error *);
bool q2_original_value(q2_original_record_io *, const q2_original_field *, void *);
bool q2_original_record(q2_original_record_io *, q2_original_record_kind, void *);
bool q2_original_client_record(qa_q2_game *, q2_original_record_io *,
    const qa_q2_save_level *, q2_original_client_state *);
void q2_original_client_free(q2_original_client_state *);
bool q2_original_write_game(qa_q2_game *, bool, const qa_q2_save_level *, qa_buffer *, qa_error *);
bool q2_original_level_open(qa_q2_game *, qa_bytes, q2_original_level_file *, qa_error *);
void q2_original_level_close(q2_original_level_file *);
const q2_original_edict_row *q2_original_level_actor(const q2_original_level_file *, uint32_t);
const q2_original_layout *q2_original_layout_for(q2_original_record_kind);
size_t q2_original_client_size(qa_q2_product);
size_t q2_original_entity_size(qa_q2_product);
size_t q2_original_level_size(qa_q2_product);

#endif
