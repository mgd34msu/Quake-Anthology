#ifndef QA_GAME_Q1_SOURCE_BIRTH_H
#define QA_GAME_Q1_SOURCE_BIRTH_H

#include "qa/game_q1.h"

/* Select a foundation weapon on the genuine physical source client, even
 * when another arsenal controls the player's selected weapon. An unowned
 * weapon leaves the source continuation unchanged and reports selected=false. */
bool qa_q1_source_select_base_weapon(qa_q1_game *, qa_actor_id, qa_q1_weapon,
    bool *selected, qa_error *);

/* The actual W_SetCurrentAmmo field write, before source spawn overlap and
 * later deathmatch grants. Inventory changes alone do not rewrite this field. */
bool qa_q1_source_current_ammo_select(qa_q1_game *, qa_actor_id, qa_error *);

/* QW PutClientInServer's deathmatch 5 grants after the teledeath stage.
 * Retains the earlier selected currentammo and the physical source clock. */
bool qa_q1_source_qw_dm5_birth(qa_q1_game *, qa_actor_id, qa_error *);

/* The composition supplies its genuine shared-grapple factory policy.
 * The physical source owns map identity, stock grants, armor and selection. */
bool qa_q1_source_ctf_spawn_arsenal(qa_q1_game *, qa_actor_id,
    bool native_grapple_enabled, bool grapple_disabled, qa_error *);

#endif
