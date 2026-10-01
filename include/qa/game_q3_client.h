#ifndef QA_GAME_Q3_CLIENT_H
#define QA_GAME_Q3_CLIENT_H

#include "qa/game_q3.h"

struct qa_q3_usercmd;
/* Seed the actual native source client's angle words before its ordinary
 * spawn/view-angle producer. The accepted-command owner retains the complete
 * command for the following real ClientThink phase. This does not run it. */
bool qa_q3_player_begin_command(qa_q3_game *, qa_actor_id,
    const struct qa_q3_usercmd *, qa_error *);

#endif
