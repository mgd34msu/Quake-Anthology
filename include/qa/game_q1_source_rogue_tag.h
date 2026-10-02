#ifndef QA_GAME_Q1_SOURCE_ROGUE_TAG_H
#define QA_GAME_Q1_SOURCE_ROGUE_TAG_H

#include "qa/game_q1.h"

typedef struct qa_q1_source_rogue_tag_services {
    void *context;
    bool (*current)(void *, const qa_q1_game *, qa_error *);
    bool (*player)(void *, qa_actor_id, bool *found, qa_error *);
    bool (*announce)(void *, const char *text, qa_actor_id, qa_error *);
    bool (*spawn_point)(void *, qa_actor_id *, qa_error *);
} qa_q1_source_rogue_tag_services;

bool qa_q1_source_rogue_tag_configure(qa_q1_game *,
    const qa_q1_source_rogue_tag_services *, qa_error *);
/* Evaluated by the real source obituary before frag publication. Token state
 * belongs to its physical source actor and the source world's reference. */
bool qa_q1_source_rogue_tag_score(qa_q1_game *, qa_actor_id victim,
    qa_actor_id attacker, int32_t *points, qa_error *);

#endif
