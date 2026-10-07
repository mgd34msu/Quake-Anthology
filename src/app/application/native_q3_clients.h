#ifndef QA_APPLICATION_NATIVE_Q3_CLIENTS_H
#define QA_APPLICATION_NATIVE_Q3_CLIENTS_H

#include "internal.h"
#include "qa/game_q3_clients.h"

/* GAME's physical client binding and retained source session exist before
 * Connect(false) reads userinfo during a real map/round readmission. */
bool application_native_q3_client_connect(application_provider *, qa_actor_id,
    uint32_t seat_or_none, const char *, bool first_time, bool bot,
    bool *accepted, const char **denial, qa_error *);
bool application_native_q3_client_begin(application_provider *, qa_actor_id,
    const qa_body_state *spawn_or_null, const qa_q3_usercmd *command_or_null, qa_error *);
bool application_native_q3_client_spawn(application_provider *, qa_actor_id,
    const qa_body_state *spawn_or_null, const qa_q3_usercmd *command_or_null, qa_error *);
bool application_native_q3_client_respawn(void *, qa_actor_id, qa_error *);
bool application_native_q3_client_death_items(void *, qa_actor_id, qa_error *);
bool application_native_q3_client_set_team(application_provider *, qa_actor_id,
    const char *request, qa_error *);
bool application_native_q3_client_stop_following_slot(application_provider *, uint32_t, qa_error *);
bool application_native_q3_client_disconnect(application_provider *, qa_actor_id, qa_error *);
bool application_native_q3_client_userinfo_changed(application_provider *, qa_actor_id, qa_error *);
bool application_native_q3_client_command(application_provider *, qa_actor_id,
    const qa_command_invocation *, bool *handled, qa_error *);
bool application_native_q3_source_client_command(application_provider *,qa_actor_id,
    const qa_command_invocation *,bool *handled,qa_error *);
bool application_native_q3_client_text(application_provider *, qa_actor_id, const char *, qa_error *);
bool application_native_q3_client_scoreboard(application_provider *, qa_actor_id, qa_error *);
bool application_native_q3_mode_client_slot(void *, qa_mode_id, qa_actor_id,
    qa_actor_owner *, uint32_t *, qa_error *);
bool application_native_q3_mode_source(void *, qa_mode_id, qa_actor_owner *);
bool application_native_q3_source_score_bound(void *, qa_mode_id, qa_actor_id, qa_actor_owner *);
application_provider *application_native_q3_mode_source_provider(qa_application *, qa_mode_id);
bool application_native_q3_mode_rank_client(void *, qa_mode_id, qa_actor_id,
    bool *, bool *, bool *, int32_t *, qa_error *);
bool application_native_q3_mode_rank_counts(void *, qa_mode_id,
    qa_mode_q3_rank_counts *, qa_error *);
bool application_native_q3_client_userinfo_changed_slot(application_provider *, uint32_t, qa_error *);
bool application_native_q3_client_print(void *, qa_actor_id, const char *, qa_error *);
bool application_native_q3_mode_choose_team(void *, qa_mode_id, qa_actor_id,
    qa_team_id *, qa_error *);
bool application_native_q3_client_pick_team(application_provider *, int32_t ignore_source_slot,
    int32_t *source_team, qa_error *);
bool application_native_q3_mode_vote_calls(void *, qa_mode_id, qa_actor_id,
    bool team_vote, int32_t *, qa_error *);
bool application_native_q3_clients_drain(qa_application *, qa_error *);
bool application_native_q3_clients_drain_provider(application_provider *, qa_error *);
bool application_native_q3_mode_team_request(void *, qa_mode_id, qa_actor_id, qa_team_id,
    bool spectator, bool automatic, int32_t spectator_state, int32_t spectator_client,
    bool *accepted, bool *changed, qa_error *);
bool application_native_q3_mode_stop_following(void *, qa_mode_id, qa_actor_id, qa_error *);
bool application_native_q3_mode_intermission_client(void *, qa_mode_id, qa_actor_id,
    bool *eligible, bool *ready, qa_error *);
bool application_native_q3_mode_ready_publish(void *, qa_mode_id, int32_t, qa_error *);
bool application_native_q3_client_think_policy(application_provider *, qa_actor_id,
    const qa_q3_usercmd *, qa_q3_usercmd *accepted, int32_t *msec, bool *run, qa_error *);
bool application_native_q3_client_think_special(application_provider *, qa_actor_id,
    const qa_q3_usercmd *, int32_t msec, bool *handled, qa_error *);
bool application_native_q3_client_spectator_buttons(application_provider *, qa_actor_id,
    const qa_q3_usercmd *, qa_error *);
bool application_native_q3_client_movement_parameters(application_provider *, qa_actor_id,
    bool source_client, int32_t *pm_type, int32_t *gravity, int32_t *speed, bool *spectator, qa_error *);
bool application_native_q3_client_deferred(application_provider *, qa_actor_id, bool *, qa_error *);
bool application_native_q3_source_client_run(void *, qa_actor_id,
    const qa_source_frame *, qa_error *);

#endif
