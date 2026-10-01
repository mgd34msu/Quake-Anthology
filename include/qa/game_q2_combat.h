#ifndef QA_GAME_Q2_COMBAT_H
#define QA_GAME_Q2_COMBAT_H

#include "qa/game_q2.h"

typedef struct qa_q2_combat_rules {
    qa_actor_owner owner;
    qa_q2_edition edition;
    int skill;
    uint32_t deathmatch_flags;
    bool deathmatch, cooperative;
} qa_q2_combat_rules;

typedef struct qa_q2_combat_actor {
    bool character, player, monster, has_enemy, suppress_pain, defender_sphere;
} qa_q2_combat_actor;

/* Pure loans from this GAME's current rules and exact actor generation.
 * Actor absence leaves the output unchanged; no callback or allocation runs. */
bool qa_q2_combat_rules_read(const qa_q2_game *, qa_q2_combat_rules *);
bool qa_q2_combat_actor_read(const qa_q2_game *, qa_actor_id, qa_q2_combat_actor *);
bool qa_q2_combat_weapon_owned(const qa_q2_game *, qa_item_id);
/* Qualifies a copied powered armor value with its actual GAME source. */
bool qa_q2_combat_power_armor_source(const qa_q2_game *, qa_powered_armor *);

#endif
