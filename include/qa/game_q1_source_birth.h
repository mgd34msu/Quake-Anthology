#ifndef QA_GAME_Q1_SOURCE_BIRTH_H
#define QA_GAME_Q1_SOURCE_BIRTH_H

#include "qa/game_q1.h"

/* Select a foundation weapon on the genuine physical source client, even
 * when another arsenal controls the player's selected weapon. An unowned
 * weapon leaves the source continuation unchanged and reports selected=false. */
bool qa_q1_source_select_base_weapon(qa_q1_game *, qa_actor_id, qa_q1_weapon,
    bool *selected, qa_error *);

#endif
