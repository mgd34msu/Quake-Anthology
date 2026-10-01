#ifndef QA_APPLICATION_GUEST_Q3_RESTART_H
#define QA_APPLICATION_GUEST_Q3_RESTART_H

#include "internal.h"

/* Readonly admission requires the completed live GAME owner. Clock and
 * gamestate reads also serve its admitted source callback/configuration phase.
 * Copy userinfo before entering the canonical retirement transaction. */
bool application_q3_guest_round_ready(application_provider *, qa_error *);
bool application_q3_guest_world_seed(application_provider *, uint32_t *, qa_error *);
/* Actual GAME cvar service owner at live, post-Shutdown handoff or fresh
 * replacement boundaries; no registration or source entry occurs. */
bool application_q3_guest_cvar_owner(application_provider *, uint64_t *, qa_error *);
/* After canonical retirement/detachment, consume already shut-down map source
 * executors and hosts while retaining their immutable role descriptors. */
bool application_q3_guest_retire_executors(application_provider *, qa_error *);
/* Genuine source-dependent UI Shutdown precedes actor retirement. Source and
 * physical seat identity may persist only when that actual borrow persists. */
bool application_q3_guest_client_sources_retire(application_provider *,
    application_provider *old_game, application_provider *next_game,
    const qa_launch_choices *, qa_error *);
/* Reconstruct physically consumed standalone client roles against the admitted
 * next GAME topology. Source Init waits for true client Begin and gamestate. */
bool application_q3_guest_client_sources_rebuild(application_provider *,
    application_provider *next_game, const qa_launch_choices *, qa_error *);
/* The real source slot binds before mode join; Connect and Begin remain owned
 * source callbacks and have not run at this reservation boundary. */
bool application_q3_guest_client_reserve(application_provider *, uint32_t source_slot,
    qa_actor_id, qa_error *);
bool application_q3_guest_actor_bound(application_provider *, qa_actor_id, uint32_t *source_slot);
/* All live providers include failed old owners retained outside the current
 * roster. This reads only genuine host/runtime pointer identity. */
bool application_q3_guest_bots_borrowed(const qa_application *, const qa_bot_runtime *);
/* Exact fresh original replacement: genuine Connect(false) precedes this
 * accepted command installation, which precedes the one real ClientBegin. */
bool application_q3_guest_client_carry(application_provider *, uint32_t source_slot,
    qa_actor_id, const qa_q3_usercmd *, qa_error *);
/* The real first network command is visible to ClientBegin without an input
 * sequence, command history, Think callback or transport acknowledgement. */
bool application_q3_guest_client_enter_command(application_provider *, uint32_t source_slot,
    qa_actor_id, const qa_q3_usercmd *, qa_error *);
bool application_q3_guest_world_source(application_provider *, int32_t *server_milliseconds,
    int32_t *loaded_game_type, uint32_t *seed, qa_error *);
bool application_q3_guest_world_userinfo(application_provider *, uint32_t source_slot,
    const char **, qa_error *);
bool application_q3_guest_world_client_read(application_provider *, uint32_t source_slot,
    qa_actor_id *, qa_q3_usercmd *, bool *bot, uint64_t *entered_ns, qa_error *);
/* Only during the actual successful old GAME shutdown handoff. */
bool application_q3_guest_handoff_client_read(application_provider *, uint32_t source_slot,
    qa_actor_id *, qa_q3_usercmd *, bool *bot, uint64_t *entered_ns,
    const char **userinfo, qa_error *);
bool application_q3_guest_snapshot_bit(application_provider *, uint8_t *, qa_error *);
bool application_q3_guest_round_clock(application_provider *, int32_t *, qa_error *);
bool application_q3_guest_round_gamestate_read(application_provider *,
    const qa_q3_gamestate **, qa_error *);
bool application_q3_guest_round_configstring_read(application_provider *, uint32_t,
    const char **, qa_error *);
bool application_q3_guest_round_userinfo(application_provider *, uint32_t source_slot,
    const char **, qa_error *);
bool application_q3_guest_round_client_read(application_provider *, uint32_t source_slot,
    qa_actor_id *, qa_q3_usercmd *, bool *bot, uint64_t *entered_ns, qa_error *);
bool application_q3_guest_round_configstring(application_provider *, uint32_t,
    const char *, qa_error *);

/* Begin runs at IDLE after the network cut has been qualified. Reset runs at
 * CONFIGURING after every canonical actor has retired, retaining executor owners,
 * source services, logical client slots and client transport histories. */
bool application_q3_guest_round_begin(application_provider *, qa_error *);
/* Genuine GAME Shutdown(true) while old actors, clients and bot routes remain
 * attached; reset consumes this successful source cut after actor retirement. */
bool application_q3_guest_round_shutdown(application_provider *, qa_error *);
bool application_q3_guest_round_reset(application_provider *, qa_error *);
/* Called only inside qa_session_round_step's real Q3 source admission. */
bool application_q3_guest_round_frame(application_provider *, qa_error *);
/* The outer cut selects source-local clients; retained remote transports queue
 * their own reliable projection through the qualified network owner. Client
 * publication and denial reads admit CONFIGURING or IDLE. Finish admits either
 * phase after all four source frames complete. */
bool application_q3_guest_round_queue_client(application_provider *, uint32_t source_slot, qa_error *);
bool application_q3_guest_round_reconnect(application_provider *, uint32_t source_slot,
    qa_actor_id, bool *accepted, qa_error *);
bool application_q3_guest_round_denial_read(application_provider *, uint32_t source_slot,
    const char **, qa_error *);
bool application_q3_guest_round_finish(application_provider *, qa_error *);

#endif
