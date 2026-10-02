#ifndef QA_GAME_Q1_SOURCE_ENTITIES_H
#define QA_GAME_Q1_SOURCE_ENTITIES_H

#include "qa/game_q1.h"

typedef struct qa_q1_source_entity {
    qa_actor_id actor, owner;
    qa_string_id classname;
    double count;
    uint32_t ordinal;
} qa_q1_source_entity;

/* The first existing native entity with this exact classname, in physical
 * source creation order. This observes genuine rows without acquiring them. */
bool qa_q1_source_entity_first(const qa_q1_game *, const char *,
    qa_q1_source_entity *, bool *found, qa_error *);

/* Pure native source observation, also admitted while an imported continuation
 * awaits finish. A live foreign/borrowed actor returns found=false; a retired
 * actor is an error. Common movement bits come from the real physics owner. */
bool qa_q1_source_movement_flags_read(const qa_q1_game *, qa_actor_id,
    uint32_t *out, bool *found, qa_error *);

#endif
