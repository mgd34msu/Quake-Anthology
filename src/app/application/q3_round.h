#ifndef QA_APPLICATION_ROUND_Q3_H
#define QA_APPLICATION_ROUND_Q3_H

#include "internal.h"
#include "qa/application_q3_round.h"
#include "qa/game_q3_round.h"

typedef struct application_q3_round_players application_q3_round_players;
bool application_q3_round_players_prepare(qa_application *, application_provider *,
    application_q3_round_players **empty, qa_error *);
size_t application_q3_round_players_clients(const application_q3_round_players *,
    const qa_application_q3_round_client **borrowed);
bool application_q3_round_players_publish(qa_application *, application_provider *,
    application_q3_round_players *, qa_error *);
bool application_q3_round_player_admit(qa_application *, application_provider *,
    application_q3_round_players *, size_t, qa_actor_id *, bool *, const char **denial, qa_error *);
bool application_q3_round_players_finish(qa_application *, application_provider *,
    application_q3_round_players *, qa_error *);
void application_q3_round_players_dispose(application_q3_round_players *);

bool application_q3_round_map_prepare(qa_application *, application_provider *,
    qa_entities *empty, qa_error *);
bool application_q3_round_map_spawn(application_provider *, const qa_entities *, qa_error *);
bool application_q3_round_restart(qa_application *, application_provider *,
    qa_mode_id, bool *mutated, qa_error *);
bool application_q3_round_compatible(qa_application *, application_provider *,
    qa_mode_id, bool *compatible, qa_error *);

#endif
