#ifndef QA_Q1_CHECKPOINT_INTERNAL_H
#define QA_Q1_CHECKPOINT_INTERNAL_H

#include "../maps/internal.h"
#include "qa/binary.h"
#include "qa/game_q1_checkpoint.h"

enum { Q1_SAVE_VERSION = 2 };
typedef struct q1_save_io {
    qa_q1_game *game;
    qa_strings *dictionary;
    const qa_string_id *strings;
    size_t string_count;
    qa_bytes input;
    qa_buffer output;
    size_t offset, capacity;
    qa_error *error;
    bool reading;
} q1_save_io;

bool q1_save_fail(q1_save_io *, const char *);
bool q1_save_bytes(q1_save_io *, void *, size_t);
bool q1_save_u8(q1_save_io *, uint8_t *);
bool q1_save_u16(q1_save_io *, uint16_t *);
bool q1_save_u32(q1_save_io *, uint32_t *);
bool q1_save_u64(q1_save_io *, uint64_t *);
bool q1_save_i16(q1_save_io *, int16_t *);
bool q1_save_i32(q1_save_io *, int32_t *);
bool q1_save_i64(q1_save_io *, int64_t *);
bool q1_save_float(q1_save_io *, float *);
bool q1_save_double(q1_save_io *, double *);
bool q1_save_deadline(q1_save_io *, double *);
bool q1_save_bool(q1_save_io *, bool *);
bool q1_save_vector(q1_save_io *, qa_vec3 *);
bool q1_save_bounds(q1_save_io *, qa_bounds *);
bool q1_save_string(q1_save_io *, qa_string_id *);
bool q1_save_literal(q1_save_io *, const char **);
bool q1_save_actor(q1_save_io *, qa_actor_id *);
bool q1_save_owned_actor(q1_save_io *, qa_actor_id *);
bool q1_save_attack(q1_save_io *, qa_attack *);
bool q1_save_physics(q1_save_io *, qa_physics_properties *);
bool q1_save_player(q1_save_io *, q1_player *);
bool q1_save_entity(q1_save_io *, q1_actor *);
bool q1_save_monster(q1_save_io *, q1_monster *);
bool q1_save_map(q1_save_io *, q1_map_state *, q1_door_group **, size_t);
bool q1_save_runtime(q1_save_io *, qa_q1_game *);
bool q1_save_map_runtime(q1_save_io *, q1_map_runtime *);

#define Q1_SAVE(io, kind, member)                                                                  \
    do {                                                                                           \
        if (!q1_save_##kind((io), &(member)))                                                      \
            return false;                                                                          \
    } while (0)
#define Q1_SAVE_ENUM(io, member, last)                                                             \
    do {                                                                                           \
        uint32_t q1_saved_enum = (uint32_t)(member);                                               \
        Q1_SAVE((io), u32, q1_saved_enum);                                                         \
        if (q1_saved_enum > (uint32_t)(last))                                                      \
            return q1_save_fail((io), "Invalid Q1 checkpoint enum");                               \
        if ((io)->reading)                                                                         \
            (member) = q1_saved_enum;                                                              \
    } while (0)

#endif
