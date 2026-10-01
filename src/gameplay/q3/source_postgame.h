#ifndef QA_Q3_SOURCE_POSTGAME_H
#define QA_Q3_SOURCE_POSTGAME_H

#include "qa/game_q3.h"

enum {
    Q3_POSTGAME_THINK_NONE,
    Q3_POSTGAME_THINK_PLACEMENT,
    Q3_POSTGAME_THINK_CELEBRATE_START,
    Q3_POSTGAME_THINK_CELEBRATE_STOP
};

/* These observations include the original disconnected fixed client rows. */
bool qa_q3_source_postgame_client_state(const qa_q3_game *, uint32_t,
    qa_q3_player_state *, qa_error *);
bool qa_q3_source_spawn_victory_pads(qa_q3_game *, qa_error *);
bool qa_q3_source_abort_podium(qa_q3_game *, qa_error *);
bool q3_postgame_step(qa_q3_game *, qa_actor_id, qa_error *);
bool q3_postgame_think_override(qa_q3_game *, qa_actor_id, bool *, qa_error *);
void q3_postgame_native_think_assigned(qa_q3_game *, qa_actor_id);
int32_t q3_postgame_think_time(const qa_q3_game *, qa_actor_id, int32_t fallback);
void q3_postgame_nextthink_assigned(qa_q3_game *, qa_actor_id, int32_t);

#endif
