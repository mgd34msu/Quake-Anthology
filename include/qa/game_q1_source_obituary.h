#ifndef QA_GAME_Q1_SOURCE_OBITUARY_H
#define QA_GAME_Q1_SOURCE_OBITUARY_H

#include "qa/game_q1.h"

enum { QA_Q1_SOURCE_MESSAGE_LITERAL = 4u };

typedef struct qa_q1_source_obituary_actor {
    qa_actor_id owner;
    qa_string_id classname, kill_string, death_type;
    const char *name;
    double team, quad_remaining, invulnerable_remaining;
    double quad_finished;
    qa_q1_weapon weapon;
    qa_physics_motion movement;
    int32_t water_level;
    bool entity, client, monster, brush, horde_source_die;
} qa_q1_source_obituary_actor;

/* Observe the existing physical source entity/client and private arsenal.
 * Name borrows that retained GAME's strings. Canonical health, foreign actor
 * classname and a source client's current selected water remain their actual
 * owners' observations; this query neither substitutes nor publishes them. */
bool qa_q1_source_obituary_read(const qa_q1_game *, qa_actor_id,
    qa_q1_source_obituary_actor *, qa_error *);

#endif
