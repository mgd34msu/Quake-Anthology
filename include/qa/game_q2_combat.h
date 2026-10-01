#ifndef QA_GAME_Q2_COMBAT_H
#define QA_GAME_Q2_COMBAT_H

#include "qa/game_q2.h"

typedef struct qa_q2_combat_rules {
    qa_actor_owner owner;
    qa_q2_edition edition;
    qa_q2_product product;
    int skill;
    uint32_t deathmatch_flags;
    bool deathmatch, cooperative;
} qa_q2_combat_rules;

typedef struct qa_q2_combat_actor {
    bool character, player, monster, has_enemy, suppress_pain, defender_sphere;
    /* Classic Q2 monster_start retains the same edict's death no-kick flag. */
    bool birth_preserves_death_knockback;
    bool immortal, no_damage_effects;
} qa_q2_combat_actor;

typedef struct qa_q2_combat_life {
    qa_actor_owner character_owner;
    uint64_t birth_epoch, death_ns, source_ns;
    bool present, no_knockback, alive_knockback_only;
} qa_q2_combat_life;

/* Pure loans from this GAME's current rules and exact actor generation.
 * Actor absence leaves the output unchanged; no callback or allocation runs. */
bool qa_q2_combat_rules_read(const qa_q2_game *, qa_q2_combat_rules *);
bool qa_q2_combat_actor_read(const qa_q2_game *, qa_actor_id, qa_q2_combat_actor *);
bool qa_q2_combat_weapon_owned(const qa_q2_game *, qa_item_id);
bool qa_q2_combat_life_read(const qa_q2_game *, qa_actor_id, qa_q2_combat_life *);
/* The caller supplies a real selected CHARACTER birth, never a health-derived
 * epoch. A changed tuple retires prior death flags, except an existing
 * same-CHARACTER-owner Classic cache at a declared Classic Q2 monster birth. */
bool qa_q2_combat_life_bind(qa_q2_game *, qa_actor_id, qa_actor_owner character_owner,
                           uint64_t birth_epoch, bool birth_preserves_death_knockback, qa_error *);
bool qa_q2_combat_life_died(qa_q2_game *, qa_actor_id, qa_actor_owner character_owner,
                           uint64_t birth_epoch, bool birth_preserves_death_knockback, qa_error *);
/* Commits this COMBAT source's equal-frame surprise history on the exact
 * canonical actor. No body, health, armor, or simulation is created. */
bool qa_q2_combat_surprise(qa_q2_game *, qa_actor_id, bool has_enemy, bool *bonus, qa_error *);
/* Qualifies a copied powered armor value with its actual GAME source. */
bool qa_q2_combat_power_armor_source(const qa_q2_game *, qa_powered_armor *);

#endif
