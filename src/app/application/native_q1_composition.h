#ifndef QA_APPLICATION_NATIVE_Q1_COMPOSITION_H
#define QA_APPLICATION_NATIVE_Q1_COMPOSITION_H

#include "internal.h"

bool application_native_q1_composition_prepare(qa_application *, application_publication *, qa_error *);
bool application_native_q1_composition_expected(void *, qa_modes *, bool *, qa_actor_owner *,
    qa_mode_source *, qa_error *);
bool application_native_q1_composition_current(void *, qa_actor_owner, qa_mode_source, qa_error *);
bool application_native_q1_composition_player_current(void *, qa_actor_owner, qa_mode_source,
    qa_actor_id, bool *observer, qa_error *);
bool application_native_q1_composition_mode(qa_application *, application_provider *, qa_mode_id *, qa_error *);
bool application_native_q1_composition_players_capture(qa_application *, qa_q1_game *,
    uint32_t client_slots, qa_world_actor_snapshot *, qa_error *);

#endif
