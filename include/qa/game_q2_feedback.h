#ifndef QA_GAME_Q2_FEEDBACK_H
#define QA_GAME_Q2_FEEDBACK_H

#include "qa/game_q2_player.h"

/* Original Q2 temp-entity ordinals. Damage is not a particle count on wire. */
typedef enum qa_q2_damage_effect {
    QA_Q2_DAMAGE_BLOOD = 1,
    QA_Q2_DAMAGE_SPARKS = 9,
    QA_Q2_DAMAGE_SCREEN_SPARKS = 12,
    QA_Q2_DAMAGE_SHIELD_SPARKS = 13,
    QA_Q2_DAMAGE_BULLET_SPARKS = 14,
    QA_Q2_DAMAGE_GREEN_BLOOD = 26,
    QA_Q2_DAMAGE_MORE_BLOOD = 42,
    QA_Q2_DAMAGE_ELECTRIC_SPARKS = 46
} qa_q2_damage_effect;

/* Mutates only an actually connected RR client's signed 16-bit PS stat.
 * Missing clients set found=false without creating a client or actor state. */
bool qa_q2_player_hit_marker_add(qa_q2_game *, qa_actor_id, int64_t damage,
                                bool *found, qa_error *);
/* Publishes CheckPowerArmor's pulse at its actual reached effect boundary.
 * Missing source clients remain absent; this does not create actor state. */
bool qa_q2_player_power_armor_activate(qa_q2_game *, qa_actor_id,
                                      bool *found, qa_error *);

#endif
