#ifndef QA_Q2_SAVE_INTERNAL_H
#define QA_Q2_SAVE_INTERNAL_H
#include "../entities/internal.h"
#include "qa/game_q2_checkpoint.h"
#include "qa/game_q2_monsters.h"
#include "qa/binary.h"
#include "qa/persistence_gameplay.h"

typedef struct q2_save_io {
    qa_q2_game *game;
    qa_bytes input;
    qa_buffer output;
    size_t offset, capacity;
    qa_error *error;
    bool reading, level_only;
} q2_save_io;
bool q2_save_fail(q2_save_io *, const char *);
bool q2_save_raw(q2_save_io *, void *, size_t);
bool q2_save_u32(q2_save_io *, uint32_t *);
bool q2_save_u64(q2_save_io *, uint64_t *);
bool q2_save_i32(q2_save_io *, int32_t *);
bool q2_save_i64(q2_save_io *, int64_t *);
bool q2_save_f32(q2_save_io *, float *);
bool q2_save_f64(q2_save_io *, double *);
bool q2_save_bool(q2_save_io *, bool *);
bool q2_save_vec(q2_save_io *, qa_vec3 *);
bool q2_save_string(q2_save_io *, qa_string_id *);
bool q2_save_text(q2_save_io *, char *, size_t);
bool q2_save_ref(q2_save_io *, qa_q2_saved_reference *);
bool q2_save_actor_pointer(q2_save_io *, qa_actor_reference *);
bool q2_save_count(q2_save_io *, size_t *, size_t minimum, size_t element_size, void **);
bool q2_save_attack(q2_save_io *, qa_attack *);
bool q2_save_visual(q2_save_io *, qa_q2_visual *);
bool q2_save_fog(q2_save_io *, qa_q2_fog *);
bool q2_save_landmark(q2_save_io *, qa_q2_landmark *);
bool q2_save_inventory(q2_save_io *, qa_inventory_entry **, size_t *);
bool q2_save_runtime(q2_save_io *, qa_q2_runtime_checkpoint *);
bool q2_save_actor(q2_save_io *, qa_q2_actor_checkpoint *);
bool q2_save_item(q2_save_io *, qa_q2_item_checkpoint *);
bool q2_save_player(q2_save_io *, qa_q2_player_checkpoint *);
bool q2_save_players(q2_save_io *, qa_q2_players_checkpoint *);
bool q2_save_entity(q2_save_io *, qa_q2_entity_checkpoint *);
bool q2_save_entities(q2_save_io *, qa_q2_entities_checkpoint *);
bool q2_save_monster(q2_save_io *, qa_q2_monster_checkpoint *);
bool q2_save_monsters(q2_save_io *, qa_q2_monsters_checkpoint *);
bool q2_save_wire(q2_save_io *);

#define Q2S(TYPE, MEMBER) do { if (!q2_save_##TYPE(io, &s->MEMBER)) return false; } while (0)
#define Q2U(MEMBER) do { uint32_t value = (uint32_t)s->MEMBER; \
    if (!q2_save_u32(io, &value)) return false; \
    if (io->reading) { s->MEMBER = value; \
        if ((uint64_t)s->MEMBER != (uint64_t)value) \
            return q2_save_fail(io, "Q2 continuation integer exceeds field range"); } } while (0)
#define Q2B(MEMBER) Q2S(bool, MEMBER)
#define Q2U8(MEMBER) do { uint32_t value = s->MEMBER; \
    if (!q2_save_u32(io, &value)) return false; \
    if (io->reading) { \
        if (value > UINT8_MAX) \
            return q2_save_fail(io, "Q2 continuation integer exceeds byte field range"); \
        s->MEMBER = (uint8_t)value; } } while (0)
#define Q2F(MEMBER) Q2S(f32, MEMBER)
#define Q2T(MEMBER) Q2S(u64, MEMBER)
#define Q2I(MEMBER) Q2S(i32, MEMBER)
#define Q2V(MEMBER) Q2S(vec, MEMBER)
#define Q2R(MEMBER) Q2S(ref, MEMBER)
#define Q2N(MEMBER) Q2S(string, MEMBER)
#endif
